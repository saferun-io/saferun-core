#include "wallet.hpp"

#include <common/hex.h>
#include <cryptonote_basic/cryptonote_basic.h>
#include <oxenmq/oxenmq.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <chrono>
#include <future>
#include <iostream>
#include <oxen/log.hpp>
#include <sqlitedb/database.hpp>
#include <thread>

#include "block.hpp"
#include "block_tx.hpp"
#include "db/walletdb.hpp"
#include "default_daemon_comms.hpp"
#include "wallet2½.hpp"

namespace wallet {
static auto logcat = oxen::log::Cat("wallet");

fs::path file_path_from_default_datadir(const Config& c, const fs::path& filename) {
    if (filename.string() == ":memory:")
        return filename;

    auto file_location = fs::absolute(fs::u8path(c.general.datadir));
    if (c.general.nettype != "mainnet" && c.general.append_network_type_to_datadir)
        file_location /= c.general.nettype;
    file_location /= filename;

    return file_location;
}

Wallet::Wallet(
        std::shared_ptr<oxenmq::OxenMQ> omq,
        std::shared_ptr<Keyring> keyring,
        std::shared_ptr<TransactionConstructor> tx_constructor,
        std::shared_ptr<DaemonComms> daemon_comms,
        std::string_view dbFilename,
        std::string_view dbPassword,
        wallet::Config config_in) :
        omq(omq),
        db{std::make_shared<WalletDB>(
                file_path_from_default_datadir(config_in, dbFilename), dbPassword)},
        keys{std::move(keyring)},
        tx_scanner{keys, db},
        tx_constructor{tx_constructor},
        daemon_comms{daemon_comms},
        omq_server{request_handler},
        config(config_in) {
    if (not omq)
        this->omq = std::make_shared<oxenmq::OxenMQ>();
    if (not daemon_comms)
        this->daemon_comms = std::make_shared<DefaultDaemonComms>(omq, config.daemon);
    if (not tx_constructor)
        this->tx_constructor = std::make_shared<TransactionConstructor>(db, daemon_comms);

    config.omq_rpc.sockname =
            file_path_from_default_datadir(config, config.omq_rpc.sockname).string();
    omq_server.set_omq(this->omq, config.omq_rpc);

    db->create_schema();
    if (!keys) {
        const auto db_keys = db->load_keys();
        keys = std::make_shared<wallet::Keyring>(
                db_keys->spend_privkey(),
                db_keys->spend_pubkey(),
                db_keys->view_privkey(),
                db_keys->view_pubkey(),
                nettype);
        tx_scanner.set_keys(keys);
    }
    db->save_keys(keys);
    db->add_address(0, 0, keys->get_main_address());
    last_scan_height = db->last_scan_height();
    scan_target_height = db->scan_target_height();
}

void Wallet::init() {
    keys->expand_subaddresses(
            {config.general.subaddress_lookahead_major, config.general.subaddress_lookahead_minor});
    oxen::log::reset_level(*oxen::logging::parse_level(config.logging.level));
    fs::path log_location = "";
    if (config.logging.save_logs_in_subdirectory)
        log_location /= config.logging.logdir;
    log_location /= config.logging.log_filename;

    log_location = file_path_from_default_datadir(config, log_location);

    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_location.string(),
            config.logging.log_file_size_limit,
            config.logging.extra_files,
            config.logging.rotate_on_open);

    oxen::log::add_sink(std::move(file_sink));
    oxen::log::info(logcat, "Writing logs to {}", log_location.string());

    oxen::log::info(logcat, "Remote Daemon set to {}", config.daemon.address);
    request_handler.set_wallet(weak_from_this());
    omq->start();
    oxen::log::info(logcat, "OMQ started");
    daemon_comms->set_remote(config.daemon.address);
    daemon_comms->register_wallet(
            *this,
            last_scan_height + 1 /*next needed block*/,
            true /* update sync height */,
            true /* new wallet */);
    oxen::log::info(logcat, "Finished wallet init");
}

Wallet::~Wallet() {}

void Wallet::propogate_config() {
    daemon_comms->propogate_config();
}

uint64_t Wallet::get_balance() {
    return db->overall_balance();
}

uint64_t Wallet::get_unlocked_balance() {
    return db->unlocked_balance();
}

cryptonote::account_keys Wallet::export_keys() {
    return keys->export_keys();
};

void Wallet::add_block(const Block& block) {
    oxen::log::trace(logcat, "add block called with block height {}", block.height);
    auto db_tx = db->db_transaction();

    db->store_block(block);

    for (const auto& tx : block.transactions) {
        if (auto outputs = tx_scanner.scan_received(tx, block.height, block.timestamp);
            not outputs.empty()) {
            oxen::log::info(
                    logcat,
                    "outputs: tx.hash {}, block.height {}, outputs {}",
                    tx.hash,
                    block.height,
                    outputs.size());
            db->store_transaction(tx.hash, block.height, outputs);
        }

        if (auto spends = tx_scanner.scan_spent(tx.tx); not spends.empty()) {
            oxen::log::info(
                    logcat,
                    "spends: tx.hash {}, block.height {}, spends {}",
                    tx.hash,
                    block.height,
                    spends.size());
            db->store_spends(tx.hash, block.height, spends);
        }
    }

    db_tx.commit();

    last_scan_height++;
}

void Wallet::add_blocks(const std::vector<Block>& blocks) {
    if (not running)
        return;

    if (blocks.size() == 0)
        throw std::runtime_error("no blocks sent to add blocks");

    if (blocks.front().height > last_scan_height + 1) {
        oxen::log::warning(
                logcat,
                "blocks.front height is greater than last scan height, calling register wallet "
                "with last scan height of {}",
                last_scan_height + 1);
        daemon_comms->register_wallet(*this, last_scan_height + 1 /*next needed block*/, true);
        return;
    }

    for (const auto& block : blocks) {
        if (block.height == last_scan_height + 1)
            add_block(block);
    }
    daemon_comms->register_wallet(*this, last_scan_height + 1 /*next needed block*/, false);
}

void Wallet::update_top_block_info(int64_t height, const crypto::hash& hash) {
    if (not running)
        return;

    db->update_top_block_info(height, hash);

    scan_target_height = height;
}

void Wallet::deregister() {
    running = false;
    auto self = weak_from_this();
    std::promise<void> p;
    auto f = p.get_future();
    daemon_comms->deregister_wallet(*this, p);
    f.wait();

    /*
    // At this point, only the true "owner" should have a reference
    using namespace std::chrono_literals;
    while (self.use_count() > 1)
      std::this_thread::sleep_for(50ms);
    */
}

}  // namespace wallet
