#include "oxen_logger.h"

#include <spdlog/sinks/rotating_file_sink.h>

#include <filesystem>
#include <oxen/log.hpp>

namespace oxen::logging {
static auto logcat = log::Cat("logging");

void set_additional_log_categories(log::Level& log_level) {
    switch (log_level) {
        case log::Level::critical: break;
        case log::Level::err: break;
        case log::Level::warn:
            log::set_level("net", log::Level::err);
            log::set_level("net.http", log::Level::err);
            log::set_level("net.p2p", log::Level::err);
            log::set_level("net.p2p.msg", log::Level::err);
            log::set_level("global", log::Level::info);
            log::set_level("verify", log::Level::err);
            log::set_level("serialization", log::Level::err);
            log::set_level("logging", log::Level::info);
            log::set_level("msgwriter", log::Level::info);
            break;
        case log::Level::info:
            log::set_level("net", log::Level::err);
            log::set_level("net.http", log::Level::err);
            log::set_level("net.p2p", log::Level::err);
            log::set_level("net.p2p.msg", log::Level::err);
            log::set_level("verify", log::Level::err);
            log::set_level("serialization", log::Level::err);
            log::set_level("blockchain", log::Level::warn);
            log::set_level("blockchain.db.lmdb", log::Level::warn);
            log::set_level("service_nodes", log::Level::warn);
            log::set_level("txpool", log::Level::warn);
            log::set_level("construct_tx", log::Level::warn);
            break;
        case log::Level::debug: break;
        case log::Level::trace: break;
        default: break;
    }
}

void process_categories_string(const std::string& categories) {
    std::istringstream iss(categories);
    std::string single_category_and_level, single_category, level_str;
    std::optional<log::Level> log_level;
    std::string::size_type level_separator = 0;

    while (getline(iss, single_category_and_level, ',')) {
        if ((level_separator = single_category_and_level.find(':')) != std::string::npos) {
            single_category = single_category_and_level.substr(0, level_separator);
            level_str = single_category_and_level.substr(level_separator + 1, std::string::npos);
            log_level = parse_level(level_str);
            if (log_level.has_value()) {
                if (single_category == "*")
                    log::reset_level(*log_level);
                else
                    log::set_level(single_category, *log_level);
            }
        }
    }
    log::info(logcat, "New log categories");
}

void init(const std::string& log_location, log::Level log_level, bool log_to_stdout) {
    log::reset_level(log_level);
    if (log_to_stdout)
        log::add_sink(log::Type::Print, "stdout");
    set_file_sink(log_location);
    set_additional_log_categories(log_level);
}

void set_file_sink(const std::string& log_location) {
    constexpr size_t LOG_FILE_SIZE_LIMIT = 1024 * 1024 * 50;  // 50MiB
    constexpr size_t EXTRA_FILES = 1;

    // setting this to `true` can be useful for debugging on testnet
    bool rotate_on_open = false;

    try {
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_location, LOG_FILE_SIZE_LIMIT, EXTRA_FILES, rotate_on_open);

        log::add_sink(std::move(file_sink));
    } catch (const spdlog::spdlog_ex& ex) {
        log::error(
                logcat,
                "Failed to open {} for logging: {}.  File logging disabled.",
                log_location,
                ex.what());
        return;
    }

    log::info(logcat, "Writing logs to {}", log_location);
}

using namespace std::literals;

using strlvl = std::pair<std::string_view, log::Level>;
static constexpr std::array logLevels = {
        strlvl{""sv, log::Level::info},         strlvl{"4"sv, log::Level::trace},
        strlvl{"3"sv, log::Level::trace},       strlvl{"2"sv, log::Level::debug},
        strlvl{"1"sv, log::Level::info},        strlvl{"0"sv, log::Level::warn},
        strlvl{"trace"sv, log::Level::trace},   strlvl{"trc"sv, log::Level::trace},
        strlvl{"debug"sv, log::Level::debug},   strlvl{"dbg"sv, log::Level::debug},
        strlvl{"info"sv, log::Level::info},     strlvl{"inf"sv, log::Level::info},
        strlvl{"warning"sv, log::Level::warn},  strlvl{"warn"sv, log::Level::warn},
        strlvl{"wrn"sv, log::Level::warn},      strlvl{"error"sv, log::Level::err},
        strlvl{"err"sv, log::Level::err},       strlvl{"critical"sv, log::Level::critical},
        strlvl{"crit"sv, log::Level::critical}, strlvl{"crt"sv, log::Level::critical},
};

std::optional<spdlog::level::level_enum> parse_level(std::string input) {
    // Lower-case the input:
    for (auto& c : input)
        if (c >= 'A' && c <= 'Z')
            c += ('A' - 'a');

    for (const auto& [str, lvl] : logLevels)
        if (str == input)
            return lvl;

    return std::nullopt;
}

static constexpr std::array<std::pair<uint8_t, log::Level>, 5> intLogLevels = {
        {{4, log::Level::trace},
         {3, log::Level::trace},
         {2, log::Level::debug},
         {1, log::Level::info},
         {0, log::Level::warn}}};

std::optional<spdlog::level::level_enum> parse_level(uint8_t input) {
    for (const auto& [str, lvl] : intLogLevels)
        if (str == input)
            return lvl;
    return std::nullopt;
}

static constexpr std::array<std::pair<oxenmq::LogLevel, log::Level>, 6> omqLogLevels = {
        {{oxenmq::LogLevel::trace, log::Level::trace},
         {oxenmq::LogLevel::debug, log::Level::debug},
         {oxenmq::LogLevel::info, log::Level::info},
         {oxenmq::LogLevel::warn, log::Level::warn},
         {oxenmq::LogLevel::error, log::Level::err},
         {oxenmq::LogLevel::fatal, log::Level::critical}}};

std::optional<spdlog::level::level_enum> parse_level(oxenmq::LogLevel input) {
    for (const auto& [str, lvl] : omqLogLevels)
        if (str == input)
            return lvl;
    return std::nullopt;
}

}  // namespace oxen::logging
