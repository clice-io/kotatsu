#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "fixtures/schema/common.h"
#include "fixtures/schema/primitives.h"
#include "fixtures/schema/tagged.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota_variant_repr_test {

// An alternative whose declared representation is itself an untagged
// variant: the outer pass level must flow through the repr into it.
struct boxed_scalar {
    std::variant<std::int8_t, double> v;

    auto operator==(const boxed_scalar&) const -> bool = default;
};

}  // namespace kota_variant_repr_test

namespace kota::meta {

template <>
struct repr<kota_variant_repr_test::boxed_scalar> {
    using type = std::variant<std::int8_t, double>;

    static type to(const kota_variant_repr_test::boxed_scalar& b) {
        return b.v;
    }

    static kota_variant_repr_test::boxed_scalar from(type v) {
        return {.v = std::move(v)};
    }
};

}  // namespace kota::meta

namespace kota::codec {

using namespace meta;

namespace {

using json::from_string;
using json::to_string;

using Point = meta::fixtures::Point2d;
using Color = meta::fixtures::Color3;
using IntHolder = meta::fixtures::IntHolder;
using StringHolder = meta::fixtures::StringHolder;

KOTATSU_ANNOTATION(ext_simple_annotation, tagged = true, tag_names = {"num", "str"});
using ExtSimple = annotate<ext_simple_annotation>::type<std::variant<int, std::string>>;

KOTATSU_ANNOTATION(ext_with_mono_annotation, tagged = true, tag_names = {"none", "num", "str"});
using ExtWithMono =
    annotate<ext_with_mono_annotation>::type<std::variant<std::monostate, int, std::string>>;

KOTATSU_ANNOTATION(ext_with_struct_annotation,
                   tagged = true,
                   tag_names = {"int", "point", "color"});
using ExtWithStruct = annotate<ext_with_struct_annotation>::type<std::variant<int, Point, Color>>;

KOTATSU_ANNOTATION(adj_simple_annotation, tag = "t", content = "v", tag_names = {"num", "str"});
using AdjSimple = annotate<adj_simple_annotation>::type<std::variant<int, std::string>>;

KOTATSU_ANNOTATION(adj_with_mono_annotation,
                   tag = "tag",
                   content = "data",
                   tag_names = {"nil", "num", "str"});
using AdjWithMono =
    annotate<adj_with_mono_annotation>::type<std::variant<std::monostate, int, std::string>>;

KOTATSU_ANNOTATION(adj_with_struct_annotation,
                   tag = "type",
                   content = "value",
                   tag_names = {"int", "point"});
using AdjWithStruct = annotate<adj_with_struct_annotation>::type<std::variant<int, Point>>;

using Circle = meta::fixtures::Circle;
using Rect = meta::fixtures::Rect;
using Triangle = meta::fixtures::Triangle;

KOTATSU_ANNOTATION(int_tag_shape_annotation, tag = "type", tag_names = {"circle", "rect"});
using IntTagShape = annotate<int_tag_shape_annotation>::type<std::variant<Circle, Rect>>;

KOTATSU_ANNOTATION(int_tag_tri_shape_annotation,
                   tag = "kind",
                   tag_names = {"circle", "rect", "triangle"});
using IntTagTriShape =
    annotate<int_tag_tri_shape_annotation>::type<std::variant<Circle, Rect, Triangle>>;

struct non_hr_config {
    constexpr static bool human_readable = false;
};

struct ExtHolder {
    std::string label;
    ExtWithStruct item;
};

struct AdjHolder {
    std::string name;
    AdjSimple data;
};

struct IntTagHolder {
    std::string name;
    IntTagShape shape;
};

ZEST_SUITE(serde_variant_untagged) {

ZEST_CASE(bool_vs_int) {
    using V = std::variant<bool, int>;

    V v_bool = true;
    ASSERT(to_string(v_bool) == "true");

    V v_int = 42;
    ASSERT(to_string(v_int) == "42");

    // bool JSON → bool alternative (not int)
    V out{};
    ASSERT(from_string("true", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<bool>(out) == true);

    ASSERT(from_string("false", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<bool>(out) == false);

    // integer JSON → int alternative (not bool)
    ASSERT(from_string("7", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<int>(out) == 7);
}

ZEST_CASE(int_before_double) {
    // When int comes before double, integer JSON should match int (first match wins)
    using V = std::variant<int, double>;

    V out{};
    ASSERT(from_string("42", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<int>(out) == 42);

    // Floating-point JSON can only match double
    ASSERT(from_string("3.14", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<double>(out) == 3.14);
}

ZEST_CASE(double_before_int) {
    // Even when double comes first, integer JSON matches int (more precise kind match).
    // Floating-point JSON still matches double.
    using V = std::variant<double, int>;

    V out{};
    ASSERT(from_string("42", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<int>(out) == 42);

    ASSERT(from_string("3.14", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<double>(out) == 3.14);
}

ZEST_CASE(double_from_integer_input) {
    // No integer alternative exists: the widening pass lets the double
    // alternative claim integer input instead of failing outright.
    using V = std::variant<double, std::string>;

    V out{};
    ASSERT(from_string("5", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<double>(out) == 5.0);

    ASSERT(from_string("3.14", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<double>(out) == 3.14);

    ASSERT(from_string(R"("x")", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::string>(out) == "x");
}

ZEST_CASE(widening_when_float_family_is_last) {
    // The widen pass claims the value through a fork even when the float
    // alternative is last (previously only the unconditional fallback did).
    using V = std::variant<std::string, double>;
    V out;
    ASSERT(from_string("5", out).has_value());
    ASSERT(out.index() == 1U);
    EXPECT(std::get<double>(out) == 5.0);

    ASSERT(from_string(R"("x")", out).has_value());
    ASSERT(out.index() == 0U);
}

ZEST_CASE(float32_alternative_from_integer_input) {
    using V = std::variant<float, std::string>;
    V out;
    ASSERT(from_string("5", out).has_value());
    ASSERT(out.index() == 0U);
    EXPECT(std::get<float>(out) == 5.0F);
}

ZEST_CASE(optional_wrapper_before_int) {
    // A nullable wrapper is judged by its wrapped type: integer input lands
    // on the exact int alternative, not the earlier optional<double>. Null
    // still engages the wrapper itself.
    using V = std::variant<std::optional<double>, int>;

    V out{};
    ASSERT(from_string("42", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<int>(out) == 42);

    ASSERT(from_string("3.14", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::optional<double>>(out) == 3.14);

    ASSERT(from_string("null", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(!std::get<std::optional<double>>(out).has_value());
}

ZEST_CASE(nested_variant_before_int) {
    // A nested variant is as compatible as its alternatives: integer input
    // skips variant<double, string> in the exact pass and lands on int.
    using V = std::variant<std::variant<double, std::string>, int>;

    V out{};
    ASSERT(from_string("42", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<int>(out) == 42);

    ASSERT(from_string("3.14", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<double>(std::get<0>(out)) == 3.14);

    ASSERT(from_string(R"("x")", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::string>(std::get<0>(out)) == "x");
}

ZEST_CASE(nested_widening_defers_to_outer_exact_match) {
    // The exact pass admits the nested variant through its int8_t branch;
    // when that narrowing fails, the nested double must not widen ahead of
    // the outer int64_t, which matches the input exactly.
    using V = std::variant<std::variant<std::int8_t, double>, std::int64_t>;

    V out{};
    ASSERT(from_string("1000", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::int64_t>(out) == 1000);

    // Values that fit int8_t still land on the nested exact branch.
    ASSERT(from_string("7", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::int8_t>(std::get<0>(out)) == 7);
}

ZEST_CASE(wrapped_nested_widening_defers_to_outer_exact_match) {
    // The outer pass level flows through nullable wrappers: the double nested
    // under optional must not widen ahead of the outer int64_t, while null
    // still engages the wrapper itself.
    using V = std::variant<std::optional<std::variant<std::int8_t, double>>, std::int64_t>;

    V out{};
    ASSERT(from_string("1000", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::int64_t>(out) == 1000);

    ASSERT(from_string("7", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::int8_t>(*std::get<0>(out)) == 7);

    ASSERT(from_string("null", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(!std::get<0>(out).has_value());
}

ZEST_CASE(pointer_wrapped_nested_widening_defers_to_outer_exact_match) {
    // Same pass propagation through a smart-pointer wrapper, and the widen
    // pass still reaches the wrapped double when no exact match remains.
    using nested_t = std::variant<std::int8_t, double>;
    using V = std::variant<std::unique_ptr<nested_t>, std::int64_t>;

    V out{};
    ASSERT(from_string("1000", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::int64_t>(out) == 1000);

    using W = std::variant<std::unique_ptr<nested_t>, std::string>;
    W wide{};
    ASSERT(from_string("1000", wide).has_value());
    EXPECT(wide.index() == 0U);
    EXPECT(std::get<double>(*std::get<0>(wide)) == 1000.0);
}

ZEST_CASE(repr_nested_widening_defers_to_outer_exact_match) {
    // An alternative whose meta::repr declares an untagged variant re-enters
    // the outer pass level like a bare nested variant: its double must not
    // widen ahead of the outer int64_t, which matches the input exactly.
    using Box = kota_variant_repr_test::boxed_scalar;
    using V = std::variant<Box, std::int64_t>;

    V out{};
    ASSERT(from_string("1000", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::int64_t>(out) == 1000);

    // Values that fit int8_t still land on the repr's exact branch.
    ASSERT(from_string("7", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::int8_t>(std::get<Box>(out).v) == 7);

    // With no outer exact alternative left, the widen pass still reaches the
    // repr's double.
    using W = std::variant<Box, std::string>;
    W wide{};
    ASSERT(from_string("1000", wide).has_value());
    EXPECT(wide.index() == 0U);
    EXPECT(std::get<double>(std::get<Box>(wide).v) == 1000.0);
}

ZEST_CASE(wrapped_repr_nested_widening_defers_to_outer_exact_match) {
    // The pass level flows through nullable wrappers into the repr chain,
    // while null still engages the wrapper itself.
    using Box = kota_variant_repr_test::boxed_scalar;
    using V = std::variant<std::optional<Box>, std::int64_t>;

    V out{};
    ASSERT(from_string("1000", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::int64_t>(out) == 1000);

    ASSERT(from_string("7", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::int8_t>(std::get<0>(out)->v) == 7);

    ASSERT(from_string("null", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(!std::get<0>(out).has_value());
}

ZEST_CASE(nested_widening_still_reachable_in_widen_pass) {
    // With no outer exact alternative left, the widen pass re-probes the
    // nested variant and its double claims the value the int8_t rejected.
    using V = std::variant<std::variant<std::int8_t, double>, std::string>;

    V out{};
    ASSERT(from_string("1000", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<double>(std::get<0>(out)) == 1000.0);
}

ZEST_CASE(tagged_nested_variant_keeps_object_shape) {
    // A tagged nested variant is classified by its object document shape,
    // not by its scalar alternatives: {"num":1} engages ExtSimple's tagged
    // decoder, while a bare scalar skips it and lands on int.
    using V = std::variant<ExtSimple, int>;

    V out{};
    ASSERT(from_string(R"({"num":1})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<int>(std::get<ExtSimple>(out)) == 1);

    ASSERT(from_string("7", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<int>(out) == 7);
}

ZEST_CASE(non_human_readable_config_ignores_tagging_in_probe) {
    // A non-human-readable config skips the tagged decoders and reads the
    // underlying variant directly, so the kind probe must classify ExtSimple
    // by its alternatives' kinds rather than as an object.
    using V = std::variant<ExtSimple, bool>;

    V v = ExtSimple{1};
    auto encoded = to_string<non_hr_config>(v);
    ASSERT(encoded);
    EXPECT(*encoded == "1");

    V out{};
    ASSERT(from_string<non_hr_config>("1", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<int>(std::get<ExtSimple>(out)) == 1);

    ASSERT(from_string<non_hr_config>("true", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<bool>(out) == true);
}

ZEST_CASE(custom_decoder_alternative_probed_for_any_kind) {
    // RawValue decodes through a deserialize_visit override that accepts any
    // JSON value, so the kind probe must not judge it by its declared struct
    // shape — even when it is neither first nor last.
    using V = std::variant<int, RawValue, bool>;

    V out{};
    ASSERT(from_string(R"("text")", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<RawValue>(out).data == R"("text")");

    ASSERT(from_string("7", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<int>(out) == 7);
}

ZEST_CASE(custom_decoder_wrapped_in_optional_probed_for_any_kind) {
    // The wrapper recursion must surface the wrapped type's dispatch
    // override, not its declared shape: a string still reaches
    // optional<RawValue> instead of falling through to int and failing.
    using V = std::variant<std::optional<RawValue>, int>;

    V out{};
    ASSERT(from_string(R"("text")", out).has_value());
    EXPECT(out.index() == 0U);
    auto& opt = std::get<std::optional<RawValue>>(out);
    ASSERT(opt);
    EXPECT(opt->data == R"("text")");

    ASSERT(from_string("null", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(!std::get<std::optional<RawValue>>(out).has_value());
}

ZEST_CASE(adjacently_tagged_nested_variant_keeps_object_shape) {
    using V = std::variant<AdjSimple, std::string>;

    V out{};
    ASSERT(from_string(R"({"t":"str","v":"inner"})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::string>(std::get<AdjSimple>(out)) == "inner");

    ASSERT(from_string(R"("plain")", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::string>(out) == "plain");
}

ZEST_CASE(monostate_matches_null) {
    using V = std::variant<std::monostate, int, std::string>;

    V out = 42;  // start non-null
    ASSERT(from_string("null", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::holds_alternative<std::monostate>(out));
}

ZEST_CASE(string_vs_int) {
    using V = std::variant<std::string, int>;

    V out{};
    ASSERT(from_string(R"("hello")", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::string>(out) == "hello");

    ASSERT(from_string("99", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<int>(out) == 99);
}

ZEST_CASE(array_vs_object) {
    using V = std::variant<std::vector<int>, std::map<std::string, int>>;

    V out{};
    ASSERT(from_string("[1,2,3]", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::vector<int>>(out) == std::vector<int>({1, 2, 3}));

    ASSERT(from_string(R"({"a":1,"b":2})", out).has_value());
    EXPECT(out.index() == 1U);
    auto& m = std::get<std::map<std::string, int>>(out);
    EXPECT(m.size() == 2U);
    EXPECT(m["a"] == 1);
    EXPECT(m["b"] == 2);
}

ZEST_CASE(scalar_vs_array_vs_object) {
    using V = std::variant<int, std::vector<int>, Point>;

    V out{};
    ASSERT(from_string("5", out).has_value());
    EXPECT(out.index() == 0U);

    ASSERT(from_string("[1,2]", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::vector<int>>(out) == std::vector<int>({1, 2}));

    ASSERT(from_string(R"({"x":1.0,"y":2.0})", out).has_value());
    EXPECT(out.index() == 2U);
    EXPECT(std::get<Point>(out) == (Point{1.0, 2.0}));
}

ZEST_CASE(single_alternative) {
    using V = std::variant<int>;

    V v = 42;
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == "42");

    V out{};
    ASSERT(from_string("99", out).has_value());
    EXPECT(std::get<int>(out) == 99);
}

ZEST_CASE(struct_deep_scoring) {
    // Two struct types with the same field name but different field types.
    // Deep scoring matches the correct alternative by recursively comparing field types.
    using V = std::variant<IntHolder, StringHolder>;

    V out{};
    // Deep scoring: "hello" is string → StringHolder.value (string) scores higher than
    // IntHolder.value (int)
    ASSERT(from_string(R"({"value":"hello"})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<StringHolder>(out).value == "hello");

    // Deep scoring: 42 is int → IntHolder.value (int) scores higher than
    // StringHolder.value (string)
    ASSERT(from_string(R"({"value":42})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<IntHolder>(out).value == 42);
}

ZEST_CASE(no_matching_type_fails) {
    using V = std::variant<int, std::string>;

    V out{};
    // JSON array doesn't match int or string
    EXPECT(!from_string("[1]", out).has_value());

    // JSON object doesn't match int or string
    EXPECT(!from_string(R"({"a":1})", out).has_value());

    // JSON bool doesn't match int or string
    EXPECT(!from_string("true", out).has_value());

    // JSON null doesn't match int or string
    EXPECT(!from_string("null", out).has_value());
}

ZEST_CASE(variant_roundtrip_all_scalars) {
    using V = std::variant<std::monostate, bool, int, double, std::string>;

    auto check = [](V input) -> bool {
        auto encoded = to_string(input);
        if(!encoded.has_value())
            return false;
        V out{};
        auto status = from_string(*encoded, out);
        if(!status.has_value())
            return false;
        return out == input;
    };

    EXPECT(check(std::monostate{}));
    EXPECT(check(true));
    EXPECT(check(42));
    EXPECT(check(3.14));
    EXPECT(check(std::string("test")));
}

ZEST_CASE(nested_variant) {
    using Inner = std::variant<int, std::string>;
    using Outer = std::variant<Inner, double>;

    Outer out{};
    // integer → Inner accepts int, double accepts int; Inner is variant so recurse
    // Inner's int alternative matches exactly → selects Inner
    ASSERT(from_string("42", out).has_value());
    EXPECT(out.index() == 0U);
    auto& inner = std::get<Inner>(out);
    EXPECT(inner.index() == 0U);
    EXPECT(std::get<int>(inner) == 42);

    // string → only Inner accepts string
    ASSERT(from_string(R"("hello")", out).has_value());
    EXPECT(out.index() == 0U);
    auto& inner2 = std::get<Inner>(out);
    EXPECT(inner2.index() == 1U);
    EXPECT(std::get<std::string>(inner2) == "hello");

    // floating → double accepts float, Inner's int doesn't, Inner's string doesn't
    ASSERT(from_string("3.14", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<double>(out) == 3.14);
}

ZEST_CASE(nested_variant_no_match_falls_through) {
    using Inner = std::variant<int, std::string>;
    using Outer = std::variant<Inner, bool>;

    Outer out{};
    // bool → Inner doesn't accept bool (int doesn't, string doesn't), bool does
    ASSERT(from_string("true", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<bool>(out) == true);

    // object → neither Inner nor bool accepts object
    EXPECT(!from_string(R"({"x":1})", out).has_value());
}

ZEST_CASE(int64_vs_uint64) {
    using V = std::variant<std::int64_t, std::uint64_t>;

    V out{};
    // positive integer within int64 range → both accept, but numeric tiebreaker
    // should prefer the exact source kind
    ASSERT(from_string("42", out).has_value());
    // simdjson parses 42 as signed_integer → int64
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::int64_t>(out) == 42);

    // large unsigned → simdjson parses as unsigned_integer → uint64
    ASSERT(from_string("18446744073709551615", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::uint64_t>(out) == UINT64_MAX);
}

ZEST_CASE(uint64_before_int64) {
    using V = std::variant<std::uint64_t, std::int64_t>;

    V out{};
    // large unsigned → uint64
    ASSERT(from_string("18446744073709551615", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::uint64_t>(out) == UINT64_MAX);

    // negative → only int64 accepts
    ASSERT(from_string("-1", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::int64_t>(out) == -1);
}

ZEST_CASE(optional_variant) {
    using V = std::variant<int, std::string>;
    using OV = std::optional<V>;

    OV out{};
    ASSERT(from_string("null", out).has_value());
    EXPECT(!out);

    ASSERT(from_string("42", out).has_value());
    ASSERT(out);
    EXPECT(out->index() == 0U);
    EXPECT(std::get<int>(*out) == 42);

    ASSERT(from_string(R"("test")", out).has_value());
    ASSERT(out);
    EXPECT(out->index() == 1U);
    EXPECT(std::get<std::string>(*out) == "test");
}

};  // ZEST_SUITE(serde_variant_untagged)

ZEST_SUITE(serde_variant_ext) {

ZEST_CASE(roundtrip_all_alternatives) {
    {
        ExtWithStruct v = 42;
        auto encoded = to_string(v);
        ASSERT(encoded);
        EXPECT(*encoded == R"({"int":42})");

        ExtWithStruct out{};
        ASSERT(from_string(*encoded, out).has_value());
        EXPECT(std::get<int>(out) == 42);
    }
    {
        ExtWithStruct v = Point{.x = 1.5, .y = 2.5};
        auto encoded = to_string(v);
        ASSERT(encoded);
        EXPECT(*encoded == R"({"point":{"x":1.5,"y":2.5}})");

        ExtWithStruct out{};
        ASSERT(from_string(*encoded, out).has_value());
        EXPECT(std::get<Point>(out) == (Point{1.5, 2.5}));
    }
    {
        ExtWithStruct v = Color{.r = 255, .g = 128, .b = 0};
        auto encoded = to_string(v);
        ASSERT(encoded);
        EXPECT(*encoded == R"({"color":{"r":255,"g":128,"b":0}})");

        ExtWithStruct out{};
        ASSERT(from_string(*encoded, out).has_value());
        EXPECT(std::get<Color>(out) == (Color{255, 128, 0}));
    }
}

ZEST_CASE(monostate_roundtrip) {
    ExtWithMono v_none = std::monostate{};
    auto enc = to_string(v_none);
    ASSERT(enc);
    EXPECT(*enc == R"({"none":null})");

    ExtWithMono out = 42;
    ASSERT(from_string(*enc, out).has_value());
    EXPECT(std::holds_alternative<std::monostate>(out));

    ExtWithMono v_int = 7;
    enc = to_string(v_int);
    ASSERT(enc);
    EXPECT(*enc == R"({"num":7})");

    ASSERT(from_string(*enc, out).has_value());
    EXPECT(std::get<int>(out) == 7);
}

ZEST_CASE(unknown_tag_fails) {
    ExtSimple out{};
    EXPECT(!from_string(R"({"bad":42})", out).has_value());
}

ZEST_CASE(empty_object_fails) {
    ExtSimple out{};
    EXPECT(!from_string(R"({})", out).has_value());
}

ZEST_CASE(not_an_object_fails) {
    ExtSimple out{};
    EXPECT(!from_string("42", out).has_value());
    EXPECT(!from_string(R"("str")", out).has_value());
    EXPECT(!from_string("[1]", out).has_value());
    EXPECT(!from_string("null", out).has_value());
}

ZEST_CASE(in_holder_struct) {
    ExtHolder input{
        .label = "origin",
        .item = Point{.x = 0.0, .y = 0.0}
    };
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"label":"origin","item":{"point":{"x":0.0,"y":0.0}}})");

    ExtHolder out{};
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(out == input);
}

ZEST_CASE(in_vector) {
    std::vector<ExtSimple> vec = {ExtSimple{42}, ExtSimple{std::string("hi")}};
    auto encoded = to_string(vec);
    ASSERT(encoded);
    EXPECT(*encoded == R"([{"num":42},{"str":"hi"}])");

    std::vector<ExtSimple> out;
    ASSERT(from_string(*encoded, out).has_value());
    ASSERT(out.size() == 2U);
    EXPECT(std::get<int>(out[0]) == 42);
    EXPECT(std::get<std::string>(out[1]) == "hi");
}

ZEST_CASE(in_optional) {
    std::optional<ExtSimple> present = ExtSimple{std::string("val")};
    auto encoded = to_string(present);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"str":"val"})");

    std::optional<ExtSimple> out;
    ASSERT(from_string(*encoded, out).has_value());
    ASSERT(out);
    EXPECT(std::get<std::string>(*out) == "val");

    std::optional<ExtSimple> absent;
    encoded = to_string(absent);
    ASSERT(encoded);
    EXPECT(*encoded == "null");

    ASSERT(from_string("null", out).has_value());
    EXPECT(!out);
}

ZEST_CASE(empty_string_value) {
    ExtSimple v = std::string("");
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"str":""})");

    ExtSimple out{};
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(std::get<std::string>(out) == "");
}

};  // ZEST_SUITE(serde_variant_ext)

ZEST_SUITE(serde_variant_adj) {

ZEST_CASE(roundtrip_int) {
    AdjSimple v = 99;
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"t":"num","v":99})");

    AdjSimple out{};
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(std::get<int>(out) == 99);
}

ZEST_CASE(roundtrip_string) {
    AdjSimple v = std::string("abc");
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"t":"str","v":"abc"})");

    AdjSimple out{};
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(std::get<std::string>(out) == "abc");
}

ZEST_CASE(monostate) {
    AdjWithMono v = std::monostate{};
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"tag":"nil","data":null})");

    AdjWithMono out = 42;
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(std::holds_alternative<std::monostate>(out));
}

ZEST_CASE(content_before_tag) {
    // Value field appears before tag field — requires buffering
    AdjSimple out{};
    ASSERT(from_string(R"({"v":42,"t":"num"})", out).has_value());
    EXPECT(std::get<int>(out) == 42);

    ASSERT(from_string(R"({"v":"hello","t":"str"})", out).has_value());
    EXPECT(std::get<std::string>(out) == "hello");
}

ZEST_CASE(content_before_tag_struct) {
    // Struct content buffered before tag is known
    AdjWithStruct out{};
    ASSERT(from_string(R"({"value":{"x":1.0,"y":2.0},"type":"point"})", out).has_value());
    EXPECT(std::get<Point>(out) == (Point{1.0, 2.0}));
}

ZEST_CASE(extra_unknown_fields_ignored) {
    // Unknown fields alongside tag and content should be skipped
    AdjSimple out{};
    ASSERT(from_string(R"({"t":"num","extra":true,"v":5,"other":"x"})", out).has_value());
    EXPECT(std::get<int>(out) == 5);
}

ZEST_CASE(missing_tag_fails) {
    AdjSimple out{};
    // Only content, no tag
    EXPECT(!from_string(R"({"v":42})", out).has_value());
}

ZEST_CASE(missing_content_fails) {
    AdjSimple out{};
    // Only tag, no content
    EXPECT(!from_string(R"({"t":"num"})", out).has_value());
}

ZEST_CASE(unknown_tag_value_fails) {
    AdjSimple out{};
    EXPECT(!from_string(R"({"t":"unknown","v":42})", out).has_value());
}

ZEST_CASE(duplicate_tag_fails) {
    AdjSimple out{};
    EXPECT(!from_string(R"({"t":"num","t":"str","v":42})", out).has_value());
}

ZEST_CASE(duplicate_content_fails) {
    AdjSimple out{};
    EXPECT(!from_string(R"({"t":"num","v":1,"v":2})", out).has_value());
}

ZEST_CASE(empty_object_fails) {
    AdjSimple out{};
    EXPECT(!from_string(R"({})", out).has_value());
}

ZEST_CASE(not_an_object_fails) {
    AdjSimple out{};
    EXPECT(!from_string("42", out).has_value());
    EXPECT(!from_string(R"("str")", out).has_value());
    EXPECT(!from_string("[1]", out).has_value());
}

ZEST_CASE(in_holder_struct) {
    AdjHolder input{.name = "test", .data = 42};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"name":"test","data":{"t":"num","v":42}})");

    AdjHolder out{};
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(out == input);
}

ZEST_CASE(in_vector) {
    std::vector<AdjSimple> vec = {AdjSimple{1}, AdjSimple{std::string("x")}};
    auto encoded = to_string(vec);
    ASSERT(encoded);
    EXPECT(*encoded == R"([{"t":"num","v":1},{"t":"str","v":"x"}])");

    std::vector<AdjSimple> out;
    ASSERT(from_string(*encoded, out).has_value());
    ASSERT(out.size() == 2U);
    EXPECT(std::get<int>(out[0]) == 1);
    EXPECT(std::get<std::string>(out[1]) == "x");
}

ZEST_CASE(in_map) {
    std::map<std::string, AdjSimple> m;
    m["a"] = AdjSimple{10};
    m["b"] = AdjSimple{std::string("val")};
    auto encoded = to_string(m);
    ASSERT(encoded);

    std::map<std::string, AdjSimple> out;
    ASSERT(from_string(*encoded, out).has_value());
    ASSERT(out.size() == 2U);
    EXPECT(std::get<int>(out["a"]) == 10);
    EXPECT(std::get<std::string>(out["b"]) == "val");
}

};  // ZEST_SUITE(serde_variant_adj)

ZEST_SUITE(serde_variant_int_tag) {

ZEST_CASE(circle_roundtrip) {
    IntTagShape v = Circle{.radius = 5.0};
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"type":"circle","radius":5.0})");

    IntTagShape out{};
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(std::get<Circle>(out) == (Circle{.radius = 5.0}));
}

ZEST_CASE(rect_roundtrip) {
    IntTagShape v = Rect{.width = 3.0, .height = 4.0};
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"type":"rect","width":3.0,"height":4.0})");

    IntTagShape out{};
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(std::get<Rect>(out) == (Rect{3.0, 4.0}));
}

ZEST_CASE(tag_not_first_in_input) {
    // Tag field comes after other fields — deserialization via DOM should still work
    IntTagShape out{};
    ASSERT(from_string(R"({"radius":2.5,"type":"circle"})", out).has_value());
    EXPECT(std::get<Circle>(out) == (Circle{.radius = 2.5}));
}

ZEST_CASE(extra_fields_ignored) {
    // Extra fields in the object should be silently ignored
    IntTagShape out{};
    ASSERT(from_string(R"({"type":"circle","radius":1.0,"extra":"ignored"})", out).has_value());
    EXPECT(std::get<Circle>(out) == (Circle{.radius = 1.0}));
}

ZEST_CASE(three_alternatives) {
    IntTagTriShape v1 = Circle{.radius = 1.0};
    IntTagTriShape v2 = Rect{.width = 2.0, .height = 3.0};
    IntTagTriShape v3 = Triangle{.base = 4.0, .height = 5.0};

    auto e1 = to_string(v1);
    auto e2 = to_string(v2);
    auto e3 = to_string(v3);
    ASSERT(e1);
    ASSERT(e2);
    ASSERT(e3);
    EXPECT(*e1 == R"({"kind":"circle","radius":1.0})");
    EXPECT(*e2 == R"({"kind":"rect","width":2.0,"height":3.0})");
    EXPECT(*e3 == R"({"kind":"triangle","base":4.0,"height":5.0})");

    IntTagTriShape out{};
    ASSERT(from_string(*e1, out).has_value());
    EXPECT(std::get<Circle>(out).radius == 1.0);

    ASSERT(from_string(*e2, out).has_value());
    EXPECT(std::get<Rect>(out) == (Rect{2.0, 3.0}));

    ASSERT(from_string(*e3, out).has_value());
    EXPECT(std::get<Triangle>(out) == (Triangle{4.0, 5.0}));
}

ZEST_CASE(unknown_tag_fails) {
    IntTagShape out{};
    EXPECT(!from_string(R"({"type":"pentagon","sides":5})", out).has_value());
}

ZEST_CASE(missing_tag_fails) {
    IntTagShape out{};
    EXPECT(!from_string(R"({"radius":5.0})", out).has_value());
}

ZEST_CASE(tag_not_a_string_fails) {
    IntTagShape out{};
    EXPECT(!from_string(R"({"type":1,"radius":5.0})", out).has_value());
}

ZEST_CASE(not_an_object_fails) {
    IntTagShape out{};
    EXPECT(!from_string("42", out).has_value());
    EXPECT(!from_string(R"("circle")", out).has_value());
    EXPECT(!from_string("[1]", out).has_value());
    EXPECT(!from_string("null", out).has_value());
}

ZEST_CASE(in_holder_struct) {
    IntTagHolder input{.name = "shape1", .shape = Circle{.radius = 9.0}};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"name":"shape1","shape":{"type":"circle","radius":9.0}})");

    IntTagHolder out{};
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(out == input);
}

ZEST_CASE(in_vector) {
    std::vector<IntTagShape> shapes = {
        IntTagShape{Circle{.radius = 1.0}},
        IntTagShape{Rect{.width = 2.0, .height = 3.0}},
    };
    auto encoded = to_string(shapes);
    ASSERT(encoded);

    std::vector<IntTagShape> out;
    ASSERT(from_string(*encoded, out).has_value());
    ASSERT(out.size() == 2U);
    EXPECT(std::get<Circle>(out[0]).radius == 1.0);
    EXPECT(std::get<Rect>(out[1]) == (Rect{2.0, 3.0}));
}

ZEST_CASE(in_optional) {
    std::optional<IntTagShape> present = IntTagShape{
        Rect{.width = 1.0, .height = 2.0}
    };
    auto encoded = to_string(present);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"type":"rect","width":1.0,"height":2.0})");

    std::optional<IntTagShape> out;
    ASSERT(from_string(*encoded, out).has_value());
    ASSERT(out);
    EXPECT(std::get<Rect>(*out) == (Rect{1.0, 2.0}));

    ASSERT(from_string("null", out).has_value());
    EXPECT(!out);
}

ZEST_CASE(missing_required_field_rejects_tagged_candidate) {
    // Missing non-optional field in a tagged variant should fail deserialization
    IntTagShape out{};
    auto result = from_string(R"({"type":"rect","width":5.0})", out);
    EXPECT(!result);
}

ZEST_CASE(missing_required_field_rejects_untagged_variant_candidate) {
    // In untagged variant probing, a missing required field should reject
    // the candidate and try the next alternative.
    // Circle has field "radius", Rect has "width" + "height".
    // {"width": 5.0} is missing "height" so Rect should be rejected.
    using UntaggedShape = std::variant<Rect, Circle>;
    UntaggedShape out{};
    // This should fail because Rect needs both width+height, and Circle
    // doesn't match either (no "radius" field).
    auto result = from_string(R"({"width":5.0})", out);
    EXPECT(!result);
}

};  // ZEST_SUITE(serde_variant_int_tag)

KOTATSU_ANNOTATION(nested_inner_annotation, tagged = true, tag_names = {"i", "s"});
KOTATSU_ANNOTATION(nested_outer_annotation, tagged = true, tag_names = {"plain", "wrapped"});
KOTATSU_ANNOTATION(vec_tagged_annotation, tag = "t", content = "v", tag_names = {"i", "s"});

ZEST_SUITE(serde_variant_nested) {

ZEST_CASE(variant_in_struct_in_variant) {
    // An externally tagged variant whose struct alternative contains another ext variant
    using Inner = annotate<nested_inner_annotation>::type<std::variant<int, std::string>>;

    struct Wrapper {
        std::string id;
        Inner val;
    };

    using Outer = annotate<nested_outer_annotation>::type<std::variant<int, Wrapper>>;

    Outer v = Wrapper{.id = "w1", .val = std::string("inner")};
    auto encoded = to_string(v);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"wrapped":{"id":"w1","val":{"s":"inner"}}})");

    Outer out{};
    ASSERT(from_string(*encoded, out).has_value());
    EXPECT(out == v);
}

ZEST_CASE(vector_of_tagged_variants) {
    using V = annotate<vec_tagged_annotation>::type<std::variant<int, std::string>>;

    std::vector<V> vec = {V{1}, V{std::string("a")}, V{2}, V{std::string("b")}};
    auto encoded = to_string(vec);
    ASSERT(encoded);

    std::vector<V> out;
    ASSERT(from_string(*encoded, out).has_value());
    ASSERT(out.size() == 4U);
    EXPECT(std::get<int>(out[0]) == 1);
    EXPECT(std::get<std::string>(out[1]) == "a");
    EXPECT(std::get<int>(out[2]) == 2);
    EXPECT(std::get<std::string>(out[3]) == "b");
}

ZEST_CASE(map_of_internally_tagged) {
    std::map<std::string, IntTagShape> shapes;
    shapes["c"] = IntTagShape{Circle{.radius = 1.0}};
    shapes["r"] = IntTagShape{
        Rect{.width = 2.0, .height = 3.0}
    };

    auto encoded = to_string(shapes);
    ASSERT(encoded);

    std::map<std::string, IntTagShape> out;
    ASSERT(from_string(*encoded, out).has_value());
    ASSERT(out.size() == 2U);
    EXPECT(std::get<Circle>(out["c"]).radius == 1.0);
    EXPECT(std::get<Rect>(out["r"]) == (Rect{2.0, 3.0}));
}

ZEST_CASE(optional_tagged_absent) {
    std::optional<ExtSimple> absent;
    auto encoded = to_string(absent);
    ASSERT(encoded);
    EXPECT(*encoded == "null");

    std::optional<ExtSimple> out = ExtSimple{42};
    ASSERT(from_string("null", out).has_value());
    EXPECT(!out);
}

};  // ZEST_SUITE(serde_variant_nested)

struct skip_if_none_extra_tag {
    constexpr static auto spec = make_spec(dsl::skip_if = skip_when::none);
};

ZEST_SUITE(serde_variant_deep_dispatch) {

ZEST_CASE(struct_with_variant_field_disambiguation) {
    // Two structs whose variant-typed fields accept different source kinds.
    // Deep scoring should recurse into the variant field's alternatives.
    struct HasIntOrString {
        std::variant<int, std::string> data;
    };

    struct HasBoolOrDouble {
        std::variant<bool, double> data;
    };

    using V = std::variant<HasIntOrString, HasBoolOrDouble>;

    V out{};
    ASSERT(from_string(R"({"data":42})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<int>(std::get<HasIntOrString>(out).data) == 42);

    ASSERT(from_string(R"({"data":true})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<bool>(std::get<HasBoolOrDouble>(out).data) == true);

    ASSERT(from_string(R"({"data":"text"})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::string>(std::get<HasIntOrString>(out).data) == "text");

    ASSERT(from_string(R"({"data":3.14})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<double>(std::get<HasBoolOrDouble>(out).data) == 3.14);
}

ZEST_CASE(vector_vs_tuple_by_length) {
    // Tuple requires exact element count; vector accepts any length.
    // Deep scoring uses element count to disambiguate.
    using V = std::variant<std::tuple<int, std::string>, std::vector<int>>;

    V out{};
    ASSERT(from_string(R"([1,2,3])", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::vector<int>>(out) == std::vector<int>({1, 2, 3}));

    ASSERT(from_string(R"([42,"hello"])", out).has_value());
    EXPECT(out.index() == 0U);
    auto& [i, s] = std::get<std::tuple<int, std::string>>(out);
    EXPECT(i == 42);
    EXPECT(s == "hello");
}

ZEST_CASE(triple_nested_variant) {
    // Three levels of variant nesting.
    using L0 = std::variant<bool, std::string>;
    using L1 = std::variant<L0, int>;
    using L2 = std::variant<L1, double>;

    L2 out{};

    // bool → L0 has bool (exact), L1 recurses to L0, L2 recurses to L1
    ASSERT(from_string("true", out).has_value());
    EXPECT(out.index() == 0U);
    auto& l1 = std::get<L1>(out);
    EXPECT(l1.index() == 0U);
    auto& l0 = std::get<L0>(l1);
    EXPECT(l0.index() == 0U);
    EXPECT(std::get<bool>(l0) == true);

    // string → only L0 has string
    ASSERT(from_string(R"("abc")", out).has_value());
    auto& l1s = std::get<L1>(out);
    auto& l0s = std::get<L0>(l1s);
    EXPECT(std::get<std::string>(l0s) == "abc");

    // integer → L1 has int (exact match, quality 3), double has widening (quality 1)
    ASSERT(from_string("99", out).has_value());
    EXPECT(out.index() == 0U);
    auto& l1i = std::get<L1>(out);
    EXPECT(l1i.index() == 1U);
    EXPECT(std::get<int>(l1i) == 99);

    // floating → only double accepts float
    ASSERT(from_string("2.5", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<double>(out) == 2.5);
}

ZEST_CASE(variant_of_containers) {
    // Variant choosing between different container types.
    using V = std::variant<std::vector<int>, std::map<std::string, int>>;

    V out{};
    ASSERT(from_string("[10,20]", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::vector<int>>(out) == std::vector<int>({10, 20}));

    ASSERT(from_string(R"({"a":1})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::map<std::string, int>>(out).at("a") == 1);
}

ZEST_CASE(struct_vs_map_object_scoring) {
    // First-match-wins: Point is tried first; if required fields are present it wins.
    using V = std::variant<Point, std::map<std::string, double>>;

    V out{};
    // Point fields "x","y" present → try_read succeeds → Point selected
    ASSERT(from_string(R"({"x":1.0,"y":2.0})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<Point>(out) == (Point{1.0, 2.0}));

    // Point requires x,y which are missing → try_read fails → falls through to map
    ASSERT(from_string(R"({"foo":3.0})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::map<std::string, double>>(out).at("foo") == 3.0);
}

ZEST_CASE(optional_wrapping_variant) {
    // optional<variant> and variant side by side
    using Inner = std::variant<int, std::string>;
    using V = std::variant<std::optional<Inner>, bool>;

    V out{};
    // null → optional accepts null
    ASSERT(from_string("null", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(!std::get<std::optional<Inner>>(out).has_value());

    // bool → only bool alternative matches
    ASSERT(from_string("true", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<bool>(out) == true);

    // integer → optional<variant<int,string>> recurses and finds int
    ASSERT(from_string("42", out).has_value());
    EXPECT(out.index() == 0U);
    auto& opt = std::get<std::optional<Inner>>(out);
    ASSERT(opt);
    EXPECT(std::get<int>(*opt) == 42);
}

ZEST_CASE(shared_ptr_wrapping_variant) {
    using Inner = std::variant<int, std::string>;
    using V = std::variant<std::shared_ptr<Inner>, bool>;

    V out{};
    // null → shared_ptr accepts null
    ASSERT(from_string("null", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::shared_ptr<Inner>>(out) == nullptr);

    // string → recurse through shared_ptr to variant
    ASSERT(from_string(R"("hello")", out).has_value());
    EXPECT(out.index() == 0U);
    auto ptr = std::get<std::shared_ptr<Inner>>(out);
    ASSERT(ptr != nullptr);
    EXPECT(std::get<std::string>(*ptr) == "hello");
}

ZEST_CASE(int_width_ordering) {
    // First-match-wins: the narrowest type that can hold the value is selected.
    using V = std::variant<std::int8_t, std::int16_t, std::int32_t, std::int64_t>;

    V out{};
    // 42 fits in int8_t → first alternative wins
    ASSERT(from_string("42", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::int8_t>(out) == 42);

    // 200 overflows int8_t → falls through to int16_t
    ASSERT(from_string("200", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::int16_t>(out) == 200);

    // 40000 overflows int16_t → falls through to int32_t
    ASSERT(from_string("40000", out).has_value());
    EXPECT(out.index() == 2U);
    EXPECT(std::get<std::int32_t>(out) == 40000);

    // 3000000000 overflows int32_t → falls through to int64_t
    ASSERT(from_string("3000000000", out).has_value());
    EXPECT(out.index() == 3U);
    EXPECT(std::get<std::int64_t>(out) == 3000000000);
}

ZEST_CASE(mixed_numeric_precision) {
    // First-match-wins: float is tried before double.
    using V = std::variant<float, double>;

    V out{};
    // 1.5 is representable in float → first alternative wins
    ASSERT(from_string("1.5", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<float>(out) == 1.5f);
}

ZEST_CASE(variant_roundtrip_complex) {
    // Roundtrip through serialization preserves the correct alternative.
    using V = std::
        variant<std::monostate, bool, std::int64_t, double, std::string, std::vector<int>, Point>;

    auto roundtrip = [](V input, std::size_t expected_index) -> bool {
        auto encoded = to_string(input);
        if(!encoded)
            return false;
        V out{};
        auto status = from_string(*encoded, out);
        if(!status)
            return false;
        return out.index() == expected_index;
    };

    EXPECT(roundtrip(std::monostate{}, 0));
    EXPECT(roundtrip(true, 1));
    EXPECT(roundtrip(std::int64_t{42}, 2));
    EXPECT(roundtrip(3.14, 3));
    EXPECT(roundtrip(std::string("test"), 4));
    EXPECT(roundtrip(std::vector<int>{1, 2, 3}, 5));
    EXPECT(roundtrip(Point{1.0, 2.0}, 6));
}

ZEST_CASE(two_structs_different_field_count) {
    // First-match-wins: Color is tried first.
    using V = std::variant<Color, Point>;

    V out{};
    // Color requires r,g,b — all present → try_read succeeds
    ASSERT(from_string(R"({"r":1,"g":2,"b":3})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<Color>(out) == (Color{1, 2, 3}));

    // Color requires r,g,b which are missing → try_read fails → falls through to Point
    ASSERT(from_string(R"({"x":1.0,"y":2.0})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<Point>(out) == (Point{1.0, 2.0}));
}

ZEST_CASE(empty_object_scoring) {
    using V = std::variant<Point, std::map<std::string, int>>;

    V out{};
    // Empty object has no matching struct fields → map wins
    ASSERT(from_string(R"({})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::map<std::string, int>>(out).empty());
}

ZEST_CASE(empty_array_scoring) {
    using V = std::variant<std::vector<int>, std::string>;

    V out{};
    // Empty array → vector accepts it
    ASSERT(from_string(R"([])", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::vector<int>>(out).empty());
}

ZEST_CASE(same_field_same_type_struct_tie) {
    // Two structs with identical field names and types — first wins
    struct A {
        int value;
    };

    struct B {
        int value;
    };

    using V = std::variant<A, B>;

    V out{};
    ASSERT(from_string(R"({"value":1})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<A>(out).value == 1);
}

ZEST_CASE(variant_wrapper_no_inflation) {
    // variant<variant<double, int>, int> facing integer source:
    // The nested variant should NOT score higher than the direct int.
    using Inner = std::variant<double, int>;
    using V = std::variant<Inner, int>;

    V out{};
    ASSERT(from_string("42", out).has_value());
    // Both alternatives accept int. The nested variant's best inner match (int)
    // should score the same as the direct int. First one wins on tie.
    EXPECT(out.index() == 0U);
    auto& inner = std::get<Inner>(out);
    EXPECT(inner.index() == 1U);
    EXPECT(std::get<int>(inner) == 42);
}

ZEST_CASE(field_subset_superset_matching) {
    // First-match-wins: place more-specific struct first to prefer it.
    struct StructA {
        int a;
    };

    struct StructAB {
        int a;
        int b;
    };

    // StructAB first: {a,b} matches StructAB (all required fields present)
    using V = std::variant<StructAB, StructA>;

    V out{};
    ASSERT(from_string(R"({"a":1,"b":2})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<StructAB>(out).a == 1);
    EXPECT(std::get<StructAB>(out).b == 2);

    // {a} alone — StructAB requires field "b" → try_read fails → falls through to StructA
    ASSERT(from_string(R"({"a":1})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<StructA>(out).a == 1);
}

ZEST_CASE(recursive_map_scoring) {
    using V = std::variant<std::map<std::string, int>, std::map<std::string, std::string>>;

    V out{};
    ASSERT(from_string(R"({"a":1,"b":2})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<std::map<std::string, int>>(out).at("a") == 1);

    ASSERT(from_string(R"({"a":"x","b":"y"})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<std::map<std::string, std::string>>(out).at("a") == "x");
}

ZEST_CASE(subset_with_required_field_missing) {
    // Two structs sharing a common field, but the larger struct has a required
    // field not present in the input.  The smaller struct (whose required fields
    // are all satisfied) should win.
    struct Partial {
        int id;
        std::string value;
    };

    struct Whole {
        std::string value;
    };

    using V = std::variant<Partial, Whole>;

    V out{};
    // Only "value" present — Partial.id is required and missing → Whole should win
    ASSERT(from_string(R"({"value":"hello"})", out).has_value());
    EXPECT(out.index() == 1U);
    EXPECT(std::get<Whole>(out).value == "hello");

    // Both "id" and "value" present — Partial matches more fields → Partial wins
    ASSERT(from_string(R"({"id":42,"value":"hello"})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<Partial>(out).id == 42);
    EXPECT(std::get<Partial>(out).value == "hello");
}

ZEST_CASE(subset_with_optional_field_missing) {
    // When the missing field has a skip_if condition, it should NOT be penalized.
    struct WithOptional {
        std::string value;
        annotate<skip_if_none_extra_tag>::type<std::optional<int>> extra;
    };

    struct Plain {
        std::string value;
    };

    using V = std::variant<WithOptional, Plain>;

    V out{};
    // "extra" is optional (has default) → WithOptional still viable, first wins on tie
    ASSERT(from_string(R"({"value":"hello"})", out).has_value());
    EXPECT(out.index() == 0U);
    EXPECT(std::get<WithOptional>(out).value == "hello");
}

};  // ZEST_SUITE(serde_variant_deep_dispatch)

}  // namespace

}  // namespace kota::codec
