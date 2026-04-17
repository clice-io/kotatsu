#pragma once

#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace kota::codec {

namespace content {

template <typename Config>
class Deserializer;

}  // namespace content

namespace detail {

template <typename D, typename = void>
struct captured_dom_value_type {};

template <typename D>
struct captured_dom_value_type<D, std::void_t<decltype(std::declval<D&>().capture_dom_value())>> {
    using type = std::remove_cvref_t<decltype(*std::declval<D&>().capture_dom_value())>;
};

template <typename D>
using captured_dom_value_t = typename captured_dom_value_type<D>::type;

template <typename D, typename V, typename = void>
struct is_captured_dom_value : std::false_type {};

template <typename D, typename V>
struct is_captured_dom_value<D, V, std::void_t<decltype(std::declval<D&>().capture_dom_value())>> :
    std::bool_constant<std::same_as<std::remove_cvref_t<V>, captured_dom_value_t<D>>> {};

template <typename D, typename V>
constexpr bool is_captured_dom_value_v = is_captured_dom_value<D, V>::value;

template <typename D, typename = void>
struct can_buffer_adjacently_tagged : std::false_type {};

template <typename D>
    struct can_buffer_adjacently_tagged<
        D,
        std::void_t<decltype(std::declval<D&>().capture_dom_value()), typename D::config_type>> :
    std::bool_constant < requires(const captured_dom_value_t<D>& captured) {
    content::Deserializer<typename D::config_type>{captured};
}>{};

template <typename D>
constexpr bool can_buffer_adjacently_tagged_v = can_buffer_adjacently_tagged<D>::value;

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
