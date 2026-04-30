#pragma once

#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace kota::codec {

namespace detail {

template <typename To, typename From>
constexpr bool integral_value_in_range(From value) {
    static_assert(std::is_integral_v<To>);
    static_assert(std::is_integral_v<From>);

    if constexpr(std::is_signed_v<To> == std::is_signed_v<From>) {
        using compare_t = std::conditional_t<(sizeof(From) > sizeof(To)), From, To>;
        return static_cast<compare_t>(value) >=
                   static_cast<compare_t>((std::numeric_limits<To>::lowest)()) &&
               static_cast<compare_t>(value) <=
                   static_cast<compare_t>((std::numeric_limits<To>::max)());
    } else if constexpr(std::is_signed_v<From>) {
        if(value < 0) {
            return false;
        }

        using from_unsigned_t = std::make_unsigned_t<From>;
        using to_unsigned_t = std::make_unsigned_t<To>;
        using compare_t = std::common_type_t<from_unsigned_t, to_unsigned_t>;
        return static_cast<compare_t>(static_cast<from_unsigned_t>(value)) <=
               static_cast<compare_t>((std::numeric_limits<To>::max)());
    } else {
        using from_unsigned_t = std::make_unsigned_t<From>;
        using to_unsigned_t = std::make_unsigned_t<To>;
        using compare_t = std::common_type_t<from_unsigned_t, to_unsigned_t>;
        return static_cast<compare_t>(static_cast<from_unsigned_t>(value)) <=
               static_cast<compare_t>((std::numeric_limits<To>::max)());
    }
}

template <typename T>
struct remove_annotation {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
    requires requires { typename std::remove_cvref_t<T>::annotated_type; }
struct remove_annotation<T> {
    using type = std::remove_cvref_t<typename std::remove_cvref_t<T>::annotated_type>;
};

template <typename T>
using remove_annotation_t = typename remove_annotation<T>::type;

template <typename T>
struct remove_optional {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
struct remove_optional<std::optional<T>> {
    using type = std::remove_cvref_t<T>;
};

template <typename T>
using remove_optional_t = typename remove_optional<std::remove_cvref_t<T>>::type;

template <typename T>
using clean_t = remove_optional_t<remove_annotation_t<T>>;

}  // namespace detail

}  // namespace kota::codec
