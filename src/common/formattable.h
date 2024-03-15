#pragma once

#include <fmt/format.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace formattable {

// Types can opt-in to being formattable as a string by specializing this to true.  Such a type
// must have one of:
//
// - a `to_string()` method on the type; when formatted we will call `val.to_string()` to format
//   it as a string.
// - a `to_string(val)` function in the same namespace as the type; we will call it to format it
//   as a string.
//
// The function should return something string-like (string, string_view, const char*).
//
// For instance to opt-in MyType for such string formatting, use:
//
//     template <> inline constexpr bool formattable::via_to_string<MyType> = true;
//
// You can also partially specialize; for instance to make all derived classes of a common base
// type formattable via to_string you could do:
//
//     template <T> inline constexpr bool formattable::via_to_string<T,
//         std::enable_if_t<std::is_base_of_v<MyBaseType, T>>> = true;
//
template <typename T, typename SFINAE = void>
constexpr bool via_to_string = false;

// Same as above, but looks for a to_hex_string() instead of to_string(), for types that get
// dumped as hex.
template <typename T, typename SFINAE = void>
constexpr bool via_to_hex_string = false;

// Scoped enums can alternatively be formatted as their underlying integer value by specializing
// this function to true:
template <typename T, typename SFINAE = void>
constexpr bool via_underlying = false;

namespace detail {

    template <typename T, typename SFINAE = void>
    constexpr bool has_to_string_method = false;
    template <typename T, typename SFINAE = void>
    constexpr bool has_to_hex_string_method = false;

    template <typename T>
    inline constexpr bool
            has_to_string_method<T, std::void_t<decltype(std::declval<const T&>().to_string())>> =
                    true;

    template <typename T>
    inline constexpr bool has_to_hex_string_method<
            T,
            std::void_t<decltype(std::declval<const T&>().to_hex_string())>> = true;

}  // namespace detail

template <typename T>
struct to_string_formatter : fmt::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(const T& val, FormatContext& ctx) const {
        if constexpr (::formattable::detail::has_to_string_method<T>)
            return formatter<std::string_view>::format(val.to_string(), ctx);
        else
            return formatter<std::string_view>::format(to_string(val), ctx);
    }
};

template <typename T>
struct to_hex_string_formatter : fmt::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(const T& val, FormatContext& ctx) const {
        if constexpr (::formattable::detail::has_to_hex_string_method<T>)
            return formatter<std::string_view>::format(val.to_hex_string(), ctx);
        else
            return formatter<std::string_view>::format(to_hex_string(val), ctx);
    }
};

template <typename T>
struct underlying_t_formatter : fmt::formatter<std::underlying_type_t<T>> {
#ifdef __cpp_lib_is_scoped_enum  // C++23
    static_assert(std::is_scoped_enum_v<T>);
#else
    static_assert(
            std::is_enum_v<T> && !std::is_convertible_v<T, std::underlying_type_t<T>>,
            "formattable::via_underlying<T> type is not a scoped enum");
#endif
    template <typename FormatContext>
    auto format(const T& val, FormatContext& ctx) const {
        using Underlying = std::underlying_type_t<T>;
        return fmt::formatter<Underlying>::format(static_cast<Underlying>(val), ctx);
    }
};

}  // namespace formattable

namespace fmt {

template <typename T, typename Char>
struct formatter<T, Char, std::enable_if_t<::formattable::via_to_string<T>>>
        : ::formattable::to_string_formatter<T> {};

template <typename T, typename Char>
struct formatter<T, Char, std::enable_if_t<::formattable::via_to_hex_string<T>>>
        : ::formattable::to_hex_string_formatter<T> {};

template <typename T, typename Char>
struct formatter<T, Char, std::enable_if_t<::formattable::via_underlying<T>>>
        : ::formattable::underlying_t_formatter<T> {};

}  // namespace fmt
