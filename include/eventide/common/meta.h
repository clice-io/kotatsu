#pragma once

#include <concepts>
#include <expected>
#include <format>
#include <optional>
#include <type_traits>

namespace eventide {

template <typename T>
constexpr inline bool dependent_false = false;

template <typename T, typename... Ts>
concept is_one_of = (std::same_as<T, Ts> || ...);

template <template <typename...> typename HKT, typename T>
constexpr inline bool is_specialization_of = false;

template <template <typename...> typename HKT, typename... Ts>
constexpr inline bool is_specialization_of<HKT, HKT<Ts...>> = true;

template <typename T>
concept Formattable = std::formattable<T, char>;

template <typename L, typename R>
concept eq_comparable_with = requires(const L& lhs, const R& rhs) {
    { lhs == rhs } -> std::convertible_to<bool>;
};

template <typename L, typename R>
concept ne_comparable_with = requires(const L& lhs, const R& rhs) {
    { lhs != rhs } -> std::convertible_to<bool>;
};

template <typename L, typename R>
concept lt_comparable_with = requires(const L& lhs, const R& rhs) {
    { lhs < rhs } -> std::convertible_to<bool>;
};

template <typename L, typename R>
concept le_comparable_with = requires(const L& lhs, const R& rhs) {
    { lhs <= rhs } -> std::convertible_to<bool>;
};

template <typename L, typename R>
concept gt_comparable_with = requires(const L& lhs, const R& rhs) {
    { lhs > rhs } -> std::convertible_to<bool>;
};

template <typename L, typename R>
concept ge_comparable_with = requires(const L& lhs, const R& rhs) {
    { lhs >= rhs } -> std::convertible_to<bool>;
};

template <typename T>
constexpr inline bool is_optional_v = is_specialization_of<std::optional, std::remove_cvref_t<T>>;

template <typename T>
constexpr inline bool is_expected_v = is_specialization_of<std::expected, std::remove_cvref_t<T>>;

}  // namespace eventide
