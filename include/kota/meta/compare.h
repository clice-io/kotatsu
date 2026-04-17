#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <type_traits>
#include <utility>

#include "struct.h"
#include "kota/support/ranges.h"

namespace kota::meta::detail {

template <typename T>
concept standard_integer = std::same_as<std::remove_cv_t<T>, signed char> ||
                           std::same_as<std::remove_cv_t<T>, short> ||
                           std::same_as<std::remove_cv_t<T>, int> ||
                           std::same_as<std::remove_cv_t<T>, long> ||
                           std::same_as<std::remove_cv_t<T>, long long> ||
                           std::same_as<std::remove_cv_t<T>, unsigned char> ||
                           std::same_as<std::remove_cv_t<T>, unsigned short> ||
                           std::same_as<std::remove_cv_t<T>, unsigned int> ||
                           std::same_as<std::remove_cv_t<T>, unsigned long> ||
                           std::same_as<std::remove_cv_t<T>, unsigned long long>;

template <typename L, typename R>
concept reflectable_pair = reflectable_class<L> && reflectable_class<R>;

template <typename T>
using range_ref_t = std::ranges::range_reference_t<const std::remove_reference_t<T>>;

struct eq_op {
    template <typename A, typename B>
    constexpr inline static bool comparable = eq_comparable_with<A, B>;
};

struct ne_op {
    template <typename A, typename B>
    constexpr inline static bool comparable = ne_comparable_with<A, B>;
};

struct lt_op {
    template <typename A, typename B>
    constexpr inline static bool comparable = lt_comparable_with<A, B>;
};

struct le_op {
    template <typename A, typename B>
    constexpr inline static bool comparable = le_comparable_with<A, B>;
};

struct gt_op {
    template <typename A, typename B>
    constexpr inline static bool comparable = gt_comparable_with<A, B>;
};

struct ge_op {
    template <typename A, typename B>
    constexpr inline static bool comparable = ge_comparable_with<A, B>;
};

template <typename Op, typename A, typename B>
concept op_comparable_with = Op::template comparable<A, B>;

template <typename Op, typename L, typename R>
consteval bool range_value_op_comparable() {
    if constexpr(map_range<L> && map_range<R>) {
        using LK = typename std::remove_cvref_t<L>::key_type;
        using RK = typename std::remove_cvref_t<R>::key_type;
        using LV = typename std::remove_cvref_t<L>::mapped_type;
        using RV = typename std::remove_cvref_t<R>::mapped_type;
        return range_value_op_comparable<Op, LK, RK>() && range_value_op_comparable<Op, LV, RV>();
    } else if constexpr(set_range<L> && set_range<R>) {
        using LK = typename std::remove_cvref_t<L>::key_type;
        using RK = typename std::remove_cvref_t<R>::key_type;
        return range_value_op_comparable<Op, LK, RK>();
    } else if constexpr(sequence_range<L> && sequence_range<R>) {
        return range_value_op_comparable<Op, range_ref_t<L>, range_ref_t<R>>();
    } else {
        return op_comparable_with<Op, L, R>;
    }
}

// Take over range comparison only when:
// 1) the container-level operator is missing, or
// 2) element-level operator is missing.
template <typename Op, typename L, typename R>
concept takeover_for_range =
    ((sequence_range<L> && sequence_range<R>) || (set_range<L> && set_range<R>) ||
     (map_range<L> && map_range<R>)) &&
    (!op_comparable_with<Op, L, R> || !range_value_op_comparable<Op, L, R>());

template <typename L, typename R>
concept takeover_range_eq = takeover_for_range<eq_op, L, R>;

template <typename L, typename R>
concept takeover_range_ne = takeover_for_range<ne_op, L, R>;

template <typename L, typename R>
concept takeover_range_lt = takeover_for_range<lt_op, L, R>;

template <typename L, typename R>
concept takeover_range_le = takeover_for_range<le_op, L, R>;

template <typename L, typename R>
concept takeover_range_gt = takeover_for_range<gt_op, L, R>;

template <typename L, typename R>
concept takeover_range_ge = takeover_for_range<ge_op, L, R>;

template <typename L, typename R>
constexpr bool compare_eq(const L& lhs, const R& rhs);

template <typename L, typename R>
constexpr bool compare_lt(const L& lhs, const R& rhs);

template <typename L, typename R, typename ElemEq>
constexpr bool compare_range_eq(const L& lhs, const R& rhs, const ElemEq& elem_eq) {
    if constexpr(std::ranges::sized_range<const L> && std::ranges::sized_range<const R>) {
        if(std::ranges::size(lhs) != std::ranges::size(rhs)) {
            return false;
        }
    }

    auto lit = std::ranges::begin(lhs);
    auto lend = std::ranges::end(lhs);
    auto rit = std::ranges::begin(rhs);
    auto rend = std::ranges::end(rhs);

    while(lit != lend && rit != rend) {
        if(!elem_eq(*lit, *rit)) {
            return false;
        }
        ++lit;
        ++rit;
    }
    return lit == lend && rit == rend;
}

template <typename L, typename R, typename ElemLt>
constexpr bool compare_range_lt(const L& lhs, const R& rhs, const ElemLt& elem_lt) {
    auto lit = std::ranges::begin(lhs);
    auto lend = std::ranges::end(lhs);
    auto rit = std::ranges::begin(rhs);
    auto rend = std::ranges::end(rhs);

    while(lit != lend && rit != rend) {
        if(elem_lt(*lit, *rit)) {
            return true;
        }
        if(elem_lt(*rit, *lit)) {
            return false;
        }
        ++lit;
        ++rit;
    }

    return lit == lend && rit != rend;
}

template <typename L, typename R>
concept findable_in = requires(const R& range, const L& value) {
    { range.find(value) };
    { range.end() };
    { range.find(value) == range.end() } -> std::convertible_to<bool>;
};

template <typename L, typename R>
constexpr bool map_entries_contained_in(const L& lhs, const R& rhs) {
    // Checks whether every (key, value) entry in lhs has an equal counterpart in rhs.
    for(const auto& [lhs_key, lhs_value]: lhs) {
        if constexpr(findable_in<decltype(lhs_key), R>) {
            // Fast path: use associative lookup by key when available.
            auto it = rhs.find(lhs_key);
            if(it == rhs.end()) {
                return false;
            }
            if(!compare_eq(lhs_value, it->second)) {
                return false;
            }
        } else {
            // Fallback for map-like ranges without find(): linear scan by key/value equality.
            auto it = std::ranges::find_if(rhs, [&](const auto& entry) {
                const auto& [rhs_key, rhs_value] = entry;
                return compare_eq(lhs_key, rhs_key) && compare_eq(lhs_value, rhs_value);
            });
            if(it == std::ranges::end(rhs)) {
                return false;
            }
        }
    }
    return true;
}

template <typename L, typename R>
constexpr bool compare_map_eq_unordered(const L& lhs, const R& rhs) {
    if constexpr(std::ranges::sized_range<const L> && std::ranges::sized_range<const R>) {
        // If sizes are known and equal, one-way containment implies equality.
        if(std::ranges::size(lhs) != std::ranges::size(rhs)) {
            return false;
        }
        return map_entries_contained_in(lhs, rhs);
    } else {
        // Without reliable sizes, require containment in both directions.
        return map_entries_contained_in(lhs, rhs) && map_entries_contained_in(rhs, lhs);
    }
}

template <typename L, typename R>
constexpr bool compare_sequence_eq(const L& lhs, const R& rhs) {
    return compare_range_eq(lhs, rhs, [](const auto& l, const auto& r) {
        return compare_eq(l, r);
    });
}

template <typename L, typename R>
constexpr bool compare_sequence_lt(const L& lhs, const R& rhs) {
    return compare_range_lt(lhs, rhs, [](const auto& l, const auto& r) {
        return compare_lt(l, r);
    });
}

template <typename L, typename R>
constexpr bool compare_map_entry_eq(const L& lhs, const R& rhs) {
    const auto& [lhs_key, lhs_value] = lhs;
    const auto& [rhs_key, rhs_value] = rhs;
    return compare_eq(lhs_key, rhs_key) && compare_eq(lhs_value, rhs_value);
}

template <typename L, typename R>
constexpr bool compare_map_entry_lt(const L& lhs, const R& rhs) {
    const auto& [lhs_key, lhs_value] = lhs;
    const auto& [rhs_key, rhs_value] = rhs;

    if(compare_lt(lhs_key, rhs_key)) {
        return true;
    }
    if(compare_lt(rhs_key, lhs_key)) {
        return false;
    }
    return compare_lt(lhs_value, rhs_value);
}

template <typename L, typename R>
constexpr bool compare_map_eq(const L& lhs, const R& rhs) {
    return compare_range_eq(lhs, rhs, [](const auto& l, const auto& r) {
        return compare_map_entry_eq(l, r);
    });
}

template <typename L, typename R>
constexpr bool compare_map_lt(const L& lhs, const R& rhs) {
    return compare_range_lt(lhs, rhs, [](const auto& l, const auto& r) {
        return compare_map_entry_lt(l, r);
    });
}

template <typename L, typename R>
constexpr bool set_entries_contained_in(const L& lhs, const R& rhs) {
    // Checks whether every element in lhs exists in rhs.
    for(const auto& l: lhs) {
        if constexpr(findable_in<decltype(l), R>) {
            // Fast path: use associative lookup when available.
            if(rhs.find(l) == rhs.end()) {
                return false;
            }
        } else {
            // Fallback for set-like ranges without find(): linear scan by equality.
            auto it = std::ranges::find_if(rhs, [&](const auto& r) { return compare_eq(l, r); });
            if(it == std::ranges::end(rhs)) {
                return false;
            }
        }
    }
    return true;
}

template <typename L, typename R>
constexpr bool compare_set_eq_unordered(const L& lhs, const R& rhs) {
    if constexpr(std::ranges::sized_range<const L> && std::ranges::sized_range<const R>) {
        // If sizes are known and equal, one-way containment implies equality.
        if(std::ranges::size(lhs) != std::ranges::size(rhs)) {
            return false;
        }
        return set_entries_contained_in(lhs, rhs);
    } else {
        // Without reliable sizes, require containment in both directions.
        return set_entries_contained_in(lhs, rhs) && set_entries_contained_in(rhs, lhs);
    }
}

template <typename L, typename R>
constexpr bool compare_eq(const L& lhs, const R& rhs) {
    if constexpr(is_expected_v<L> && !is_expected_v<R>) {
        if(!lhs.has_value()) {
            return false;
        }
        return compare_eq(*lhs, rhs);
    } else if constexpr(!is_expected_v<L> && is_expected_v<R>) {
        if(!rhs.has_value()) {
            return false;
        }
        return compare_eq(lhs, *rhs);
    } else if constexpr(takeover_range_eq<L, R>) {
        if constexpr(ordered_map_range<L> && ordered_map_range<R>) {
            return compare_map_eq(lhs, rhs);
        } else if constexpr(map_range<L> && map_range<R>) {
            return compare_map_eq_unordered(lhs, rhs);
        } else if constexpr((ordered_set_range<L> && ordered_set_range<R>) ||
                            (sequence_range<L> && sequence_range<R>)) {
            return compare_sequence_eq(lhs, rhs);
        } else if constexpr(set_range<L> && set_range<R>) {
            return compare_set_eq_unordered(lhs, rhs);
        } else {
            static_assert(dependent_false<L>,
                          "meta::eq: internal error: takeover range category not handled");
            return false;
        }
    } else if constexpr(eq_comparable_with<L, R>) {
        constexpr bool lhs_int_like = std::is_enum_v<L> || standard_integer<L>;
        constexpr bool rhs_int_like = std::is_enum_v<R> || standard_integer<R>;
        if constexpr(lhs_int_like && rhs_int_like) {
            auto to_integer = [](auto v) {
                using V = decltype(v);
                if constexpr(std::is_enum_v<V>) {
                    return static_cast<std::underlying_type_t<V>>(v);
                } else {
                    return v;
                }
            };
            auto li = to_integer(lhs);
            auto ri = to_integer(rhs);
            if constexpr(standard_integer<decltype(li)> && standard_integer<decltype(ri)>) {
                return std::cmp_equal(li, ri);
            } else {
                return static_cast<bool>(li == ri);
            }
        } else {
            return static_cast<bool>(lhs == rhs);
        }
    } else if constexpr(reflectable_pair<L, R>) {
        constexpr std::size_t lhs_count = reflection<L>::field_count;
        constexpr std::size_t rhs_count = reflection<R>::field_count;

        if constexpr(lhs_count != rhs_count) {
            return false;
        } else if constexpr(lhs_count == 0) {
            return true;
        } else {
            auto lhs_addrs = reflection<L>::field_addrs(lhs);
            auto rhs_addrs = reflection<R>::field_addrs(rhs);
            return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return (compare_eq(*std::get<Is>(lhs_addrs), *std::get<Is>(rhs_addrs)) && ...);
            }(std::make_index_sequence<lhs_count>{});
        }
    } else {
        static_assert(dependent_false<L>,
                      "meta::eq: operands are not comparable and not reflectable");
        return false;
    }
}

template <typename L, typename R>
constexpr bool compare_ne(const L& lhs, const R& rhs) {
    if constexpr(!takeover_range_ne<L, R> && ne_comparable_with<L, R>) {
        return static_cast<bool>(lhs != rhs);
    } else {
        return !compare_eq(lhs, rhs);
    }
}

template <typename L, typename R>
constexpr bool compare_lt(const L& lhs, const R& rhs) {
    if constexpr(takeover_range_lt<L, R>) {
        if constexpr(ordered_map_range<L> && ordered_map_range<R>) {
            return compare_map_lt(lhs, rhs);
        } else if constexpr(map_range<L> && map_range<R>) {
            static_assert(
                dependent_false<L>,
                "meta::lt: unordered/mixed map ranges have no deterministic strict order; " "use meta::eq/meta::ne");
            return false;
        } else if constexpr((ordered_set_range<L> && ordered_set_range<R>) ||
                            (sequence_range<L> && sequence_range<R>)) {
            return compare_sequence_lt(lhs, rhs);
        } else if constexpr(set_range<L> && set_range<R>) {
            static_assert(
                dependent_false<L>,
                "meta::lt: unordered/mixed set ranges have no deterministic strict order; " "use meta::eq/meta::ne");
            return false;
        } else {
            static_assert(dependent_false<L>,
                          "meta::lt: internal error: takeover range category not handled");
            return false;
        }
    } else if constexpr(lt_comparable_with<L, R>) {
        return static_cast<bool>(lhs < rhs);
    } else if constexpr(reflectable_pair<L, R>) {
        constexpr std::size_t lhs_count = reflection<L>::field_count;
        constexpr std::size_t rhs_count = reflection<R>::field_count;
        constexpr std::size_t common_count = lhs_count < rhs_count ? lhs_count : rhs_count;

        auto lhs_addrs = reflection<L>::field_addrs(lhs);
        auto rhs_addrs = reflection<R>::field_addrs(rhs);

        bool result = false;
        bool decided = false;

        if constexpr(common_count > 0) {
            decided = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                return (([&] {
                            const auto& l = *std::get<Is>(lhs_addrs);
                            const auto& r = *std::get<Is>(rhs_addrs);
                            if(compare_lt(l, r)) {
                                result = true;
                                return true;
                            }
                            if(compare_lt(r, l)) {
                                result = false;
                                return true;
                            }
                            return false;
                        }()) ||
                        ...);
            }(std::make_index_sequence<common_count>{});
        }

        if(decided) {
            return result;
        }
        if constexpr(lhs_count != rhs_count) {
            return lhs_count < rhs_count;
        }
        return false;
    } else {
        static_assert(dependent_false<L>,
                      "meta::lt: operands are not comparable and not reflectable");
        return false;
    }
}

template <typename L, typename R>
constexpr bool compare_le(const L& lhs, const R& rhs) {
    // For a strict-weak-order comparator (<), `lhs <= rhs` is equivalent to `!(rhs < lhs)`.
    // This is also equivalent to `(lhs < rhs) || (lhs == rhs)`.
    if constexpr(!takeover_range_le<L, R> && le_comparable_with<L, R>) {
        return static_cast<bool>(lhs <= rhs);
    } else {
        return !compare_lt(rhs, lhs);
    }
}

template <typename L, typename R>
constexpr bool compare_gt(const L& lhs, const R& rhs) {
    if constexpr(!takeover_range_gt<L, R> && gt_comparable_with<L, R>) {
        return static_cast<bool>(lhs > rhs);
    } else {
        return compare_lt(rhs, lhs);
    }
}

template <typename L, typename R>
constexpr bool compare_ge(const L& lhs, const R& rhs) {
    // Symmetric to <= : `lhs >= rhs` is `!(lhs < rhs)` under strict-weak-order semantics.
    if constexpr(!takeover_range_ge<L, R> && ge_comparable_with<L, R>) {
        return static_cast<bool>(lhs >= rhs);
    } else {
        return !compare_lt(lhs, rhs);
    }
}

}  // namespace kota::meta::detail

namespace kota::meta {

struct eq_t {
    using is_transparent = void;

    template <typename L, typename R>
    constexpr bool operator()(const L& lhs, const R& rhs) const {
        return detail::compare_eq(lhs, rhs);
    }
};

struct ne_t {
    using is_transparent = void;

    template <typename L, typename R>
    constexpr bool operator()(const L& lhs, const R& rhs) const {
        return detail::compare_ne(lhs, rhs);
    }
};

struct lt_t {
    using is_transparent = void;

    template <typename L, typename R>
    constexpr bool operator()(const L& lhs, const R& rhs) const {
        return detail::compare_lt(lhs, rhs);
    }
};

struct le_t {
    using is_transparent = void;

    template <typename L, typename R>
    constexpr bool operator()(const L& lhs, const R& rhs) const {
        return detail::compare_le(lhs, rhs);
    }
};

struct gt_t {
    using is_transparent = void;

    template <typename L, typename R>
    constexpr bool operator()(const L& lhs, const R& rhs) const {
        return detail::compare_gt(lhs, rhs);
    }
};

struct ge_t {
    using is_transparent = void;

    template <typename L, typename R>
    constexpr bool operator()(const L& lhs, const R& rhs) const {
        return detail::compare_ge(lhs, rhs);
    }
};

constexpr inline eq_t eq;
constexpr inline ne_t ne;
constexpr inline lt_t lt;
constexpr inline le_t le;
constexpr inline gt_t gt;
constexpr inline ge_t ge;

}  // namespace kota::meta
