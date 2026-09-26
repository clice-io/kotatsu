#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/json/schema.h"
#include "kota/codec/macro.h"

namespace kota::meta {

struct json_schema_opaque_root {};

/// A non-reflectable class (raw kind unknown) whose meta::repr resolves to a
/// struct with a defaulted member: the schema describes the representation's
/// shape, but the defaults pass covers only types the decoder reads
/// directly, so a repr-routed root stays unannotated.
class json_schema_reprd_root {
public:
    int total() const {
        return total_;
    }

private:
    int total_ = 5;
};

struct json_schema_reprd_repr {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> total = 5;
};

/// A declarative repr whose to() disagrees with a fresh representation
/// (a fresh root encodes n = 9, decode value-initializes n = 4): no single
/// honest default exists behind the repr, so the schema carries none.
class json_schema_reprd_shifted {
public:
    json_schema_reprd_shifted() = default;

    explicit json_schema_reprd_shifted(int n) : n_(n) {}

    int n() const {
        return n_;
    }

private:
    int n_ = 9;
};

struct json_schema_reprd_shifted_repr {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> n = 4;
};

/// An imperative repr with a struct representation: repr_decode's imperative
/// branch hands the caller's value straight to deserialize — the declared
/// representation is never constructed — so what an absent property leaves
/// behind is the repr's business, and the schema carries no default.
class json_schema_imperative_root {
public:
    int n() const {
        return n_;
    }

    void set_n(int n) {
        n_ = n;
    }

private:
    int n_ = 9;
};

struct json_schema_imperative_repr {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> n = 4;
};

}  // namespace kota::meta

namespace kota::meta {

template <>
constexpr inline bool schema_opaque<kota::meta::json_schema_opaque_root> = true;

template <>
struct repr<kota::meta::json_schema_reprd_root> {
    using type = kota::meta::json_schema_reprd_repr;

    static type to(const kota::meta::json_schema_reprd_root& v) {
        return {.total = v.total()};
    }
};

template <>
struct repr<kota::meta::json_schema_reprd_shifted> {
    using type = kota::meta::json_schema_reprd_shifted_repr;

    static type to(const kota::meta::json_schema_reprd_shifted& v) {
        return {.n = v.n()};
    }

    static kota::meta::json_schema_reprd_shifted from(const type& d) {
        return kota::meta::json_schema_reprd_shifted(d.n);
    }
};

template <>
struct repr<kota::meta::json_schema_imperative_root> {
    using type = kota::meta::json_schema_imperative_repr;

    template <typename Config>
    static bool serialize(auto& vis, const kota::meta::json_schema_imperative_root& v) {
        return codec::encode_value<Config>(vis, type{.n = v.n()});
    }

    template <typename Config>
    static bool deserialize(auto& vis, kota::meta::json_schema_imperative_root& v) {
        // Seed the representation from the in-place value: an absent
        // property keeps it.
        type d{.n = v.n()};
        if(!codec::decode_value<Config>(vis, d)) {
            return false;
        }
        v.set_n(d.n);
        return true;
    }
};

}  // namespace kota::meta

namespace kota::meta {

namespace {

// ---------------------------------------------------------------------------
// Scalar wrappers
// ---------------------------------------------------------------------------
struct s_bool {
    bool v;
};

struct s_i8 {
    std::int8_t v;
};

struct s_i16 {
    std::int16_t v;
};

struct s_i32 {
    std::int32_t v;
};

struct s_i64 {
    std::int64_t v;
};

struct s_u8 {
    std::uint8_t v;
};

struct s_u16 {
    std::uint16_t v;
};

struct s_u32 {
    std::uint32_t v;
};

struct s_u64 {
    std::uint64_t v;
};

struct s_f32 {
    float v;
};

struct s_f64 {
    double v;
};

struct s_char {
    char v;
};

struct s_str {
    std::string v;
};

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum class color_i8 : std::int8_t { red = 0, green = 1, blue = 2 };
enum class single_enum : std::int32_t { only = 42 };
enum class status : std::int32_t { ok = 0, fail = 1, pending = 2 };
enum class flag_u8 : std::uint8_t { off = 0, on = 1 };
enum class level_i16 : std::int16_t { low = 0, mid = 50, high = 100 };

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------
struct s_vec_i32 {
    std::vector<std::int32_t> v;
};

struct s_set_i32 {
    std::set<std::int32_t> v;
};

struct s_map_str_i32 {
    std::map<std::string, std::int32_t> v;
};

struct s_vec_vec_i32 {
    std::vector<std::vector<std::int32_t>> v;
};

struct s_map_str_vec_i32 {
    std::map<std::string, std::vector<std::int32_t>> v;
};

// ---------------------------------------------------------------------------
// Tuple / Pair
// ---------------------------------------------------------------------------
struct s_pair {
    std::pair<std::string, std::int32_t> v;
};

struct s_tuple {
    std::tuple<std::int32_t, std::string, bool> v;
};

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------
struct empty_struct {};

struct single_field {
    std::int32_t x;
};

struct point2d {
    std::int32_t x;
    std::int32_t y;
};

struct with_string {
    std::string name;
    std::int32_t value;
};

// ---------------------------------------------------------------------------
// Nested structs
// ---------------------------------------------------------------------------
struct inner {
    std::int32_t a;
};

struct middle {
    inner i;
    std::string s;
};

struct outer {
    middle m;
    std::int32_t n;
};

// ---------------------------------------------------------------------------
// Struct with enum
// ---------------------------------------------------------------------------
struct with_enum {
    color_i8 c;
    std::string name;
};

// ---------------------------------------------------------------------------
// Optional / pointer
// ---------------------------------------------------------------------------
struct with_optional {
    std::string name;
    std::optional<std::int32_t> age;
};

struct with_unique {
    std::string name;
    std::unique_ptr<std::int32_t> ptr;
};

struct with_shared {
    std::string name;
    std::shared_ptr<std::int32_t> ptr;
};

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------
struct with_default {
    std::string name;
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> count;
};

struct with_skip {
    std::string visible;
    KOTATSU_ANNOTATE(skip = true)
    <std::int32_t> hidden;
};

struct base_fields {
    std::int32_t a;
    std::int32_t b;
};

struct with_flatten {
    KOTATSU_ANNOTATE(flatten = true)
    <base_fields> base;
    std::string extra;
};

struct with_rename {
    KOTATSU_ANNOTATE(rename = "my_field")
    <std::int32_t> x;
    std::string y;
};

struct with_skip_when {
    std::string name;
    KOTATSU_ANNOTATE(skip_if = skip_when::empty)
    <std::vector<std::int32_t>> tags;
    KOTATSU_ANNOTATE(skip_if = skip_when::default_value)
    <std::int32_t> count;
    KOTATSU_ANNOTATE(skip_if = type<pred::empty>)
    <std::string> note;
};

struct casing_child {
    std::int32_t first_value;
};

struct repeated_child_annotation {
    KOTATSU_ANNOTATE(rename_all = casing::lower_camel)
    <casing_child> left;
    KOTATSU_ANNOTATE(rename_all = casing::lower_camel)
    <casing_child> right;
};

// ---------------------------------------------------------------------------
// Variant
// ---------------------------------------------------------------------------
struct var_none {
    std::variant<std::int32_t, std::string> v;
};

struct var_three {
    std::variant<std::int32_t, std::string, bool> v;
};

struct tagged_circle {
    double radius;
};

struct tagged_rect {
    double width;
    double height;
};

KOTATSU_ANNOTATION(root_external_annotation, tagged = true, tag_names = {"integer", "text"});
using root_external_variant =
    annotate<root_external_annotation>::type<std::variant<std::int32_t, std::string>>;

KOTATSU_ANNOTATION(root_internal_annotation, tag = "kind", tag_names = {"circle", "rect"});
using root_internal_variant =
    annotate<root_internal_annotation>::type<std::variant<tagged_circle, tagged_rect>>;

KOTATSU_ANNOTATION(root_adjacent_annotation,
                   tag = "type",
                   content = "value",
                   tag_names = {"integer", "text"});
using root_adjacent_variant =
    annotate<root_adjacent_annotation>::type<std::variant<std::int32_t, std::string>>;

// ---------------------------------------------------------------------------
// Combinations
// ---------------------------------------------------------------------------
struct combo {
    color_i8 color;
    std::optional<std::string> label;
    std::vector<std::int32_t> values;
    std::map<std::string, std::int32_t> attrs;
};

struct nested_combo {
    point2d point;
    color_i8 color;
    std::vector<point2d> points;
    std::map<std::string, point2d> named_points;
};

struct multi_map {
    std::map<std::string, std::int32_t> a;
    std::map<std::string, std::string> b;
};

struct vec_of_struct {
    std::vector<point2d> items;
};

struct deep_inner {
    color_i8 c;
    std::int32_t v;
};

struct deep_middle {
    deep_inner di;
    std::string s;
};

struct deep_outer {
    deep_middle dm;
    std::int32_t n;
};

// ---------------------------------------------------------------------------
// Additional types
// ---------------------------------------------------------------------------
struct all_optional {
    std::optional<std::int32_t> a;
    std::optional<std::string> b;
};

struct all_default {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> x;
    KOTATSU_ANNOTATE(defaulted = true)
    <std::string> y;
};

struct skip_default {
    std::string name;
    KOTATSU_ANNOTATE(skip = true)
    <std::int32_t> hidden;
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> count;
};

struct base_with_opt {
    std::int32_t x;
    std::optional<std::int32_t> y;
};

struct flatten_opt {
    KOTATSU_ANNOTATE(flatten = true)
    <base_with_opt> base;
    std::string tag;
};

struct rename_base {
    KOTATSU_ANNOTATE(rename = "alpha")
    <std::int32_t> a;
    std::int32_t b;
};

struct flatten_rename {
    KOTATSU_ANNOTATE(flatten = true)
    <rename_base> inner;
    std::string extra;
};

struct map_str_struct {
    std::map<std::string, point2d> entries;
};

struct map_str_enum {
    std::map<std::string, color_i8> entries;
};

struct vec_optional {
    std::vector<std::optional<std::int32_t>> v;
};

struct optional_vec {
    std::optional<std::vector<std::int32_t>> v;
};

struct with_pair_field {
    std::pair<std::string, std::int32_t> p;
    std::string name;
};

struct with_tuple_field {
    std::tuple<std::int32_t, bool> t;
    std::string name;
};

struct shared_struct {
    std::string name;
    std::shared_ptr<point2d> point;
};

struct multi_ref {
    point2d a;
    point2d b;
    std::vector<point2d> list;
};

struct vec_enum {
    std::vector<color_i8> colors;
};

struct set_string {
    std::set<std::string> tags;
};

struct optional_struct {
    std::optional<point2d> point;
    std::string name;
};

struct vec_map {
    std::vector<std::map<std::string, std::int32_t>> items;
};

struct map_vec_struct {
    std::map<std::string, std::vector<point2d>> groups;
};

struct trivial_nested {
    point2d p;
    std::int32_t z;
};

struct multi_enum {
    color_i8 c;
    status s;
    std::string label;
};

struct with_flag {
    flag_u8 f;
    std::string name;
};

struct with_level {
    level_i16 l;
    std::int32_t v;
};

struct with_all_ptr {
    std::optional<std::string> opt;
    std::unique_ptr<std::int32_t> uniq;
    std::shared_ptr<bool> shr;
};

struct deep_container {
    std::map<std::string, std::vector<std::map<std::string, std::int32_t>>> data;
};

struct optional_inner {
    std::optional<inner> i;
    std::string name;
};

struct map_of_map {
    std::map<std::string, std::map<std::string, std::int32_t>> m;
};

struct vec_variant {
    std::vector<std::variant<std::int32_t, std::string>> items;
};

struct many_fields {
    std::int32_t a;
    std::int32_t b;
    std::int32_t c;
    std::string d;
    bool e;
    double f;
};

struct set_of_struct {
    std::set<std::int32_t> ids;
    std::string name;
};

// ---------------------------------------------------------------------------
// description fixtures
// ---------------------------------------------------------------------------

struct desc_scalar {
    KOTATSU_ANNOTATE(description = "Number of worker threads.")
    <std::int32_t> threads;
    std::string name;
};

struct desc_optional {
    KOTATSU_ANNOTATE(description = "Optional display label.")
    <std::optional<std::string>> label;
};

struct desc_struct_ref {
    KOTATSU_ANNOTATE(description = "Anchor position.")
    <point2d> anchor;
};

struct desc_base {
    KOTATSU_ANNOTATE(description = "Inherited counter.")
    <std::int32_t> count;
};

struct desc_flatten {
    KOTATSU_ANNOTATE(flatten = true)
    <desc_base> base;
    std::string tag;
};

struct desc_rename {
    KOTATSU_ANNOTATE(rename = "max_size", description = "Maximum size in bytes.")
    <std::int32_t> size;
};

struct desc_default {
    KOTATSU_ANNOTATE(defaulted = true, description = "Retry limit.")
    <std::int32_t> retries;
};

struct desc_shared_ref {
    point2d origin;
    KOTATSU_ANNOTATE(description = "Anchor position.")
    <point2d> anchor;
};

struct desc_tagged_circle {
    KOTATSU_ANNOTATE(description = "Radius in meters.")
    <double> radius;
};

struct desc_tagged_rect {
    double width;
    double height;
};

KOTATSU_ANNOTATION(desc_internal_annotation, tag = "kind", tag_names = {"circle", "rect"});
using desc_internal_variant =
    annotate<desc_internal_annotation>::type<std::variant<desc_tagged_circle, desc_tagged_rect>>;

// ---------------------------------------------------------------------------
// default annotation fixtures
// ---------------------------------------------------------------------------

struct defaults_leaf {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> threads = 4;

    KOTATSU_ANNOTATE(defaulted = true)
    <std::string> name = "worker";
};

struct defaults_root {
    KOTATSU_ANNOTATE(defaulted = true)
    <bool> enabled = true;

    defaults_leaf pool;
    defaults_leaf mirror;

    std::optional<std::int32_t> limit;

    KOTATSU_ANNOTATE(defaulted = true)
    <std::vector<std::int32_t>> ids;
};

struct defaults_skipped {
    KOTATSU_ANNOTATE(skip_if = skip_when::empty)
    <std::vector<std::int32_t>> tags;
};

enum class defaults_level : std::uint8_t { Low = 0, High = 1 };

struct defaults_with_enum {
    KOTATSU_ANNOTATE(defaulted = true)
    <defaults_level> log_level = defaults_level::High;
};

struct defaults_shared_override {
    defaults_leaf pool;
    defaults_leaf mirror = {.threads = 9};
};

struct defaults_engaged {
    std::optional<std::int32_t> limit = 5;
    std::optional<defaults_leaf> anchor = defaults_leaf{};
};

struct defaults_node {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> depth = 1;

    std::unique_ptr<defaults_node> next;
};

struct defaults_ref_sites {
    KOTATSU_ANNOTATE(defaulted = true)
    <defaults_leaf> pool;

    KOTATSU_ANNOTATE(defaulted = true)
    <defaults_leaf> mirror = {defaults_leaf{.threads = 9}};
};

struct defaults_alt_a {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> depth = 3;

    std::string name;
};

struct defaults_alt_b {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> other = 9;
};

KOTATSU_ANNOTATION(defaults_internal_annotation, tag = "kind", tag_names = {"a", "b"});
using defaults_internal_variant =
    annotate<defaults_internal_annotation>::type<std::variant<defaults_alt_a, defaults_alt_b>>;

struct defaults_variant_holder {
    defaults_internal_variant shape;
};

KOTATSU_ANNOTATION(defaults_adjacent_annotation,
                   tag = "type",
                   content = "value",
                   tag_names = {"a", "b"});
using defaults_adjacent_variant =
    annotate<defaults_adjacent_annotation>::type<std::variant<defaults_alt_a, defaults_alt_b>>;

KOTATSU_ANNOTATION(defaults_external_annotation, tagged = true, tag_names = {"a", "b"});
using defaults_external_variant =
    annotate<defaults_external_annotation>::type<std::variant<defaults_alt_a, defaults_alt_b>>;

struct defaults_elem_a {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> alpha = 7;
};

struct defaults_elem_b {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::string> beta = "cell";
};

struct defaults_elem_c {
    KOTATSU_ANNOTATE(defaulted = true)
    <bool> gamma = true;
};

struct defaults_containers {
    std::vector<defaults_elem_a> pool = {{}};
    std::tuple<defaults_elem_b, std::int32_t> entry;
    std::map<std::string, defaults_elem_c> index = {
        {"main", {}}
    };
};

struct defaults_seq_override {
    std::vector<defaults_leaf> workers = {{.threads = 9}};
};

struct defaults_variant_override {
    defaults_internal_variant shape{
        defaults_alt_a{.depth = 8, .name = {}}
    };
};

struct defaults_cyclic {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> depth = 1;

    std::vector<defaults_cyclic> kids = {
        {.depth = 2, .kids = {}}
    };
};

struct defaults_engaged_override {
    std::optional<defaults_leaf> anchor = defaults_leaf{.threads = 9};
};

struct defaults_mid {
    defaults_leaf leaf;
};

struct defaults_cascade_root {
    defaults_mid mid = {.leaf = {.threads = 9}};
};

struct defaults_tuple_override {
    std::tuple<defaults_leaf, std::int32_t> entry = {defaults_leaf{.threads = 9}, 0};
};

struct renamed_defaults_child {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> first_value = 4;
};

struct renamed_defaults_holder {
    KOTATSU_ANNOTATE(rename_all = casing::lower_camel)
    <renamed_defaults_child> child;
};

KOTATSU_ANNOTATION(renamed_defaults_root_annotation, rename_all = casing::lower_camel);
using renamed_defaults_root =
    annotate<renamed_defaults_root_annotation>::type<renamed_defaults_child>;

namespace json = kota::codec::json;

template <typename T>
void check_root_integer_schema() {
    const auto result = json::schema_string<T>().value();
    std::string expected;
    if constexpr(std::is_signed_v<T>) {
        expected = std::format(
            R"({{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"integer","minimum":{},"maximum":{}}})",
            static_cast<std::int64_t>(std::numeric_limits<T>::min()),
            static_cast<std::int64_t>(std::numeric_limits<T>::max()));
    } else {
        expected = std::format(
            R"({{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"integer","minimum":0,"maximum":{}}})",
            static_cast<std::uint64_t>(std::numeric_limits<T>::max()));
    }
    EXPECT(result == expected);
}

template <typename Wrapper, typename T>
void check_wrapper_integer_schema() {
    const auto result = json::schema_string<Wrapper>().value();
    std::string expected;
    if constexpr(std::is_signed_v<T>) {
        expected = std::format(
            R"({{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{{"v":{{"type":"integer","minimum":{},"maximum":{}}}}},"required":["v"]}})",
            static_cast<std::int64_t>(std::numeric_limits<T>::min()),
            static_cast<std::int64_t>(std::numeric_limits<T>::max()));
    } else {
        expected = std::format(
            R"({{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{{"v":{{"type":"integer","minimum":0,"maximum":{}}}}},"required":["v"]}})",
            static_cast<std::uint64_t>(std::numeric_limits<T>::max()));
    }
    EXPECT(result == expected);
}

ZEST_SUITE(codec_json_schema) {

// ---------------------------------------------------------------------------
// Root scalars
// ---------------------------------------------------------------------------

ZEST_CASE(root_bool) {
    const auto result = json::schema_string<bool>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"boolean"})");
}

ZEST_CASE(root_integers) {
    check_root_integer_schema<std::int8_t>();
    check_root_integer_schema<std::int16_t>();
    check_root_integer_schema<std::int32_t>();
    check_root_integer_schema<std::int64_t>();
    check_root_integer_schema<std::uint8_t>();
    check_root_integer_schema<std::uint16_t>();
    check_root_integer_schema<std::uint32_t>();
    check_root_integer_schema<std::uint64_t>();
}

ZEST_CASE(root_floats) {
    // The default nan_repr (Passthrough) hands non-finite values to the
    // writer, which emits null — so even the default schema admits null.
    const auto schema =
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema","anyOf":[{"type":"number"},{"type":"null"}]})";
    EXPECT(json::schema_string<float>().value() == schema);
    EXPECT(json::schema_string<double>().value() == schema);
}

ZEST_CASE(root_char) {
    const auto result = json::schema_string<char>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"string"})");
}

ZEST_CASE(root_string) {
    const auto result = json::schema_string<std::string>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"string"})");
}

// ---------------------------------------------------------------------------
// Scalar struct wrappers
// ---------------------------------------------------------------------------

ZEST_CASE(scalar_wrapper_bool) {
    const auto result = json::schema_string<s_bool>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"boolean"}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(scalar_wrapper_integers) {
    check_wrapper_integer_schema<s_i8, std::int8_t>();
    check_wrapper_integer_schema<s_i16, std::int16_t>();
    check_wrapper_integer_schema<s_i32, std::int32_t>();
    check_wrapper_integer_schema<s_i64, std::int64_t>();
    check_wrapper_integer_schema<s_u8, std::uint8_t>();
    check_wrapper_integer_schema<s_u16, std::uint16_t>();
    check_wrapper_integer_schema<s_u32, std::uint32_t>();
    check_wrapper_integer_schema<s_u64, std::uint64_t>();
}

ZEST_CASE(scalar_wrapper_floats) {
    const auto schema =
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"v":{"anyOf":[{"type":"number"},{"type":"null"}]}},"required":["v"]})";
    EXPECT(json::schema_string<s_f32>().value() == schema);
    EXPECT(json::schema_string<s_f64>().value() == schema);
}

ZEST_CASE(scalar_wrapper_char) {
    const auto result = json::schema_string<s_char>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"string"}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(scalar_wrapper_str) {
    const auto result = json::schema_string<s_str>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"string"}},)"
                     R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Root enums
// ---------------------------------------------------------------------------

ZEST_CASE(root_enum_color_i8) {
    const auto result = json::schema_string<color_i8>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"integer","minimum":-128,"maximum":127})");
}

ZEST_CASE(root_enum_single) {
    const auto result = json::schema_string<single_enum>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"integer","minimum":-2147483648,"maximum":2147483647})");
}

ZEST_CASE(root_enum_status) {
    const auto result = json::schema_string<status>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"integer","minimum":-2147483648,"maximum":2147483647})");
}

ZEST_CASE(root_enum_flag_u8) {
    const auto result = json::schema_string<flag_u8>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"integer","minimum":0,"maximum":255})");
}

ZEST_CASE(root_enum_level_i16) {
    const auto result = json::schema_string<level_i16>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"integer","minimum":-32768,"maximum":32767})");
}

ZEST_CASE(root_enum_value_outside_name_scan) {
    // 65535 lies outside the [-128, 127] name-reflection scan, yet encodes
    // fine as a number — the schema must not reject it, so the numeric form
    // is the underlying integer's range rather than a reflected value list.
    enum class big_u16 : std::uint16_t { a = 0, c = 65535 };
    const auto encoded = json::to_string(big_u16::c);
    ASSERT(encoded);
    EXPECT(*encoded == "65535");

    const auto result = json::schema_string<big_u16>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"integer","minimum":0,"maximum":65535})");
}

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------

ZEST_CASE(container_vec_i32) {
    const auto result = json::schema_string<s_vec_i32>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"array",)"
                     R"("items":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(container_set_i32) {
    const auto result = json::schema_string<s_set_i32>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"array",)"
                     R"("items":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("uniqueItems":true}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(container_map_str_i32) {
    const auto result = json::schema_string<s_map_str_i32>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"object",)"
                     R"("additionalProperties":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(container_vec_vec_i32) {
    const auto result = json::schema_string<s_vec_vec_i32>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"array",)"
                     R"("items":{"type":"array",)"
                     R"("items":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(map_str_vec_i32) {
    const auto result = json::schema_string<s_map_str_vec_i32>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"object",)"
                     R"("additionalProperties":{"type":"array",)"
                     R"("items":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}}},)"
                     R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Tuple / Pair
// ---------------------------------------------------------------------------

ZEST_CASE(tuple_pair) {
    const auto result = json::schema_string<s_pair>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"array",)"
                     R"("prefixItems":[)"
                     R"({"type":"string"},)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}],)"
                     R"("items":false,)"
                     R"("minItems":2,"maxItems":2}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(tuple_triple) {
    const auto result = json::schema_string<s_tuple>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"array",)"
                     R"("prefixItems":[)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"string"},)"
                     R"({"type":"boolean"}],)"
                     R"("items":false,)"
                     R"("minItems":3,"maxItems":3}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(tuple_pair_in_struct) {
    const auto result = json::schema_string<with_pair_field>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("p":{"type":"array",)"
                     R"("prefixItems":[)"
                     R"({"type":"string"},)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}],)"
                     R"("items":false,)"
                     R"("minItems":2,"maxItems":2},)"
                     R"("name":{"type":"string"}},)"
                     R"("required":["p","name"]})");
}

ZEST_CASE(tuple_in_struct) {
    const auto result = json::schema_string<with_tuple_field>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("t":{"type":"array",)"
                     R"("prefixItems":[)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"boolean"}],)"
                     R"("items":false,)"
                     R"("minItems":2,"maxItems":2},)"
                     R"("name":{"type":"string"}},)"
                     R"("required":["t","name"]})");
}

// ---------------------------------------------------------------------------
// Basic structs
// ---------------------------------------------------------------------------

ZEST_CASE(struct_empty) {
    const auto result = json::schema_string<empty_struct>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{}})");
}

ZEST_CASE(struct_single_field) {
    const auto result = json::schema_string<single_field>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x"]})");
}

ZEST_CASE(struct_point2d) {
    const auto result = json::schema_string<point2d>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]})");
}

ZEST_CASE(struct_with_string) {
    const auto result = json::schema_string<with_string>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("value":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["name","value"]})");
}

// ---------------------------------------------------------------------------
// Nested structs
// ---------------------------------------------------------------------------

ZEST_CASE(nested_inner) {
    const auto result = json::schema_string<inner>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("a":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["a"]})");
}

ZEST_CASE(nested_middle) {
    const auto result = json::schema_string<middle>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("i":{"$ref":"#/$defs/inner"},)"
                     R"("s":{"type":"string"}},)"
                     R"("required":["i","s"],)"
                     R"("$defs":{)"
                     R"("inner":{"type":"object",)"
                     R"("properties":{)"
                     R"("a":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["a"]}}})");
}

ZEST_CASE(nested_outer) {
    const auto result = json::schema_string<outer>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("m":{"$ref":"#/$defs/middle"},)"
                     R"("n":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["m","n"],)"
                     R"("$defs":{)"
                     R"("inner":{"type":"object",)"
                     R"("properties":{)"
                     R"("a":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["a"]},)"
                     R"("middle":{"type":"object",)"
                     R"("properties":{)"
                     R"("i":{"$ref":"#/$defs/inner"},)"
                     R"("s":{"type":"string"}},)"
                     R"("required":["i","s"]}}})");
}

ZEST_CASE(nested_with_enum) {
    const auto result = json::schema_string<with_enum>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("c":{"type":"integer","minimum":-128,"maximum":127},)"
                     R"("name":{"type":"string"}},)"
                     R"("required":["c","name"]})");
}

// ---------------------------------------------------------------------------
// Optional / pointer
// ---------------------------------------------------------------------------

ZEST_CASE(optional_field) {
    const auto result = json::schema_string<with_optional>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("age":{"anyOf":[{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"null"}],"default":null}},)"
                     R"("required":["name"]})");
}

ZEST_CASE(unique_ptr_field) {
    const auto result = json::schema_string<with_unique>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("ptr":{"anyOf":[{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"null"}],"default":null}},)"
                     R"("required":["name"]})");
}

ZEST_CASE(shared_ptr_field) {
    const auto result = json::schema_string<with_shared>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("ptr":{"anyOf":[{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"null"}],"default":null}},)"
                     R"("required":["name"]})");
}

ZEST_CASE(all_optional_fields) {
    const auto result = json::schema_string<all_optional>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("a":{"anyOf":[{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"null"}],"default":null},)"
                     R"("b":{"anyOf":[{"type":"string"},)"
                     R"({"type":"null"}],"default":null}}})");
}

ZEST_CASE(all_ptr_types) {
    const auto result = json::schema_string<with_all_ptr>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("opt":{"anyOf":[{"type":"string"},)"
                     R"({"type":"null"}],"default":null},)"
                     R"("uniq":{"anyOf":[{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"null"}],"default":null},)"
                     R"("shr":{"anyOf":[{"type":"boolean"},)"
                     R"({"type":"null"}],"default":null}}})");
}

// ---------------------------------------------------------------------------
// default_value attribute
// ---------------------------------------------------------------------------

ZEST_CASE(attr_default_value) {
    const auto result = json::schema_string<with_default>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("count":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647,"default":0}},)"
                     R"("required":["name"]})");
}

ZEST_CASE(all_default_fields) {
    const auto result = json::schema_string<all_default>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647,"default":0},)"
                     R"("y":{"type":"string","default":""}}})");
}

// ---------------------------------------------------------------------------
// deny_unknown_fields
// ---------------------------------------------------------------------------

ZEST_CASE(deny_unknown_struct) {
    const static field_info deny_fields[] = {
        {"name",  {}, 0, 0, type_info_of<std::string>,  false, false, false},
        {"count", {}, 0, 1, type_info_of<std::int32_t>, false, false, false},
    };
    const static struct_type_info deny_info = {
        {type_kind::structure, "deny_struct"},
        true, // deny_unknown
        false, // is_trivial_layout
        {deny_fields,          2            },
    };
    const auto result = json::schema_string(deny_info).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("count":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["name","count"],)"
                     R"("additionalProperties":false})");
}

// ---------------------------------------------------------------------------
// skip
// ---------------------------------------------------------------------------

ZEST_CASE(attr_skip) {
    const auto result = json::schema_string<with_skip>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("visible":{"type":"string"}},)"
                     R"("required":["visible"]})");
}

ZEST_CASE(skip_and_default) {
    const auto result = json::schema_string<skip_default>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("count":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647,"default":0}},)"
                     R"("required":["name"]})");
}

// A field the encoder may omit (built-in skip_when or a custom skip_if
// predicate) is never required — the decoder accepts its absence.
ZEST_CASE(skip_if_fields_not_required) {
    const auto result = json::schema_string<with_skip_when>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("tags":{"type":"array",)"
                     R"("items":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("count":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("note":{"type":"string"}},)"
                     R"("required":["name"]})");
}

// Two KOTATSU_ANNOTATE uses expand to distinct tags; identical untagged
// struct specs must still collapse to one type_info instance and $defs entry.
ZEST_CASE(repeated_inline_struct_annotation_shares_def) {
    const auto result = json::schema_string<repeated_child_annotation>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("left":{"$ref":"#/$defs/casing_child"},)"
                     R"("right":{"$ref":"#/$defs/casing_child"}},)"
                     R"("required":["left","right"],)"
                     R"("$defs":{)"
                     R"("casing_child":{"type":"object",)"
                     R"("properties":{)"
                     R"("firstValue":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["firstValue"]}}})");
}

// ---------------------------------------------------------------------------
// flatten
// ---------------------------------------------------------------------------

ZEST_CASE(attr_flatten) {
    const auto result = json::schema_string<with_flatten>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("a":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("b":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("extra":{"type":"string"}},)"
                     R"("required":["a","b","extra"]})");
}

ZEST_CASE(flatten_with_optional) {
    const auto result = json::schema_string<flatten_opt>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"anyOf":[{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"null"}],"default":null},)"
                     R"("tag":{"type":"string"}},)"
                     R"("required":["x","tag"]})");
}

ZEST_CASE(flatten_with_rename) {
    const auto result = json::schema_string<flatten_rename>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("alpha":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("b":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("extra":{"type":"string"}},)"
                     R"("required":["alpha","b","extra"]})");
}

// ---------------------------------------------------------------------------
// rename
// ---------------------------------------------------------------------------

ZEST_CASE(attr_rename) {
    const auto result = json::schema_string<with_rename>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("my_field":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"string"}},)"
                     R"("required":["my_field","y"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::none)
// ---------------------------------------------------------------------------

ZEST_CASE(variant_untagged) {
    const auto result = json::schema_string<var_none>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"anyOf":[)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"string"}]}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(variant_three_alts) {
    const auto result = json::schema_string<var_three>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"anyOf":[)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"string"},)"
                     R"({"type":"boolean"}]}},)"
                     R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::external)
// ---------------------------------------------------------------------------

ZEST_CASE(variant_external_tag) {
    const static type_info_fn ext_alts[] = {
        type_info_of<std::int32_t>,
        type_info_of<std::string>,
    };
    const static std::string_view ext_names[] = {"num", "text"};
    const static variant_type_info ext_var = {
        {type_kind::variant, "ext_var"},
        {ext_alts, 2},
        tag_mode::external,
        {},
        {},
        {ext_names, 2},
    };
    const static type_info_fn ext_var_ref = []() -> const type_info& {
        return ext_var;
    };
    const static field_info ext_field = {
        "v",
        {},
        0,
        0,
        ext_var_ref,
        false,
        false,
        false,
    };
    const static struct_type_info ext_wrap = {
        {type_kind::structure, "ext_wrap"},
        false,
        false,
        {&ext_field,           1         },
    };
    const auto result = json::schema_string(ext_wrap).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"oneOf":[)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("num":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["num"],)"
                     R"("additionalProperties":false},)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("text":{"type":"string"}},)"
                     R"("required":["text"],)"
                     R"("additionalProperties":false}]}},)"
                     R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::internal)
// ---------------------------------------------------------------------------

ZEST_CASE(variant_internal_tag) {
    const static type_info_fn int_alts[] = {
        type_info_of<point2d>,
        type_info_of<inner>,
    };
    const static std::string_view int_names[] = {"point", "inner"};
    const static variant_type_info int_var = {
        {type_kind::variant, "int_var"},
        {int_alts, 2},
        tag_mode::internal,
        "type",
        {},
        {int_names, 2},
    };
    const static type_info_fn int_var_ref = []() -> const type_info& {
        return int_var;
    };
    const static field_info int_field = {
        "v",
        {},
        0,
        0,
        int_var_ref,
        false,
        false,
        false,
    };
    const static struct_type_info int_wrap = {
        {type_kind::structure, "int_wrap"},
        false,
        false,
        {&int_field,           1         },
    };
    const auto result = json::schema_string(int_wrap).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"oneOf":[)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                     R"("y":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                     R"("type":{"const":"point"}},)"
                     R"("required":["x","y","type"]},)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("a":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                     R"("type":{"const":"inner"}},)"
                     R"("required":["a","type"]}]}},)"
                     R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Variant (tag_mode::adjacent)
// ---------------------------------------------------------------------------

ZEST_CASE(variant_adjacent_tag) {
    const static type_info_fn adj_alts[] = {
        type_info_of<std::int32_t>,
        type_info_of<std::string>,
    };
    const static std::string_view adj_names[] = {"num", "text"};
    const static variant_type_info adj_var = {
        {type_kind::variant, "adj_var"},
        {adj_alts,           2        },
        tag_mode::adjacent,
        "t",
        "c",
        {adj_names,          2        },
    };
    const static type_info_fn adj_var_ref = []() -> const type_info& {
        return adj_var;
    };
    const static field_info adj_field = {
        "v",
        {},
        0,
        0,
        adj_var_ref,
        false,
        false,
        false,
    };
    const static struct_type_info adj_wrap = {
        {type_kind::structure, "adj_wrap"},
        false,
        false,
        {&adj_field,           1         },
    };
    const auto result = json::schema_string(adj_wrap).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"oneOf":[)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("t":{"const":"num"},)"
                     R"("c":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["t","c"],)"
                     R"("additionalProperties":false},)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("t":{"const":"text"},)"
                     R"("c":{"type":"string"}},)"
                     R"("required":["t","c"],)"
                     R"("additionalProperties":false}]}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(root_external_variant) {
    const auto result = json::schema_string<root_external_variant>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("oneOf":[)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("integer":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["integer"],)"
                     R"("additionalProperties":false},)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("text":{"type":"string"}},)"
                     R"("required":["text"],)"
                     R"("additionalProperties":false}]})");
}

ZEST_CASE(root_internal_variant) {
    const auto result = json::schema_string<root_internal_variant>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("oneOf":[)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("radius":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                     R"("kind":{"const":"circle"}},)"
                     R"("required":["radius","kind"]},)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("width":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                     R"("height":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                     R"("kind":{"const":"rect"}},)"
                     R"("required":["width","height","kind"]}]})");
}

ZEST_CASE(root_adjacent_variant) {
    const auto result = json::schema_string<root_adjacent_variant>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("oneOf":[)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("type":{"const":"integer"},)"
                     R"("value":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["type","value"],)"
                     R"("additionalProperties":false},)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("type":{"const":"text"},)"
                     R"("value":{"type":"string"}},)"
                     R"("required":["type","value"],)"
                     R"("additionalProperties":false}]})");
}

ZEST_CASE(opaque_root_returns_error) {
    const auto result = json::schema_string<json_schema_opaque_root>();
    EXPECT(!result);
}

ZEST_CASE(any_type_root) {
    const static type_info any_ti = {type_kind::any, "any"};
    const auto result = json::schema_string(any_ti).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema"})");
}

ZEST_CASE(any_type_field) {
    const static type_info any_ti = {type_kind::any, "any"};
    const static field_info any_fields[] = {
        {"data", {}, 0, 0, []() -> const type_info& { return any_ti; }, false, false, false},
    };
    const static struct_type_info any_struct = {
        {type_kind::structure, "with_any"},
        false,
        false,
        {any_fields,           1         },
    };
    const auto result = json::schema_string(any_struct).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{"data":{}},)"
                     R"("required":["data"]})");
}

// ---------------------------------------------------------------------------
// More containers
// ---------------------------------------------------------------------------

ZEST_CASE(map_str_struct) {
    const auto result = json::schema_string<map_str_struct>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("entries":{"type":"object",)"
                     R"("additionalProperties":{)"
                     R"("$ref":"#/$defs/point2d"}}},)"
                     R"("required":["entries"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

ZEST_CASE(map_str_enum) {
    const auto result = json::schema_string<map_str_enum>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("entries":{"type":"object",)"
                     R"("additionalProperties":{)"
                     R"("type":"integer","minimum":-128,"maximum":127}}},)"
                     R"("required":["entries"]})");
}

ZEST_CASE(vec_optional_items) {
    const auto result = json::schema_string<vec_optional>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"type":"array",)"
                     R"("items":{"anyOf":[{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"null"}]}}},)"
                     R"("required":["v"]})");
}

ZEST_CASE(optional_vec_field) {
    const auto result = json::schema_string<optional_vec>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"anyOf":[{"type":"array",)"
                     R"("items":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"({"type":"null"}],"default":null}}})");
}

ZEST_CASE(vec_of_enum) {
    const auto result = json::schema_string<vec_enum>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("colors":{"type":"array",)"
                     R"("items":{)"
                     R"("type":"integer","minimum":-128,"maximum":127}}},)"
                     R"("required":["colors"]})");
}

ZEST_CASE(set_of_string) {
    const auto result = json::schema_string<set_string>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("tags":{"type":"array",)"
                     R"("items":{"type":"string"},)"
                     R"("uniqueItems":true}},)"
                     R"("required":["tags"]})");
}

ZEST_CASE(vec_of_map) {
    const auto result = json::schema_string<vec_map>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("items":{"type":"array",)"
                     R"("items":{"type":"object",)"
                     R"("additionalProperties":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}}},)"
                     R"("required":["items"]})");
}

ZEST_CASE(map_of_vec_struct) {
    const auto result = json::schema_string<map_vec_struct>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("groups":{"type":"object",)"
                     R"("additionalProperties":{"type":"array",)"
                     R"("items":{)"
                     R"("$ref":"#/$defs/point2d"}}}},)"
                     R"("required":["groups"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

ZEST_CASE(deep_container_field) {
    const auto result = json::schema_string<deep_container>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("data":{"type":"object",)"
                     R"("additionalProperties":{"type":"array",)"
                     R"("items":{"type":"object",)"
                     R"("additionalProperties":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}}}},)"
                     R"("required":["data"]})");
}

ZEST_CASE(map_of_map_field) {
    const auto result = json::schema_string<map_of_map>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("m":{"type":"object",)"
                     R"("additionalProperties":{"type":"object",)"
                     R"("additionalProperties":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}}},)"
                     R"("required":["m"]})");
}

// ---------------------------------------------------------------------------
// Struct with pointer to struct
// ---------------------------------------------------------------------------

ZEST_CASE(shared_ptr_to_struct) {
    const auto result = json::schema_string<shared_struct>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("point":{"anyOf":[{)"
                     R"("$ref":"#/$defs/point2d"},)"
                     R"({"type":"null"}],"default":null}},)"
                     R"("required":["name"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

ZEST_CASE(optional_struct_field) {
    const auto result = json::schema_string<optional_struct>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("point":{"anyOf":[{)"
                     R"("$ref":"#/$defs/point2d"},)"
                     R"({"type":"null"}],"default":null},)"
                     R"("name":{"type":"string"}},)"
                     R"("required":["name"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

// ---------------------------------------------------------------------------
// $defs dedup
// ---------------------------------------------------------------------------

ZEST_CASE(defs_dedup_multi_ref) {
    const auto result = json::schema_string<multi_ref>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("a":{)"
                     R"("$ref":"#/$defs/point2d"},)"
                     R"("b":{)"
                     R"("$ref":"#/$defs/point2d"},)"
                     R"("list":{"type":"array",)"
                     R"("items":{)"
                     R"("$ref":"#/$defs/point2d"}}},)"
                     R"("required":["a","b","list"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

// ---------------------------------------------------------------------------
// Struct with enum fields
// ---------------------------------------------------------------------------

ZEST_CASE(multi_enum_fields) {
    const auto result = json::schema_string<multi_enum>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("c":{"type":"integer","minimum":-128,"maximum":127},)"
                     R"("s":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                     R"("label":{"type":"string"}},)"
                     R"("required":["c","s","label"]})");
}

ZEST_CASE(with_flag_enum) {
    const auto result = json::schema_string<with_flag>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("f":{"type":"integer","minimum":0,"maximum":255},)"
                     R"("name":{"type":"string"}},)"
                     R"("required":["f","name"]})");
}

ZEST_CASE(with_level_enum) {
    const auto result = json::schema_string<with_level>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("l":{"type":"integer","minimum":-32768,"maximum":32767},)"
                     R"("v":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["l","v"]})");
}

// ---------------------------------------------------------------------------
// Nested struct with optional
// ---------------------------------------------------------------------------

ZEST_CASE(optional_inner_field) {
    const auto result = json::schema_string<optional_inner>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("i":{"anyOf":[{)"
                     R"("$ref":"#/$defs/inner"},)"
                     R"({"type":"null"}],"default":null},)"
                     R"("name":{"type":"string"}},)"
                     R"("required":["name"],)"
                     R"("$defs":{)"
                     R"("inner":{"type":"object",)"
                     R"("properties":{)"
                     R"("a":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["a"]}}})");
}

// ---------------------------------------------------------------------------
// Variant in container
// ---------------------------------------------------------------------------

ZEST_CASE(vec_of_variant) {
    const auto result = json::schema_string<vec_variant>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("items":{"type":"array",)"
                     R"("items":{"anyOf":[)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"string"}]}}},)"
                     R"("required":["items"]})");
}

// ---------------------------------------------------------------------------
// Combinations
// ---------------------------------------------------------------------------

ZEST_CASE(combo_mixed_fields) {
    const auto result = json::schema_string<combo>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("color":{)"
                     R"("type":"integer","minimum":-128,"maximum":127},)"
                     R"("label":{"anyOf":[{"type":"string"},)"
                     R"({"type":"null"}],"default":null},)"
                     R"("values":{"type":"array",)"
                     R"("items":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("attrs":{"type":"object",)"
                     R"("additionalProperties":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}},)"
                     R"("required":["color","values","attrs"]})");
}

ZEST_CASE(combo_nested_struct_refs) {
    const auto result = json::schema_string<nested_combo>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("point":{)"
                     R"("$ref":"#/$defs/point2d"},)"
                     R"("color":{)"
                     R"("type":"integer","minimum":-128,"maximum":127},)"
                     R"("points":{"type":"array",)"
                     R"("items":{)"
                     R"("$ref":"#/$defs/point2d"}},)"
                     R"("named_points":{"type":"object",)"
                     R"("additionalProperties":{)"
                     R"("$ref":"#/$defs/point2d"}}},)"
                     R"("required":[)"
                     R"("point","color","points","named_points"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

ZEST_CASE(combo_vec_of_struct) {
    const auto result = json::schema_string<vec_of_struct>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("items":{"type":"array",)"
                     R"("items":{)"
                     R"("$ref":"#/$defs/point2d"}}},)"
                     R"("required":["items"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

ZEST_CASE(combo_deep_nesting) {
    const auto result = json::schema_string<deep_outer>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("dm":{)"
                     R"("$ref":"#/$defs/deep_middle"},)"
                     R"("n":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["dm","n"],)"
                     R"("$defs":{)"
                     R"("deep_inner":{"type":"object",)"
                     R"("properties":{)"
                     R"("c":{)"
                     R"("type":"integer","minimum":-128,"maximum":127},)"
                     R"("v":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["c","v"]},)"
                     R"("deep_middle":{"type":"object",)"
                     R"("properties":{)"
                     R"("di":{)"
                     R"("$ref":"#/$defs/deep_inner"},)"
                     R"("s":{"type":"string"}},)"
                     R"("required":["di","s"]}}})");
}

ZEST_CASE(combo_multi_map) {
    const auto result = json::schema_string<multi_map>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("a":{"type":"object",)"
                     R"("additionalProperties":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("b":{"type":"object",)"
                     R"("additionalProperties":{"type":"string"}}},)"
                     R"("required":["a","b"]})");
}

ZEST_CASE(combo_many_fields) {
    const auto result = json::schema_string<many_fields>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("a":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("b":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("c":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("d":{"type":"string"},)"
                     R"("e":{"type":"boolean"},)"
                     R"("f":{"anyOf":[{"type":"number"},{"type":"null"}]}},)"
                     R"("required":["a","b","c","d","e","f"]})");
}

ZEST_CASE(combo_set_of_struct) {
    const auto result = json::schema_string<set_of_struct>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("ids":{"type":"array",)"
                     R"("items":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("uniqueItems":true},)"
                     R"("name":{"type":"string"}},)"
                     R"("required":["ids","name"]})");
}

ZEST_CASE(combo_trivial_nested) {
    const auto result = json::schema_string<trivial_nested>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("p":{)"
                     R"("$ref":"#/$defs/point2d"},)"
                     R"("z":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["p","z"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

// ---------------------------------------------------------------------------
// Self-referential struct
// ---------------------------------------------------------------------------

ZEST_CASE(self_referential_struct) {
    static struct_type_info self_info = {
        {type_kind::structure, "self_ref"},
        false,
        false,
        {},
    };
    const static optional_type_info opt_self = {
        {type_kind::optional, "optional<self_ref>"},
        []() -> const type_info& { return self_info; },
    };
    const static field_info self_fields[] = {
        {"value", {}, 0, 0, type_info_of<std::int32_t>, false, false, false},
        {"next",
         {},
         0, 1,
         []() -> const type_info& { return opt_self; },
         false, false,
         false, true},
    };
    self_info.fields = {self_fields, 2};

    const auto result = json::schema_string(self_info).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("value":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                     R"("next":{"anyOf":[{"$ref":"#"},{"type":"null"}]}},)"
                     R"("required":["value"]})");
}

// ---------------------------------------------------------------------------
// Empty enum
// ---------------------------------------------------------------------------

ZEST_CASE(empty_enum) {
    const static enum_type_info empty_ei = {
        {type_kind::enumeration, "empty_enum"},
        {},
        nullptr,
        type_kind::int32,
    };
    const auto result = json::schema_string(empty_ei).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"integer","minimum":-2147483648,"maximum":2147483647})");
}

// ---------------------------------------------------------------------------
// Variant nesting variant
// ---------------------------------------------------------------------------

ZEST_CASE(variant_of_variant) {
    const static type_info_fn inner_alts[] = {
        type_info_of<std::string>,
        type_info_of<bool>,
    };
    const static variant_type_info inner_var = {
        {type_kind::variant, "inner_variant"},
        {inner_alts, 2},
        tag_mode::none,
        {},
        {},
        {},
    };

    const static type_info_fn outer_alts[] = {
        type_info_of<std::int32_t>,
        []() -> const type_info& { return inner_var; },
    };
    const static variant_type_info outer_var = {
        {type_kind::variant, "outer_variant"},
        {outer_alts, 2},
        tag_mode::none,
        {},
        {},
        {},
    };

    const auto result = json::schema_string(outer_var).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("anyOf":[)"
                     R"({"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                     R"({"anyOf":[)"
                     R"({"type":"string"},)"
                     R"({"type":"boolean"}]}]})");
}

// ---------------------------------------------------------------------------
// Field ordering stability
// ---------------------------------------------------------------------------

ZEST_CASE(field_ordering_stability) {
    const static field_info ordered_fields[] = {
        {"zebra",  {}, 0, 0, type_info_of<std::string>,  false, false, false},
        {"alpha",  {}, 0, 1, type_info_of<std::int32_t>, false, false, false},
        {"middle", {}, 0, 2, type_info_of<bool>,         false, false, false},
        {"beta",   {}, 0, 3, type_info_of<double>,       false, false, false},
    };
    const static struct_type_info ordered_info = {
        {type_kind::structure, "ordered_struct"},
        false,
        false,
        {ordered_fields,       4               },
    };
    const auto result = json::schema_string(ordered_info).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("zebra":{"type":"string"},)"
                     R"("alpha":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                     R"("middle":{"type":"boolean"},)"
                     R"("beta":{"anyOf":[{"type":"number"},{"type":"null"}]}},)"
                     R"("required":["zebra","alpha","middle","beta"]})");
}

// ---------------------------------------------------------------------------
// Variant with monostate
// ---------------------------------------------------------------------------

struct with_monostate {
    std::variant<std::monostate, std::int32_t, std::string> v;
};

ZEST_CASE(variant_with_monostate) {
    const auto result = json::schema_string<with_monostate>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("v":{"anyOf":[)"
                     R"({"type":"null"},)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"string"}]}},)"
                     R"("required":["v"]})");
}

// ---------------------------------------------------------------------------
// Mutual recursion
// ---------------------------------------------------------------------------

ZEST_CASE(mutual_recursion) {
    static struct_type_info info_a = {
        {type_kind::structure, "node_a"},
        false,
        false,
        {}
    };
    static struct_type_info info_b = {
        {type_kind::structure, "node_b"},
        false,
        false,
        {}
    };

    const static optional_type_info opt_b = {
        {type_kind::optional, "optional<node_b>"},
        []() -> const type_info& { return info_b; },
    };
    const static optional_type_info opt_a = {
        {type_kind::optional, "optional<node_a>"},
        []() -> const type_info& { return info_a; },
    };

    const static field_info fields_a[] = {
        {"value", {}, 0, 0, type_info_of<std::int32_t>, false, false, false},
        {"b", {}, 0, 1, []() -> const type_info& { return opt_b; }, false, false, false, true},
    };
    const static field_info fields_b[] = {
        {"name", {}, 0, 0, type_info_of<std::string>, false, false, false},
        {"a", {}, 0, 1, []() -> const type_info& { return opt_a; }, false, false, false, true},
    };
    info_a.fields = {fields_a, 2};
    info_b.fields = {fields_b, 2};

    const auto result = json::schema_string(info_a).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("value":{"type":"integer","minimum":-2147483648,"maximum":2147483647},)"
                     R"("b":{"anyOf":[{"$ref":"#/$defs/node_b"},{"type":"null"}]}},)"
                     R"("required":["value"],)"
                     R"("$defs":{)"
                     R"("node_b":{"type":"object",)"
                     R"("properties":{)"
                     R"("name":{"type":"string"},)"
                     R"("a":{"anyOf":[{"$ref":"#"},{"type":"null"}]}},)"
                     R"("required":["name"]}}})");
}

// ---------------------------------------------------------------------------
// Bytes type
// ---------------------------------------------------------------------------

ZEST_CASE(bytes_field) {
    const static type_info bytes_ti = {type_kind::bytes, "bytes"};
    const static field_info bytes_fields[] = {
        {"data", {}, 0, 0, []() -> const type_info& { return bytes_ti; }, false, false, false},
    };
    const static struct_type_info bytes_struct = {
        {type_kind::structure, "with_bytes"},
        false,
        false,
        {bytes_fields,         1           },
    };
    const auto result = json::schema_string(bytes_struct).value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("data":{"type":"array",)"
                     R"("items":{"type":"integer",)"
                     R"("minimum":0,)"
                     R"("maximum":255}}},)"
                     R"("required":["data"]})");
}

// ---------------------------------------------------------------------------
// description
// ---------------------------------------------------------------------------

ZEST_CASE(description_on_scalar_field) {
    const auto result = json::schema_string<desc_scalar>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("threads":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647,)"
                     R"("description":"Number of worker threads."},)"
                     R"("name":{"type":"string"}},)"
                     R"("required":["threads","name"]})");
}

ZEST_CASE(description_on_optional_field) {
    const auto result = json::schema_string<desc_optional>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("label":{"anyOf":[{"type":"string"},)"
                     R"({"type":"null"}],)"
                     R"("description":"Optional display label.","default":null}}})");
}

ZEST_CASE(description_on_struct_ref_field) {
    const auto result = json::schema_string<desc_struct_ref>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("anchor":{"$ref":"#/$defs/point2d",)"
                     R"("description":"Anchor position."}},)"
                     R"("required":["anchor"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

ZEST_CASE(description_through_flatten) {
    const auto result = json::schema_string<desc_flatten>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("count":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647,)"
                     R"("description":"Inherited counter."},)"
                     R"("tag":{"type":"string"}},)"
                     R"("required":["count","tag"]})");
}

ZEST_CASE(description_with_rename) {
    const auto result = json::schema_string<desc_rename>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("max_size":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647,)"
                     R"("description":"Maximum size in bytes."}},)"
                     R"("required":["max_size"]})");
}

ZEST_CASE(description_with_default_value) {
    const auto result = json::schema_string<desc_default>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("retries":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647,)"
                     R"("description":"Retry limit.","default":0}}})");
}

ZEST_CASE(described_and_bare_struct_share_def) {
    const auto result = json::schema_string<desc_shared_ref>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("origin":{"$ref":"#/$defs/point2d"},)"
                     R"("anchor":{"$ref":"#/$defs/point2d",)"
                     R"("description":"Anchor position."}},)"
                     R"("required":["origin","anchor"],)"
                     R"("$defs":{)"
                     R"("point2d":{"type":"object",)"
                     R"("properties":{)"
                     R"("x":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"("y":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["x","y"]}}})");
}

ZEST_CASE(description_in_internal_tagged_alternative) {
    const auto result = json::schema_string<desc_internal_variant>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("oneOf":[)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("radius":{"anyOf":[{"type":"number"},{"type":"null"}],)"
                     R"("description":"Radius in meters."},)"
                     R"("kind":{"const":"circle"}},)"
                     R"("required":["radius","kind"]},)"
                     R"({"type":"object",)"
                     R"("properties":{)"
                     R"("width":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                     R"("height":{"anyOf":[{"type":"number"},{"type":"null"}]},)"
                     R"("kind":{"const":"rect"}},)"
                     R"("required":["width","height","kind"]}]})");
}

// ---------------------------------------------------------------------------
// enum representation config
// ---------------------------------------------------------------------------

struct string_enum_config {
    [[maybe_unused]] constexpr static auto enum_repr = codec::enum_repr::String;
};

ZEST_CASE(enum_names_under_string_config) {
    const auto result = json::schema_string<color_i8, string_enum_config>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("enum":["red","green","blue"]})");
}

ZEST_CASE(unnamed_enum_value_rejected_under_string_config) {
    // A representable value without a reflected member name has no string
    // spelling: the encoder rejects it, so the schema's enum list of
    // reflected names stays exhaustive.
    const auto encoded = json::to_string<string_enum_config>(static_cast<color_i8>(42));
    EXPECT(!encoded);
}

ZEST_CASE(schema_agrees_with_encoder_on_enums) {
    // Under the default config the encoder emits the numeric value, so the
    // schema constrains the same numeric form.
    const auto encoded = json::to_string(with_enum{.c = color_i8::green, .name = "g"});
    ASSERT(encoded);
    EXPECT(zest::contains(*encoded, R"("c":1)"));

    const auto schema = json::schema_string<with_enum>().value();
    EXPECT(zest::contains(schema, R"("c":{"type":"integer","minimum":-128,"maximum":127})"));
}

struct renamed_enum_config {
    [[maybe_unused]] constexpr static auto enum_repr = codec::enum_repr::String;
    using enum_rename = naming::rename_policy::upper_snake;
};

ZEST_CASE(schema_agrees_with_encoder_on_enum_rename) {
    // The schema lists the spellings the encoder writes under the same
    // config, not the raw reflected names.
    const auto encoded = json::to_string<renamed_enum_config>(color_i8::green);
    ASSERT(encoded);
    EXPECT(*encoded == R"("GREEN")");

    const auto result = json::schema_string<color_i8, renamed_enum_config>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("enum":["RED","GREEN","BLUE"]})");
}

// ---------------------------------------------------------------------------
// nan_repr config
// ---------------------------------------------------------------------------

struct nan_null_config {
    [[maybe_unused]] constexpr static auto nan_repr = codec::nan_repr::Null;
};

struct nan_string_config {
    [[maybe_unused]] constexpr static auto nan_repr = codec::nan_repr::String;
};

ZEST_CASE(schema_agrees_with_encoder_on_nan_passthrough) {
    // The default Passthrough forwards the non-finite value to the writer,
    // whose only JSON spelling for it is null — the schema must admit that.
    const auto encoded = json::to_string(std::numeric_limits<double>::quiet_NaN());
    ASSERT(encoded);
    EXPECT(*encoded == "null");

    const auto result = json::schema_string<double>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("anyOf":[{"type":"number"},{"type":"null"}]})");
}

ZEST_CASE(schema_agrees_with_encoder_on_nan_null) {
    const auto encoded = json::to_string<nan_null_config>(std::numeric_limits<double>::quiet_NaN());
    ASSERT(encoded);
    EXPECT(*encoded == "null");

    const auto result = json::schema_string<double, nan_null_config>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("anyOf":[{"type":"number"},{"type":"null"}]})");
}

ZEST_CASE(schema_agrees_with_encoder_on_nan_string) {
    const auto encoded = json::to_string<nan_string_config>(std::numeric_limits<float>::infinity());
    ASSERT(encoded);
    EXPECT(*encoded == R"("Infinity")");

    const auto result = json::schema_string<float, nan_string_config>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("anyOf":[{"type":"number"},)"
                     R"({"enum":["NaN","Infinity","-Infinity"]}]})");
}

struct nan_error_config {
    [[maybe_unused]] constexpr static auto nan_repr = codec::nan_repr::Error;
};

ZEST_CASE(schema_agrees_with_encoder_on_long_double_overflow) {
    // A finite long double beyond double's range narrows to infinity in the
    // document, so the nan_repr policy judges the narrowed value: String
    // spells it, Error rejects it — never a null the schema does not admit.
    constexpr long double big = std::numeric_limits<long double>::max();
    if constexpr(big > static_cast<long double>(std::numeric_limits<double>::max())) {
        const auto spelled = json::to_string<nan_string_config>(big);
        ASSERT(spelled);
        EXPECT(*spelled == R"("Infinity")");

        const auto negative = json::to_string<nan_string_config>(-big);
        ASSERT(negative);
        EXPECT(*negative == R"("-Infinity")");

        EXPECT(!json::to_string<nan_error_config>(big).has_value());
    } else {
        // long double is double: the value stays a finite number.
        EXPECT(json::to_string<nan_error_config>(big).has_value());
    }
}

// ---------------------------------------------------------------------------
// human_readable config
// ---------------------------------------------------------------------------

struct non_hr_config {
    [[maybe_unused]] constexpr static bool human_readable = false;
};

ZEST_CASE(schema_agrees_with_encoder_on_non_human_readable) {
    // A non-human-readable config bypasses tagging and encodes the underlying
    // variant, so the schema describes the untagged alternatives.
    const auto encoded = json::to_string<non_hr_config>(root_external_variant{7});
    ASSERT(encoded);
    EXPECT(*encoded == "7");

    const auto result = json::schema_string<root_external_variant, non_hr_config>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("anyOf":[)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647},)"
                     R"({"type":"string"}]})");
}

// ---------------------------------------------------------------------------
// overlapping untagged alternatives
// ---------------------------------------------------------------------------

ZEST_CASE(untagged_overlap_validates_as_any_of) {
    // A numeric enum's underlying range overlaps the int alternative: both
    // branches match the same document, so exactly-one (oneOf) semantics
    // would reject every value the encoder emits — anyOf must apply.
    using overlapping = std::variant<color_i8, std::int32_t>;
    const auto result = json::schema_string<overlapping>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("anyOf":[)"
                     R"({"type":"integer","minimum":-128,"maximum":127},)"
                     R"({"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}]})");
}

// ---------------------------------------------------------------------------
// metadata config forwarding
// ---------------------------------------------------------------------------

struct camel_deny_config {
    using field_rename = naming::rename_policy::lower_camel;
    [[maybe_unused]] constexpr static bool deny_unknown_fields = true;
};

ZEST_CASE(schema_agrees_with_encoder_on_config) {
    // The schema must accept what to_string under the same config emits:
    // renamed field names and the unknown-field policy.
    const auto encoded = json::to_string<camel_deny_config>(casing_child{.first_value = 7});
    ASSERT(encoded);
    EXPECT(*encoded == R"({"firstValue":7})");

    const auto result = json::schema_string<casing_child, camel_deny_config>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("firstValue":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}},)"
                     R"("required":["firstValue"],)"
                     R"("additionalProperties":false})");
}

// ---------------------------------------------------------------------------
// default annotations from a default-constructed instance
// ---------------------------------------------------------------------------

ZEST_CASE(defaults_annotated) {
    const auto result = json::schema_string<defaults_root>().value();
    // Non-required root fields carry the value a default-constructed
    // instance encodes.
    EXPECT(zest::contains(result, R"("enabled":{"type":"boolean","default":true})"));
    EXPECT(zest::contains(result, R"({"type":"null"}],"default":null})"));
    EXPECT(zest::contains(result, R"("default":[]})"));
    // The shared defaults_leaf $def is annotated once, inside $defs; the ref
    // sites stay bare.
    EXPECT(zest::contains(result, R"("pool":{"$ref":"#/$defs/defaults_leaf"})"));
    EXPECT(zest::contains(result, R"("mirror":{"$ref":"#/$defs/defaults_leaf"})"));
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    EXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
}

ZEST_CASE(defaults_skip_condition) {
    // The empty vector triggers skip_if at encode time, so the default
    // document has no such property to annotate from.
    const auto result = json::schema_string<defaults_skipped>().value();
    EXPECT(zest::contains(result, R"("tags")"));
    EXPECT(!zest::contains(result, R"("default")"));
}

ZEST_CASE(defaults_shared_def_shows_fresh_values) {
    // The shared $def body always describes a fresh defaults_leaf — threads
    // 4, name "worker" — even though mirror's member initializer overrides
    // threads to 9 at its site: both sites are required, so the override has
    // no schema position and is not represented.
    const auto result = json::schema_string<defaults_shared_override>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    EXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
    EXPECT(!zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_non_required_ref_sites) {
    // Non-required refs to a shared $def each carry their whole encoded
    // object as the property default, so the per-site member initializer
    // survives and takes precedence at its site; the $def body keeps
    // describing a fresh defaults_leaf.
    const auto result = json::schema_string<defaults_ref_sites>().value();
    EXPECT(zest::contains(
        result,
        R"("pool":{"$ref":"#/$defs/defaults_leaf","default":{"threads":4,"name":"worker"}})"));
    EXPECT(zest::contains(
        result,
        R"("mirror":{"$ref":"#/$defs/defaults_leaf","default":{"threads":9,"name":"worker"}})"));
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    EXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
}

ZEST_CASE(defaults_engaged_override_carries_site_default) {
    // The nullable site carries the enclosing instance's whole encoded
    // object — the engaged override rides the site default, which takes
    // precedence there — while the $def body keeps describing a fresh
    // defaults_leaf.
    const auto result = json::schema_string<defaults_engaged_override>().value();
    EXPECT(zest::contains(result, R"("default":{"threads":9,"name":"worker"})"));
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    EXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
}

ZEST_CASE(defaults_in_place_override_not_represented) {
    // The override sits two required in-place levels down (root member
    // initializer → mid.leaf.threads): required sites carry no site default,
    // so the override has no schema position — the leaf $def stays exact for
    // a fresh defaults_leaf.
    const auto result = json::schema_string<defaults_cascade_root>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    EXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
    EXPECT(!zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_tuple_element_def_shows_fresh_values) {
    // Tuple elements have no per-element default position, so the required
    // entry's override is not represented; the element type's $def describes
    // a fresh defaults_leaf.
    const auto result = json::schema_string<defaults_tuple_override>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    EXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
    EXPECT(!zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_nullable_root) {
    // A nullable root unwraps to the struct body, but its default instance
    // encodes to null — a document with no properties to annotate from, not
    // a crash.
    const auto opt = json::schema_string<std::optional<defaults_leaf>>().value();
    EXPECT(zest::contains(opt, R"("threads")"));
    EXPECT(!zest::contains(opt, R"("default")"));

    const auto ptr = json::schema_string<std::unique_ptr<defaults_leaf>>().value();
    EXPECT(!zest::contains(ptr, R"("default")"));
}

ZEST_CASE(defaults_engaged_optional) {
    // An engaged optional lands its whole encoded value as the default on
    // the anyOf wrapper, and the struct's $def — reachable only through the
    // nullable field — still carries the fresh instance's own defaults.
    const auto result = json::schema_string<defaults_engaged>().value();
    EXPECT(zest::contains(result, R"("default":5)"));
    EXPECT(zest::contains(result, R"("default":{"threads":4,"name":"worker"})"));
    EXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
}

ZEST_CASE(defaults_recursive_root) {
    // A self-referential root terminates: the pointer self-reference sits
    // inside an anyOf wrapper, which is a leaf for the walk, and carries the
    // disengaged pointer's null.
    const auto result = json::schema_string<defaults_node>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":1})"));
    EXPECT(zest::contains(result,
                          R"("next":{"anyOf":[{"$ref":"#"},{"type":"null"}],"default":null})"));
}

ZEST_CASE(defaults_internal_tagged_variant_member) {
    // Every internal-tagged branch takes its defaults from a freshly
    // constructed alternative — what decode emplaces before reading fields —
    // so selected and unselected alternatives alike keep their own
    // initializers.
    const auto result = json::schema_string<defaults_variant_holder>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    EXPECT(zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_tagged_variant_root) {
    // The same applies to a tagged variant at the root, whose oneOf is
    // merged into the top-level schema object.
    const auto result = json::schema_string<defaults_internal_variant>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    EXPECT(zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_adjacent_tagged_variant) {
    // Adjacent tagging routes each alternative behind the content property,
    // a struct $ref: both alternatives' $defs carry their fresh defaults.
    const auto result = json::schema_string<defaults_adjacent_variant>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    EXPECT(zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_external_tagged_variant) {
    // External tagging nests each alternative's schema behind its name; the
    // struct $defs under both branches carry their fresh defaults.
    const auto result = json::schema_string<defaults_external_variant>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    EXPECT(zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_container_elements) {
    // Required containers carry no site default, but struct $defs reachable
    // only through containers — array elements, tuple slots, map values —
    // still carry the fresh defaults of their own types.
    const auto result = json::schema_string<defaults_containers>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":7})"));
    EXPECT(zest::contains(result, R"("beta":{"type":"string","default":"cell"})"));
    EXPECT(zest::contains(result, R"("gamma":{"type":"boolean","default":true})"));
    // The required container properties themselves stay bare.
    EXPECT(zest::contains(result,
                          R"("pool":{"type":"array","items":{"$ref":"#/$defs/defaults_elem_a"}})"));
}

ZEST_CASE(defaults_container_root) {
    // A container root merges its schema shape (std::array reflects as a
    // tuple: prefixItems) into the top-level object; the element $def still
    // carries its fresh defaults.
    const auto result = json::schema_string<std::array<defaults_elem_a, 2>>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":7})"));
}

ZEST_CASE(defaults_sequence_element_override_stays_local) {
    // decode value-initializes every sequence element before reading its
    // fields, so the shared $def carries defaults_leaf's own initializers —
    // the root's per-element override must not leak into it.
    const auto result = json::schema_string<defaults_seq_override>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    EXPECT(!zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_variant_holder_override_stays_local) {
    // decode emplaces a fresh alternative before reading its fields, so the
    // branch carries defaults_alt_a's own initializer — the holder's member
    // initializer must not leak into the branch default.
    const auto result = json::schema_string<defaults_variant_override>().value();
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    EXPECT(!zest::contains(result, R"("default":8)"));
}

ZEST_CASE(defaults_recursive_root_with_elements) {
    // A default instance may carry elements of the root type itself (the
    // element initializer bottoms out by overriding kids to empty); the
    // pass never follows the emitted root self-reference, so it terminates
    // and the per-element override leaks into no default.
    const auto result = json::schema_string<defaults_cyclic>().value();
    EXPECT(zest::contains(result, R"("$ref":"#")"));
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":1})"));
    EXPECT(!zest::contains(result, R"("default":2)"));
}

ZEST_CASE(defaults_repr_backed_root_unannotated) {
    // The decoder reads the representation, not a json_schema_reprd_root:
    // the defaults pass covers only types decode reads directly, so the
    // repr-routed root keeps the representation's schema shape without any
    // default.
    const auto result = json::schema_string<json_schema_reprd_root>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("total":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}})");
}

ZEST_CASE(defaults_declarative_repr_unannotated) {
    // Encode and decode disagree behind the repr (a fresh root encodes
    // n = 9, decode value-initializes n = 4): rather than guess, the
    // repr-routed root carries no default at all.
    const auto result = json::schema_string<json_schema_reprd_shifted>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("n":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}})");
}

ZEST_CASE(defaults_follow_slot_rename_all) {
    // A rename_all spec on the field slot renames the child $def's
    // properties; the fresh document is encoded under the same merged
    // config, so the renamed property still pairs with its default.
    const auto result = json::schema_string<renamed_defaults_holder>().value();
    EXPECT(zest::contains(result, R"("firstValue")"));
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
}

ZEST_CASE(defaults_annotated_root_rename_all) {
    // A structural annotation on the root itself shapes the fresh document
    // the same way it shapes the schema: the fresh root encodes under the
    // resolution's merged config, so the renamed property still pairs with
    // its default.
    const auto result = json::schema_string<renamed_defaults_root>().value();
    EXPECT(zest::contains(result, R"("firstValue")"));
    EXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    EXPECT(!zest::contains(result, R"("first_value")"));
}

ZEST_CASE(defaults_imperative_repr_unannotated) {
    // repr_decode's imperative branch never constructs the declared
    // representation — deserialize reads the caller's value in place — so
    // what an absent property leaves behind is the repr's business and the
    // repr-routed root carries no default.
    const auto result = json::schema_string<json_schema_imperative_root>().value();
    EXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                     R"("type":"object",)"
                     R"("properties":{)"
                     R"("n":{"type":"integer",)"
                     R"("minimum":-2147483648,)"
                     R"("maximum":2147483647}}})");
}

struct defaults_enum_config {
    [[maybe_unused]] constexpr static auto enum_repr = codec::enum_repr::String;
    using field_rename = naming::rename_policy::lower_camel;
};

ZEST_CASE(defaults_enum_and_rename_with_config) {
    // The default rides through the real encoder: the enum's String repr and
    // the field rename both shape the annotated value and its property name.
    const auto result = json::schema_string<defaults_with_enum, defaults_enum_config>().value();
    EXPECT(zest::contains(result, R"("logLevel":{"enum":["Low","High"],"default":"High"})"));
}

ZEST_CASE(defaults_type_erased_absent) {
    // The type-erased entry has no T to default-construct, so no defaults.
    const auto result = json::schema_string(type_info_of<defaults_root>()).value();
    EXPECT(!zest::contains(result, R"("default")"));
}

};  // ZEST_SUITE(codec_json_schema)

}  // namespace

}  // namespace kota::meta
