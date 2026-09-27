#pragma once

// Probing: how a keyed document picks an untagged variant's alternative. An
// exact-kind pass comes first, then a widening one, and the last alternative
// decodes on the real reader so its error surfaces; nested variants, nullable
// wrappers and reprs over variants take part at the outer pass's level.
// Backends that carry the alternative's index never probe.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/repr.h"
#include "codec/harness/visit/kit.h"
#include "fixtures/containers.h"
#include "fixtures/structs.h"
#include "fixtures/tagged.h"

namespace kota::test {

template <Backend B>
    requires (B::caps.self_describing)
void probing(const Kit<B>& kit) {
    reads_in_field<std::variant<bool, int>>(
        kit,
        "bool_input_picks_bool",
        [] { return true; },
        [] { return std::variant<bool, int>{true}; });
    reads_in_field<std::variant<bool, int>>(
        kit,
        "integer_input_skips_bool",
        [] { return 7; },
        [] { return std::variant<bool, int>{7}; });
    reads_in_field<std::variant<int, double>>(
        kit,
        "integer_input_picks_int_before_double",
        [] { return 42; },
        [] { return std::variant<int, double>{42}; });
    reads_in_field<std::variant<int, double>>(
        kit,
        "float_input_picks_double",
        [] { return 3.14; },
        [] { return std::variant<int, double>{3.14}; });
    reads_in_field<std::variant<double, int>>(
        kit,
        "integer_input_picks_int_after_double",
        [] { return 42; },
        [] { return std::variant<double, int>{42}; });
    reads_in_field<std::variant<double, std::string>>(
        kit,
        "integer_input_widens_to_double",
        [] { return 5; },
        [] { return std::variant<double, std::string>{5.0}; });
    reads_in_field<std::variant<std::string, double>>(
        kit,
        "integer_input_widens_to_last_double",
        [] { return 5; },
        [] { return std::variant<std::string, double>{5.0}; });
    reads_in_field<std::variant<float, std::string>>(
        kit,
        "integer_input_widens_to_float",
        [] { return 5; },
        [] { return std::variant<float, std::string>{5.0F}; });
    reads_in_field<std::variant<int>>(
        kit,
        "single_alternative_takes_its_kind",
        [] { return 99; },
        [] { return std::variant<int>{99}; });
    reads_in_field<std::variant<float, double>>(
        kit,
        "float_input_picks_first_float_kind",
        [] { return 1.5; },
        [] { return std::variant<float, double>{1.5F}; });
    reads_in_field<std::variant<std::int8_t, std::int16_t, std::int32_t, std::int64_t>>(
        kit,
        "integer_input_picks_first_width_that_fits",
        [] { return 40000; },
        [] {
            return std::variant<std::int8_t, std::int16_t, std::int32_t, std::int64_t>{
                std::int32_t{40000}};
        });
    reads_in_field<std::variant<std::int64_t, std::uint64_t>>(
        kit,
        "signed_input_picks_int64",
        [] { return 42; },
        [] { return std::variant<std::int64_t, std::uint64_t>{std::int64_t{42}}; });
    reads_in_field<std::variant<std::uint64_t, std::int64_t>>(
        kit,
        "negative_input_skips_uint64",
        [] { return -1; },
        [] { return std::variant<std::uint64_t, std::int64_t>{std::int64_t{-1}}; });
    if constexpr(B::caps.full_uint64) {
        reads_in_field<std::variant<std::int64_t, std::uint64_t>>(
            kit,
            "unsigned_input_picks_uint64",
            [] { return std::numeric_limits<std::uint64_t>::max(); },
            [] {
                return std::variant<std::int64_t, std::uint64_t>{
                    std::numeric_limits<std::uint64_t>::max()};
            });
    }
    reads_in_field<std::variant<std::string, int>>(
        kit,
        "text_input_picks_string",
        [] { return std::string("hello"); },
        [] { return std::variant<std::string, int>{"hello"}; });
    // The null alternatives come second, so a null that did not reach them
    // would leave the first one. A null input stands in a field, so these
    // cases need a backend that reads a null field back.
    if constexpr(B::caps.nested_nulls) {
        reads_in_field<std::variant<int, std::monostate, std::string>>(
            kit,
            "null_input_picks_monostate",
            [] { return nullptr; },
            [] { return std::variant<int, std::monostate, std::string>{std::monostate{}}; });
    }
    reads_in_field<std::variant<std::optional<double>, int>>(
        kit,
        "integer_input_skips_optional_double",
        [] { return 42; },
        [] { return std::variant<std::optional<double>, int>{42}; });
    if constexpr(B::caps.nested_nulls) {
        reads_in_field<std::variant<int, std::optional<double>>>(
            kit,
            "null_input_engages_optional",
            [] { return nullptr; },
            [] { return std::variant<int, std::optional<double>>{std::optional<double>{}}; });
    }
    reads_in_field<std::variant<std::optional<double>, int>>(
        kit,
        "float_input_reaches_optional_double",
        [] { return 3.14; },
        [] { return std::variant<std::optional<double>, int>{std::optional<double>{3.14}}; });
    reads_in_field<std::variant<int, std::vector<int>, Point2d>>(
        kit,
        "array_input_picks_sequence",
        [] { return std::vector<int>{1, 2}; },
        [] {
            return std::variant<int, std::vector<int>, Point2d>{
                std::vector<int>{1, 2}
            };
        });
    reads_in_field<std::variant<int, std::vector<int>, Point2d>>(
        kit,
        "object_input_picks_struct",
        [] { return Point2d{.x = 1, .y = 2}; },
        [] {
            return std::variant<int, std::vector<int>, Point2d>{
                Point2d{.x = 1, .y = 2}
            };
        });
    reads_in_field<std::variant<std::vector<int>, std::map<std::string, int>>>(
        kit,
        "object_input_picks_map",
        [] {
            return std::map<std::string, int>{
                {"a", 1}
            };
        },
        [] {
            return std::variant<std::vector<int>, std::map<std::string, int>>{
                std::map<std::string, int>{{"a", 1}}};
        });
    reads_in_field<std::variant<std::vector<int>, std::string>>(
        kit,
        "empty_array_picks_sequence",
        [] { return std::vector<int>{}; },
        [] { return std::variant<std::vector<int>, std::string>{std::vector<int>{}}; });
    reads_in_field<std::variant<Point2d, std::map<std::string, int>>>(
        kit,
        "empty_object_skips_struct_with_required_fields",
        [] { return Empty{}; },
        [] {
            return std::variant<Point2d, std::map<std::string, int>>{std::map<std::string, int>{}};
        });
    reads_in_field<std::variant<Point2d, std::map<std::string, double>>>(
        kit,
        "object_with_its_fields_picks_struct_over_map",
        [] { return Point2d{.x = 1, .y = 2}; },
        [] {
            return std::variant<Point2d, std::map<std::string, double>>{
                Point2d{.x = 1, .y = 2}
            };
        });
    reads_in_field<std::variant<Point2d, std::map<std::string, double>>>(
        kit,
        "object_without_its_fields_picks_map",
        [] {
            return std::map<std::string, double>{
                {"foo", 3.0}
            };
        },
        [] {
            return std::variant<Point2d, std::map<std::string, double>>{
                std::map<std::string, double>{{"foo", 3.0}}};
        });
    reads_in_field<std::variant<Color3, Point2d>>(
        kit,
        "struct_fields_pick_struct",
        [] { return Point2d{.x = 1, .y = 2}; },
        [] {
            return std::variant<Color3, Point2d>{
                Point2d{.x = 1, .y = 2}
            };
        });
    reads_in_field<std::variant<Field<int>, Field<std::string>>>(
        kit,
        "struct_field_kinds_pick_struct",
        [] { return Field<std::string>{"hello"}; },
        [] { return std::variant<Field<int>, Field<std::string>>{Field<std::string>{"hello"}}; });
    reads_in_field<
        std::variant<Field<std::variant<int, std::string>>, Field<std::variant<bool, double>>>>(
        kit,
        "struct_field_variants_pick_struct",
        [] { return Field<bool>{true}; },
        [] {
            return std::variant<Field<std::variant<int, std::string>>,
                                Field<std::variant<bool, double>>>{
                Field<std::variant<bool, double>>{true}};
        });
    reads_in_field<std::variant<Field<int>, Field<std::int64_t>>>(
        kit,
        "equal_structs_pick_first",
        [] { return Field<int>{1}; },
        [] { return std::variant<Field<int>, Field<std::int64_t>>{Field<int>{1}}; });
    reads_in_field<std::variant<TwoKeys, OneKey>>(
        kit,
        "missing_required_field_skips_struct",
        [] { return OneKey{.a = 1}; },
        [] { return std::variant<TwoKeys, OneKey>{OneKey{.a = 1}}; });
    reads_in_field<std::variant<TwoKeys, OneKey>>(
        kit,
        "all_required_fields_pick_struct",
        [] { return TwoKeys{.a = 1, .b = 2}; },
        [] {
            return std::variant<TwoKeys, OneKey>{
                TwoKeys{.a = 1, .b = 2}
            };
        });
    reads_in_field<std::variant<OneKeyMaybeTwo, OneKey>>(
        kit,
        "missing_optional_field_keeps_struct",
        [] { return OneKey{.a = 1}; },
        [] {
            return std::variant<OneKeyMaybeTwo, OneKey>{
                OneKeyMaybeTwo{.a = 1, .b = std::nullopt}
            };
        });
    reads_in_field<std::variant<std::map<std::string, int>, std::map<std::string, std::string>>>(
        kit,
        "map_value_kinds_pick_map",
        [] {
            return std::map<std::string, std::string>{
                {"a", "x"}
            };
        },
        [] {
            return std::variant<std::map<std::string, int>, std::map<std::string, std::string>>{
                std::map<std::string, std::string>{{"a", "x"}}};
        });
    reads_in_field<std::variant<std::tuple<int, std::string>, std::vector<int>>>(
        kit,
        "array_length_picks_tuple",
        [] { return std::tuple<int, std::string>{42, "hello"}; },
        [] {
            return std::variant<std::tuple<int, std::string>, std::vector<int>>{
                std::tuple<int, std::string>{42, "hello"}
            };
        });
    reads_in_field<std::variant<std::tuple<int, std::string>, std::vector<int>>>(
        kit,
        "array_length_skips_tuple",
        [] { return std::vector<int>{1, 2, 3}; },
        [] {
            return std::variant<std::tuple<int, std::string>, std::vector<int>>{
                std::vector<int>{1, 2, 3}
            };
        });

    // A nested variant is as compatible as its alternatives, and an outer
    // exact pass never lets it widen.
    using Inner = std::variant<int, std::string>;
    reads_in_field<std::variant<Inner, double>>(
        kit,
        "integer_input_reaches_nested_int",
        [] { return 42; },
        [] { return std::variant<Inner, double>{Inner{42}}; });
    reads_in_field<std::variant<Inner, double>>(
        kit,
        "float_input_skips_nested_variant",
        [] { return 3.14; },
        [] { return std::variant<Inner, double>{3.14}; });
    reads_in_field<std::variant<Inner, bool>>(
        kit,
        "bool_input_falls_through_nested_variant",
        [] { return true; },
        [] { return std::variant<Inner, bool>{true}; });
    reads_in_field<std::variant<std::variant<double, std::string>, int>>(
        kit,
        "integer_input_skips_nested_double",
        [] { return 42; },
        [] { return std::variant<std::variant<double, std::string>, int>{42}; });
    reads_in_field<std::variant<std::variant<double, int>, int>>(
        kit,
        "nested_exact_match_ties_with_outer",
        [] { return 42; },
        [] { return std::variant<std::variant<double, int>, int>{std::variant<double, int>{42}}; });
    using Narrow = std::variant<std::int8_t, double>;
    reads_in_field<std::variant<Narrow, std::int64_t>>(
        kit,
        "nested_widening_defers_to_outer_exact_match",
        [] { return 1000; },
        [] { return std::variant<Narrow, std::int64_t>{std::int64_t{1000}}; });
    reads_in_field<std::variant<Narrow, std::int64_t>>(
        kit,
        "nested_exact_match_wins",
        [] { return 7; },
        [] { return std::variant<Narrow, std::int64_t>{Narrow{std::int8_t{7}}}; });
    reads_in_field<std::variant<Narrow, std::string>>(
        kit,
        "nested_widening_reachable_without_outer_match",
        [] { return 1000; },
        [] { return std::variant<Narrow, std::string>{Narrow{1000.0}}; });
    reads_in_field<std::variant<std::optional<Narrow>, std::int64_t>>(
        kit,
        "optional_nested_widening_defers_to_outer_exact_match",
        [] { return 1000; },
        [] { return std::variant<std::optional<Narrow>, std::int64_t>{std::int64_t{1000}}; });
    using PointerToNarrow = std::unique_ptr<Narrow>;
    reads<PointerOr<PointerToNarrow, std::int64_t>>(
        kit,
        "pointer_nested_widening_defers_to_outer_exact_match",
        [] { return Field<int>{1000}; },
        [] { return PointerOr<PointerToNarrow, std::int64_t>{std::int64_t{1000}}; });
    reads<PointerOr<PointerToNarrow, std::string>>(
        kit,
        "pointer_nested_widening_reachable_without_outer_match",
        [] { return Field<int>{1000}; },
        [] { return PointerOr<PointerToNarrow, std::string>{std::make_unique<Narrow>(1000.0)}; });
    reads_in_field<std::variant<std::optional<Inner>, bool>>(
        kit,
        "optional_nested_variant_takes_its_kinds",
        [] { return 42; },
        [] { return std::variant<std::optional<Inner>, bool>{Inner{42}}; });
    using SharedInner = std::shared_ptr<Inner>;
    reads<PointerOr<SharedInner, bool>>(
        kit,
        "pointer_nested_variant_takes_its_kinds",
        [] { return Field<std::string>{"hello"}; },
        [] { return PointerOr<SharedInner, bool>{std::make_shared<Inner>("hello")}; });
    if constexpr(B::caps.nested_nulls) {
        reads<PointerOr<SharedInner, bool>>(
            kit,
            "null_input_engages_pointer",
            [] { return Field<std::nullptr_t>{}; },
            [] { return PointerOr<SharedInner, bool>{SharedInner{}}; });
    }
    reads_in_field<std::variant<BoxedScalar, std::int64_t>>(
        kit,
        "repr_nested_widening_defers_to_outer_exact_match",
        [] { return 1000; },
        [] { return std::variant<BoxedScalar, std::int64_t>{std::int64_t{1000}}; });
    reads_in_field<std::variant<BoxedScalar, std::int64_t>>(
        kit,
        "repr_nested_exact_match_wins",
        [] { return 7; },
        [] { return std::variant<BoxedScalar, std::int64_t>{BoxedScalar{.v = std::int8_t{7}}}; });
    reads_in_field<std::variant<BoxedScalar, std::string>>(
        kit,
        "repr_nested_widening_reachable_without_outer_match",
        [] { return 1000; },
        [] { return std::variant<BoxedScalar, std::string>{BoxedScalar{.v = 1000.0}}; });
    reads_in_field<std::variant<std::optional<BoxedScalar>, std::int64_t>>(
        kit,
        "optional_repr_nested_widening_defers_to_outer_exact_match",
        [] { return 1000; },
        [] { return std::variant<std::optional<BoxedScalar>, std::int64_t>{std::int64_t{1000}}; });
    using Triple = std::variant<std::variant<std::variant<bool, std::string>, int>, double>;
    reads_in_field<Triple>(
        kit,
        "bool_input_reaches_innermost_variant",
        [] { return true; },
        [] {
            return Triple{std::variant<std::variant<bool, std::string>, int>{
                std::variant<bool, std::string>{true}}};
        });

    // A tagged nested variant is judged by its object shape.
    reads_in_field<std::variant<ExternalShape, int>>(
        kit,
        "object_input_reaches_external_nested_variant",
        [] {
            return std::map<std::string, int>{
                {"number", 1}
            };
        },
        [] { return std::variant<ExternalShape, int>{ExternalShape{1}}; });
    reads_in_field<std::variant<ExternalShape, int>>(
        kit,
        "scalar_input_skips_external_nested_variant",
        [] { return 7; },
        [] { return std::variant<ExternalShape, int>{7}; });
    reads_in_field<std::variant<AdjacentShape, std::string>>(
        kit,
        "object_input_reaches_adjacent_nested_variant",
        [] { return AdjacentPlain<int>{.t = "number", .c = 1}; },
        [] { return std::variant<AdjacentShape, std::string>{AdjacentShape{1}}; });
    reads_in_field<std::variant<ExternalShape, bool>, NotHumanReadableConfig>(
        kit,
        "not_human_readable_probes_tagged_by_alternatives",
        [] { return 1; },
        [] { return std::variant<ExternalShape, bool>{ExternalShape{1}}; });

    read_in_field_fails<std::variant<int, std::string>>(kit,
                                                        "untagged_no_match_fails",
                                                        [] { return std::vector<int>{1}; },
                                                        {.message = "", .path = "value"});
    read_in_field_fails<std::variant<int, std::string>>(kit,
                                                        "untagged_object_no_match_fails",
                                                        [] { return Point{.x = 1, .y = 2}; },
                                                        {.message = "", .path = "value"});
    if constexpr(B::caps.nested_nulls) {
        read_in_field_fails<std::variant<int, std::string>>(kit,
                                                            "untagged_null_no_match_fails",
                                                            [] { return nullptr; },
                                                            {.message = "", .path = "value"});
    }
    read_in_field_fails<std::variant<Rect, Circle>>(
        kit,
        "untagged_no_struct_match_fails",
        [] {
            return std::map<std::string, double>{
                {"width", 5}
            };
        },
        {.message = "missing required field 'radius'", .path = "value"});
}

}  // namespace kota::test
