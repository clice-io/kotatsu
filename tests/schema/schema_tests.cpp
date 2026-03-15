#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include "eventide/zest/zest.h"
#include "eventide/serde/schema/kind.h"
#include "eventide/serde/schema/descriptor.h"
#include "eventide/serde/schema/match.h"
#include "eventide/serde/schema/node.h"
#include "eventide/serde/schema/json_schema.h"
#include "eventide/serde/serde/annotation.h"
#include "eventide/serde/schema/attrs.h"

namespace eventide::serde {

namespace {

namespace sk = schema;

// ─── Test types: plain aggregates without annotation wrappers ───────────────

struct simple_struct {
    int x = 0;
    std::string y;
    bool z = false;
};

struct with_optional {
    int id = 0;
    std::optional<std::string> note;
};

struct inner_flat {
    int a = 0;
    double b = 0.0;
};

struct empty_struct {};

struct single_field {
    int only = 0;
};

enum class color : std::uint8_t { red, green, blue };

struct with_vector {
    std::vector<int> items;
};

struct with_map {
    std::map<std::string, int> kv;
};

struct object_a {
    int x = 0;
    std::string y;
};

struct object_b {
    double p = 0.0;
    std::string q;
    int r = 0;
};

struct object_c {
    std::string y;
    bool w = false;
};

struct multi_type {
    int id = 0;
    std::string name;
    double score = 0.0;
    bool active = false;
};

struct camel_fields {
    int user_id = 0;
    std::string first_name;
};

struct camel_cfg {
    using field_rename = rename_policy::lower_camel;
};

struct upper_snake_cfg {
    using field_rename = rename_policy::upper_snake;
};

struct upper_camel_cfg {
    using field_rename = rename_policy::upper_camel;
};

struct lower_snake_cfg {
    using field_rename = rename_policy::lower_snake;
};

struct nested_struct {
    simple_struct inner;
    int extra = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
//  kind.h
// ═══════════════════════════════════════════════════════════════════════════

static_assert(sk::kind_of<std::monostate>() == sk::type_kind::null_like);
static_assert(sk::kind_of<std::nullptr_t>() == sk::type_kind::null_like);
static_assert(sk::kind_of<bool>() == sk::type_kind::boolean);
static_assert(sk::kind_of<int>() == sk::type_kind::integer);
static_assert(sk::kind_of<std::int64_t>() == sk::type_kind::integer);
static_assert(sk::kind_of<unsigned>() == sk::type_kind::integer);
static_assert(sk::kind_of<std::uint8_t>() == sk::type_kind::integer);
static_assert(sk::kind_of<float>() == sk::type_kind::floating);
static_assert(sk::kind_of<double>() == sk::type_kind::floating);
static_assert(sk::kind_of<char>() == sk::type_kind::character);
static_assert(sk::kind_of<std::string>() == sk::type_kind::string);
static_assert(sk::kind_of<std::string_view>() == sk::type_kind::string);
static_assert(sk::kind_of<std::vector<int>>() == sk::type_kind::array);
static_assert(sk::kind_of<std::array<int, 3>>() == sk::type_kind::array);
static_assert(sk::kind_of<std::tuple<int, double>>() == sk::type_kind::tuple);
static_assert(sk::kind_of<std::pair<int, int>>() == sk::type_kind::tuple);
static_assert(sk::kind_of<std::map<std::string, int>>() == sk::type_kind::map);
static_assert(sk::kind_of<simple_struct>() == sk::type_kind::structure);
static_assert(sk::kind_of<std::variant<int, std::string>>() == sk::type_kind::variant);
static_assert(sk::kind_of<color>() == sk::type_kind::enumeration);
static_assert(sk::kind_of<std::optional<int>>() == sk::type_kind::optional);
static_assert(sk::kind_of<std::optional<std::string>>() == sk::type_kind::optional);

static_assert(sk::scalar_kind_of<int>() == sk::scalar_kind::int32);
static_assert(sk::scalar_kind_of<std::uint8_t>() == sk::scalar_kind::uint8);
static_assert(sk::scalar_kind_of<float>() == sk::scalar_kind::float32);
static_assert(sk::scalar_kind_of<double>() == sk::scalar_kind::float64);
static_assert(sk::scalar_kind_of<bool>() == sk::scalar_kind::bool_v);
static_assert(sk::scalar_kind_of<char>() == sk::scalar_kind::char_v);
static_assert(sk::scalar_kind_of<std::string>() == sk::scalar_kind::none);

static_assert(sk::is_optional_type<std::optional<int>>());
static_assert(!sk::is_optional_type<int>());
static_assert(!sk::is_optional_type<std::string>());

static_assert(sk::hint_to_kind(0x40) == sk::type_kind::structure);
static_assert(sk::hint_to_kind(0x20) == sk::type_kind::array);
static_assert(sk::hint_to_kind(0x10) == sk::type_kind::string);
static_assert(sk::hint_to_kind(0x08) == sk::type_kind::floating);
static_assert(sk::hint_to_kind(0x04) == sk::type_kind::integer);
static_assert(sk::hint_to_kind(0x02) == sk::type_kind::boolean);
static_assert(sk::hint_to_kind(0x01) == sk::type_kind::null_like);
static_assert(sk::hint_to_kind(0x00) == sk::type_kind::any);
static_assert(sk::hint_to_kind(0x40 | 0x10) == sk::type_kind::structure);
static_assert(sk::hint_to_kind(0x04 | 0x08) == sk::type_kind::floating);

// ═══════════════════════════════════════════════════════════════════════════
//  descriptor.h
// ═══════════════════════════════════════════════════════════════════════════

static_assert(sk::detail::effective_field_count<simple_struct>() == 3);
static_assert(sk::detail::effective_field_count<with_optional>() == 2);
static_assert(sk::detail::effective_field_count<empty_struct>() == 0);
static_assert(sk::detail::effective_field_count<single_field>() == 1);
static_assert(sk::detail::effective_field_count<inner_flat>() == 2);
static_assert(sk::detail::effective_field_count<multi_type>() == 4);
static_assert(sk::detail::effective_field_count<std::monostate>() == 0);

// ═══════════════════════════════════════════════════════════════════════════
//  match.h: compile-time invariants
// ═══════════════════════════════════════════════════════════════════════════

static_assert(sk::detail::kind_compatible(sk::type_kind::integer, sk::type_kind::integer));
static_assert(sk::detail::kind_compatible(sk::type_kind::integer, sk::type_kind::floating));
static_assert(sk::detail::kind_compatible(sk::type_kind::floating, sk::type_kind::integer));
static_assert(sk::detail::kind_compatible(sk::type_kind::any, sk::type_kind::string));
static_assert(sk::detail::kind_compatible(sk::type_kind::boolean, sk::type_kind::any));
static_assert(!sk::detail::kind_compatible(sk::type_kind::string, sk::type_kind::integer));
static_assert(!sk::detail::kind_compatible(sk::type_kind::boolean, sk::type_kind::string));
static_assert(!sk::detail::kind_compatible(sk::type_kind::structure, sk::type_kind::array));
static_assert(!sk::detail::kind_compatible(sk::type_kind::null_like, sk::type_kind::boolean));

static_assert(sk::detail::count_match_entries<simple_struct>() == 3);
static_assert(sk::detail::count_match_entries<empty_struct>() == 0);
static_assert(sk::detail::count_match_entries<single_field>() == 1);
static_assert(sk::detail::count_match_entries<multi_type>() == 4);

static_assert(sk::detail::count_required_fields<simple_struct>() == 3);
static_assert(sk::detail::count_required_fields<with_optional>() == 1);
static_assert(sk::detail::count_required_fields<empty_struct>() == 0);
static_assert(sk::detail::count_required_fields<multi_type>() == 4);

TEST_SUITE(serde_schema) {

// ─── descriptor.h: describe() ───────────────────────────────────────────────

TEST_CASE(describe_simple_struct) {
    constexpr auto desc = sk::describe<simple_struct>();
    static_assert(desc.field_count() == 3);
    static_assert(desc.fields[0].name == "x");
    static_assert(desc.fields[0].kind == sk::type_kind::integer);
    static_assert(desc.fields[0].required == true);
    static_assert(desc.fields[1].name == "y");
    static_assert(desc.fields[1].kind == sk::type_kind::string);
    static_assert(desc.fields[1].required == true);
    static_assert(desc.fields[2].name == "z");
    static_assert(desc.fields[2].kind == sk::type_kind::boolean);
    static_assert(desc.fields[2].required == true);
    EXPECT_TRUE(true);
}

TEST_CASE(describe_with_optional) {
    constexpr auto desc = sk::describe<with_optional>();
    static_assert(desc.field_count() == 2);
    static_assert(desc.fields[0].name == "id");
    static_assert(desc.fields[0].kind == sk::type_kind::integer);
    static_assert(desc.fields[0].required == true);
    static_assert(desc.fields[1].name == "note");
    static_assert(desc.fields[1].kind == sk::type_kind::string);
    static_assert(desc.fields[1].required == false);
    EXPECT_TRUE(true);
}

TEST_CASE(describe_inner_flat) {
    constexpr auto desc = sk::describe<inner_flat>();
    static_assert(desc.field_count() == 2);
    static_assert(desc.fields[0].name == "a");
    static_assert(desc.fields[0].kind == sk::type_kind::integer);
    static_assert(desc.fields[1].name == "b");
    static_assert(desc.fields[1].kind == sk::type_kind::floating);
    EXPECT_TRUE(true);
}

TEST_CASE(describe_multi_type) {
    constexpr auto desc = sk::describe<multi_type>();
    static_assert(desc.field_count() == 4);
    static_assert(desc.fields[0].name == "id");
    static_assert(desc.fields[0].kind == sk::type_kind::integer);
    static_assert(desc.fields[1].name == "name");
    static_assert(desc.fields[1].kind == sk::type_kind::string);
    static_assert(desc.fields[2].name == "score");
    static_assert(desc.fields[2].kind == sk::type_kind::floating);
    static_assert(desc.fields[3].name == "active");
    static_assert(desc.fields[3].kind == sk::type_kind::boolean);
    EXPECT_TRUE(true);
}

TEST_CASE(schema_of_convenience) {
    constexpr auto& desc = sk::schema_of<simple_struct>::value;
    static_assert(desc.field_count() == 3);
    static_assert(desc.fields[0].name == "x");
    EXPECT_TRUE(true);
}

// ─── descriptor.h: constexpr rename policies ────────────────────────────────

TEST_CASE(normalize_to_lower_snake_cx) {
    static_assert(sk::detail::normalize_to_lower_snake_cx("camelCase").view() == "camel_case");
    static_assert(sk::detail::normalize_to_lower_snake_cx("PascalCase").view() == "pascal_case");
    static_assert(sk::detail::normalize_to_lower_snake_cx("lower_snake").view() == "lower_snake");
    static_assert(sk::detail::normalize_to_lower_snake_cx("UPPER_SNAKE").view() == "upper_snake");
    static_assert(sk::detail::normalize_to_lower_snake_cx("XMLParser").view() == "xml_parser");
    static_assert(
        sk::detail::normalize_to_lower_snake_cx("getHTTPSUrl").view() == "get_https_url");
    static_assert(sk::detail::normalize_to_lower_snake_cx("simple").view() == "simple");
    static_assert(sk::detail::normalize_to_lower_snake_cx("A").view() == "a");
    static_assert(sk::detail::normalize_to_lower_snake_cx("a").view() == "a");
    static_assert(sk::detail::normalize_to_lower_snake_cx("abc123Def").view() == "abc123_def");
    EXPECT_TRUE(true);
}

TEST_CASE(snake_to_camel_cx) {
    static_assert(sk::detail::snake_to_camel_cx("user_name", false).view() == "userName");
    static_assert(sk::detail::snake_to_camel_cx("user_name", true).view() == "UserName");
    static_assert(sk::detail::snake_to_camel_cx("request_id", false).view() == "requestId");
    static_assert(sk::detail::snake_to_camel_cx("request_id", true).view() == "RequestId");
    static_assert(sk::detail::snake_to_camel_cx("x", false).view() == "x");
    static_assert(sk::detail::snake_to_camel_cx("x", true).view() == "X");
    static_assert(sk::detail::snake_to_camel_cx("some_value", false).view() == "someValue");
    static_assert(sk::detail::snake_to_camel_cx("a_b_c", false).view() == "aBC");
    static_assert(sk::detail::snake_to_camel_cx("a_b_c", true).view() == "ABC");
    EXPECT_TRUE(true);
}

TEST_CASE(snake_to_upper_cx) {
    static_assert(sk::detail::snake_to_upper_cx("user_name").view() == "USER_NAME");
    static_assert(sk::detail::snake_to_upper_cx("requestId").view() == "REQUEST_ID");
    static_assert(sk::detail::snake_to_upper_cx("simple").view() == "SIMPLE");
    static_assert(sk::detail::snake_to_upper_cx("a_b_c").view() == "A_B_C");
    EXPECT_TRUE(true);
}

TEST_CASE(apply_rename_cx_identity) {
    using P = rename_policy::identity;
    static_assert(sk::detail::apply_rename_cx<P>("hello").view() == "hello");
    static_assert(sk::detail::apply_rename_cx<P>("user_id").view() == "user_id");
    EXPECT_TRUE(true);
}

TEST_CASE(apply_rename_cx_lower_camel) {
    using P = rename_policy::lower_camel;
    static_assert(sk::detail::apply_rename_cx<P>("user_id").view() == "userId");
    static_assert(sk::detail::apply_rename_cx<P>("first_name").view() == "firstName");
    EXPECT_TRUE(true);
}

TEST_CASE(apply_rename_cx_upper_camel) {
    using P = rename_policy::upper_camel;
    static_assert(sk::detail::apply_rename_cx<P>("user_id").view() == "UserId");
    static_assert(sk::detail::apply_rename_cx<P>("first_name").view() == "FirstName");
    EXPECT_TRUE(true);
}

TEST_CASE(apply_rename_cx_upper_snake) {
    using P = rename_policy::upper_snake;
    static_assert(sk::detail::apply_rename_cx<P>("user_id").view() == "USER_ID");
    static_assert(sk::detail::apply_rename_cx<P>("first_name").view() == "FIRST_NAME");
    EXPECT_TRUE(true);
}

TEST_CASE(apply_rename_cx_lower_snake) {
    using P = rename_policy::lower_snake;
    static_assert(sk::detail::apply_rename_cx<P>("userId").view() == "user_id");
    static_assert(sk::detail::apply_rename_cx<P>("PascalCase").view() == "pascal_case");
    EXPECT_TRUE(true);
}

// ─── match.h: kind_compatible ───────────────────────────────────────────────

TEST_CASE(kind_compatible_symmetric_and_special) {
    using K = sk::type_kind;
    static_assert(sk::detail::kind_compatible(K::integer, K::integer));
    static_assert(sk::detail::kind_compatible(K::string, K::string));
    static_assert(sk::detail::kind_compatible(K::boolean, K::boolean));
    static_assert(sk::detail::kind_compatible(K::floating, K::floating));
    static_assert(sk::detail::kind_compatible(K::structure, K::structure));
    static_assert(sk::detail::kind_compatible(K::array, K::array));
    static_assert(sk::detail::kind_compatible(K::structure, K::map));
    static_assert(sk::detail::kind_compatible(K::enumeration, K::integer));
    static_assert(sk::detail::kind_compatible(K::null_like, K::null_like));

    static_assert(sk::detail::kind_compatible(K::integer, K::floating));
    static_assert(sk::detail::kind_compatible(K::floating, K::integer));

    static_assert(sk::detail::kind_compatible(K::any, K::integer));
    static_assert(sk::detail::kind_compatible(K::any, K::string));
    static_assert(sk::detail::kind_compatible(K::integer, K::any));
    static_assert(sk::detail::kind_compatible(K::any, K::any));

    static_assert(!sk::detail::kind_compatible(K::string, K::integer));
    static_assert(!sk::detail::kind_compatible(K::boolean, K::string));
    static_assert(!sk::detail::kind_compatible(K::structure, K::array));
    static_assert(!sk::detail::kind_compatible(K::null_like, K::boolean));
    static_assert(!sk::detail::kind_compatible(K::string, K::structure));
    EXPECT_TRUE(true);
}

// ─── match.h: build_field_entries (uses string_view wire_name) ──────────────

TEST_CASE(build_field_entries_simple) {
    using cfg = config::default_config;
    constexpr auto entries = sk::detail::build_field_entries<simple_struct, cfg>();
    static_assert(entries.size() == 3);
    static_assert(entries[0].wire_name.size() == 1);
    static_assert(entries[1].wire_name.size() == 1);
    static_assert(entries[2].wire_name.size() == 1);
    EXPECT_TRUE(true);
}

TEST_CASE(build_field_entries_with_camel_config) {
    constexpr auto entries = sk::detail::build_field_entries<camel_fields, camel_cfg>();
    static_assert(entries.size() == 2);

    static_assert([] {
        auto e = sk::detail::build_field_entries<camel_fields, camel_cfg>();
        bool a = false, b = false;
        for(std::size_t i = 0; i < e.size(); i++) {
            if(e[i].wire_name == "userId") a = true;
            if(e[i].wire_name == "firstName") b = true;
        }
        return a && b;
    }());
    EXPECT_TRUE(true);
}

TEST_CASE(build_field_entries_sorted_by_length) {
    using cfg = config::default_config;
    static_assert([] {
        auto e = sk::detail::build_field_entries<multi_type, cfg>();
        for(std::size_t i = 1; i < e.size(); i++) {
            if(e[i].wire_name.size() < e[i - 1].wire_name.size()) return false;
        }
        return true;
    }());
    EXPECT_TRUE(true);
}

TEST_CASE(build_field_entries_upper_snake_config) {
    static_assert([] {
        auto e = sk::detail::build_field_entries<camel_fields, upper_snake_cfg>();
        bool a = false, b = false;
        for(std::size_t i = 0; i < e.size(); i++) {
            if(e[i].wire_name == "USER_ID") a = true;
            if(e[i].wire_name == "FIRST_NAME") b = true;
        }
        return a && b;
    }());
    EXPECT_TRUE(true);
}

TEST_CASE(build_field_entries_upper_camel_config) {
    static_assert([] {
        auto e = sk::detail::build_field_entries<camel_fields, upper_camel_cfg>();
        bool a = false, b = false;
        for(std::size_t i = 0; i < e.size(); i++) {
            if(e[i].wire_name == "UserId") a = true;
            if(e[i].wire_name == "FirstName") b = true;
        }
        return a && b;
    }());
    EXPECT_TRUE(true);
}

TEST_CASE(build_field_entries_preserves_kind) {
    using cfg = config::default_config;
    static_assert([] {
        auto e = sk::detail::build_field_entries<multi_type, cfg>();
        for(std::size_t i = 0; i < e.size(); i++) {
            if(e[i].wire_name == "id" && e[i].kind != sk::type_kind::integer) return false;
            if(e[i].wire_name == "name" && e[i].kind != sk::type_kind::string) return false;
            if(e[i].wire_name == "score" && e[i].kind != sk::type_kind::floating) return false;
            if(e[i].wire_name == "active" && e[i].kind != sk::type_kind::boolean) return false;
        }
        return true;
    }());
    EXPECT_TRUE(true);
}

TEST_CASE(build_field_entries_required_flag) {
    using cfg = config::default_config;
    static_assert([] {
        auto e = sk::detail::build_field_entries<with_optional, cfg>();
        for(std::size_t i = 0; i < e.size(); i++) {
            if(e[i].wire_name == "id" && !e[i].required) return false;
            if(e[i].wire_name == "note" && e[i].required) return false;
        }
        return true;
    }());
    EXPECT_TRUE(true);
}

TEST_CASE(build_field_entries_default_config_identity) {
    using cfg = config::default_config;
    static_assert([] {
        auto e = sk::detail::build_field_entries<camel_fields, cfg>();
        bool a = false, b = false;
        for(std::size_t i = 0; i < e.size(); i++) {
            if(e[i].wire_name == "user_id") a = true;
            if(e[i].wire_name == "first_name") b = true;
        }
        return a && b;
    }());
    EXPECT_TRUE(true);
}

// ─── match.h: match_key ────────────────────────────────────────────────────

TEST_CASE(match_key_simple_hit) {
    using cfg = config::default_config;
    auto r = sk::detail::match_key<simple_struct, cfg>("x");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<int>(r->kind), static_cast<int>(sk::type_kind::integer));
    EXPECT_TRUE(r->required);
}

TEST_CASE(match_key_simple_miss) {
    using cfg = config::default_config;
    auto r = sk::detail::match_key<simple_struct, cfg>("nonexistent");
    EXPECT_FALSE(r.has_value());
}

TEST_CASE(match_key_all_fields) {
    using cfg = config::default_config;
    auto rx = sk::detail::match_key<simple_struct, cfg>("x");
    auto ry = sk::detail::match_key<simple_struct, cfg>("y");
    auto rz = sk::detail::match_key<simple_struct, cfg>("z");
    ASSERT_TRUE(rx.has_value());
    ASSERT_TRUE(ry.has_value());
    ASSERT_TRUE(rz.has_value());
    EXPECT_EQ(static_cast<int>(rx->kind), static_cast<int>(sk::type_kind::integer));
    EXPECT_EQ(static_cast<int>(ry->kind), static_cast<int>(sk::type_kind::string));
    EXPECT_EQ(static_cast<int>(rz->kind), static_cast<int>(sk::type_kind::boolean));
}

TEST_CASE(match_key_with_camel_config) {
    auto r1 = sk::detail::match_key<camel_fields, camel_cfg>("userId");
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(static_cast<int>(r1->kind), static_cast<int>(sk::type_kind::integer));

    auto r2 = sk::detail::match_key<camel_fields, camel_cfg>("firstName");
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(static_cast<int>(r2->kind), static_cast<int>(sk::type_kind::string));

    auto miss = sk::detail::match_key<camel_fields, camel_cfg>("user_id");
    EXPECT_FALSE(miss.has_value());
}

TEST_CASE(match_key_with_upper_snake_config) {
    auto r1 = sk::detail::match_key<camel_fields, upper_snake_cfg>("USER_ID");
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(static_cast<int>(r1->kind), static_cast<int>(sk::type_kind::integer));

    auto r2 = sk::detail::match_key<camel_fields, upper_snake_cfg>("FIRST_NAME");
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(static_cast<int>(r2->kind), static_cast<int>(sk::type_kind::string));
}

TEST_CASE(match_key_empty_struct) {
    using cfg = config::default_config;
    auto r = sk::detail::match_key<empty_struct, cfg>("anything");
    EXPECT_FALSE(r.has_value());
}

TEST_CASE(match_key_length_mismatch_fast_path) {
    using cfg = config::default_config;
    auto r = sk::detail::match_key<simple_struct, cfg>("xx");
    EXPECT_FALSE(r.has_value());

    auto r2 = sk::detail::match_key<simple_struct, cfg>("");
    EXPECT_FALSE(r2.has_value());
}

TEST_CASE(match_key_optional_field_not_required) {
    using cfg = config::default_config;
    auto r = sk::detail::match_key<with_optional, cfg>("note");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(static_cast<int>(r->kind), static_cast<int>(sk::type_kind::string));
    EXPECT_FALSE(r->required);
}

TEST_CASE(match_key_multi_type_all_fields) {
    using cfg = config::default_config;
    auto r_id = sk::detail::match_key<multi_type, cfg>("id");
    auto r_name = sk::detail::match_key<multi_type, cfg>("name");
    auto r_score = sk::detail::match_key<multi_type, cfg>("score");
    auto r_active = sk::detail::match_key<multi_type, cfg>("active");

    ASSERT_TRUE(r_id.has_value());
    ASSERT_TRUE(r_name.has_value());
    ASSERT_TRUE(r_score.has_value());
    ASSERT_TRUE(r_active.has_value());

    EXPECT_EQ(static_cast<int>(r_id->kind), static_cast<int>(sk::type_kind::integer));
    EXPECT_EQ(static_cast<int>(r_name->kind), static_cast<int>(sk::type_kind::string));
    EXPECT_EQ(static_cast<int>(r_score->kind), static_cast<int>(sk::type_kind::floating));
    EXPECT_EQ(static_cast<int>(r_active->kind), static_cast<int>(sk::type_kind::boolean));
}

// ─── match.h: match_variant (using schema_node) ─────────────────────────────

sk::schema_node make_object_node(
    std::initializer_list<std::pair<const char*, serde::type_hint>> fields) {
    sk::schema_node node;
    node.hints = serde::type_hint::object;
    for(auto& [k, h]: fields) {
        node.fields.push_back({std::string(k), h});
    }
    return node;
}

TEST_CASE(variant_match_basic) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"x", serde::type_hint::integer},
        {"y", serde::type_hint::string},
    });

    auto result = sk::match_variant<cfg, object_a, object_b>(node);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
}

TEST_CASE(variant_match_second_alt) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"p", serde::type_hint::floating},
        {"q", serde::type_hint::string},
        {"r", serde::type_hint::integer},
    });

    auto result = sk::match_variant<cfg, object_a, object_b>(node);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1u);
}

TEST_CASE(variant_match_no_match) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"unknown1", serde::type_hint::string},
        {"unknown2", serde::type_hint::integer},
    });

    auto result = sk::match_variant<cfg, object_a, object_b>(node);
    EXPECT_FALSE(result.has_value());
}

TEST_CASE(variant_match_type_mismatch_rejects) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"x", serde::type_hint::string},
        {"y", serde::type_hint::string},
    });

    auto result = sk::match_variant<cfg, object_a, object_c>(node);
    EXPECT_FALSE(result.has_value());
}

TEST_CASE(variant_match_picks_higher_score) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"p", serde::type_hint::floating},
        {"q", serde::type_hint::string},
        {"r", serde::type_hint::integer},
        {"extra", serde::type_hint::string},
    });

    auto result = sk::match_variant<cfg, object_a, object_b>(node);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1u);
}

TEST_CASE(variant_match_with_non_reflectable_alts) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"x", serde::type_hint::integer},
        {"y", serde::type_hint::string},
    });

    auto result = sk::match_variant<cfg, int, std::string, object_a>(node);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 2u);
}

TEST_CASE(variant_match_empty_incoming) {
    using cfg = config::default_config;
    sk::schema_node node;
    node.hints = serde::type_hint::object;

    auto result = sk::match_variant<cfg, object_a, object_b>(node);
    EXPECT_FALSE(result.has_value());
}

TEST_CASE(variant_match_with_optional_fields) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"id", serde::type_hint::integer},
    });

    auto result = sk::match_variant<cfg, with_optional, simple_struct>(node);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
}

TEST_CASE(variant_match_first_wins_on_tie) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"x", serde::type_hint::integer},
        {"y", serde::type_hint::string},
        {"w", serde::type_hint::boolean},
    });

    auto result = sk::match_variant<cfg, object_a, object_c>(node);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
}

TEST_CASE(variant_match_required_fields_missing) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"y", serde::type_hint::string},
    });

    auto result = sk::match_variant<cfg, simple_struct>(node);
    EXPECT_FALSE(result.has_value());
}

TEST_CASE(variant_match_kind_any_is_flexible) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"items", serde::type_hint::any},
    });

    auto result = sk::match_variant<cfg, with_vector>(node);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
}

TEST_CASE(variant_match_integer_float_compatible) {
    using cfg = config::default_config;
    auto node = make_object_node({
        {"a", serde::type_hint::floating},
        {"b", serde::type_hint::integer},
    });

    auto result = sk::match_variant<cfg, inner_flat>(node);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0u);
}

// ─── match.h: build_kind_to_index_map ───────────────────────────────────────

TEST_CASE(kind_to_index_map_basic) {
    constexpr auto map = sk::build_kind_to_index_map<int, std::string, bool>();
    constexpr auto int_hi = sk::kind_to_hint_index(sk::type_kind::integer);
    constexpr auto str_hi = sk::kind_to_hint_index(sk::type_kind::string);
    constexpr auto bool_hi = sk::kind_to_hint_index(sk::type_kind::boolean);

    static_assert(map.count[int_hi] == 1);
    static_assert(map.indices[int_hi][0] == 0);
    static_assert(map.count[str_hi] == 1);
    static_assert(map.indices[str_hi][0] == 1);
    static_assert(map.count[bool_hi] == 1);
    static_assert(map.indices[bool_hi][0] == 2);
    EXPECT_TRUE(true);
}

TEST_CASE(kind_to_index_map_multiple_same_kind) {
    constexpr auto map = sk::build_kind_to_index_map<int, std::string, std::uint64_t>();
    constexpr auto int_hi = sk::kind_to_hint_index(sk::type_kind::integer);

    static_assert(map.count[int_hi] == 2);
    static_assert(map.indices[int_hi][0] == 0);
    static_assert(map.indices[int_hi][1] == 2);
    EXPECT_TRUE(true);
}

TEST_CASE(kind_to_index_map_with_objects) {
    constexpr auto map = sk::build_kind_to_index_map<simple_struct, int, object_a>();
    constexpr auto obj_hi = sk::kind_to_hint_index(sk::type_kind::structure);
    constexpr auto int_hi = sk::kind_to_hint_index(sk::type_kind::integer);

    static_assert(map.count[obj_hi] == 2);
    static_assert(map.indices[obj_hi][0] == 0);
    static_assert(map.indices[obj_hi][1] == 2);
    static_assert(map.count[int_hi] == 1);
    static_assert(map.indices[int_hi][0] == 1);
    EXPECT_TRUE(true);
}

TEST_CASE(kind_to_index_map_empty_kinds) {
    constexpr auto map = sk::build_kind_to_index_map<int>();
    constexpr auto str_hi = sk::kind_to_hint_index(sk::type_kind::string);
    constexpr auto bool_hi = sk::kind_to_hint_index(sk::type_kind::boolean);
    constexpr auto arr_hi = sk::kind_to_hint_index(sk::type_kind::array);

    static_assert(map.count[str_hi] == 0);
    static_assert(map.count[bool_hi] == 0);
    static_assert(map.count[arr_hi] == 0);
    EXPECT_TRUE(true);
}

TEST_CASE(kind_to_index_map_arrays_and_tuples) {
    constexpr auto map =
        sk::build_kind_to_index_map<std::vector<int>, std::tuple<int, double>, int>();
    constexpr auto arr_hi = sk::kind_to_hint_index(sk::type_kind::array);
    constexpr auto int_hi = sk::kind_to_hint_index(sk::type_kind::integer);

    static_assert(map.count[arr_hi] == 2);
    static_assert(map.indices[arr_hi][0] == 0);
    static_assert(map.indices[arr_hi][1] == 1);
    static_assert(map.count[int_hi] == 1);
    static_assert(map.indices[int_hi][0] == 2);
    EXPECT_TRUE(true);
}

TEST_CASE(kind_to_index_map_all_primitive_kinds) {
    constexpr auto map = sk::build_kind_to_index_map<std::monostate, bool, int, double,
                                                     std::string, std::vector<int>, object_a>();
    constexpr auto null_hi = sk::kind_to_hint_index(sk::type_kind::null_like);
    constexpr auto bool_hi = sk::kind_to_hint_index(sk::type_kind::boolean);
    constexpr auto int_hi = sk::kind_to_hint_index(sk::type_kind::integer);
    constexpr auto float_hi = sk::kind_to_hint_index(sk::type_kind::floating);
    constexpr auto str_hi = sk::kind_to_hint_index(sk::type_kind::string);
    constexpr auto arr_hi = sk::kind_to_hint_index(sk::type_kind::array);
    constexpr auto obj_hi = sk::kind_to_hint_index(sk::type_kind::structure);

    static_assert(map.count[null_hi] == 1 && map.indices[null_hi][0] == 0);
    static_assert(map.count[bool_hi] == 1 && map.indices[bool_hi][0] == 1);
    static_assert(map.count[int_hi] == 1 && map.indices[int_hi][0] == 2);
    static_assert(map.count[float_hi] == 1 && map.indices[float_hi][0] == 3);
    static_assert(map.count[str_hi] == 1 && map.indices[str_hi][0] == 4);
    static_assert(map.count[arr_hi] == 1 && map.indices[arr_hi][0] == 5);
    static_assert(map.count[obj_hi] == 1 && map.indices[obj_hi][0] == 6);
    EXPECT_TRUE(true);
}

// ─── descriptor.h: type_schema_view / get_schema ────────────────────────────

TEST_CASE(get_schema_primitive_int) {
    constexpr auto* s = sk::get_schema<int>();
    static_assert(s->kind == sk::type_kind::integer);
    static_assert(s->fields.empty());
    static_assert(s->element == nullptr);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_string) {
    constexpr auto* s = sk::get_schema<std::string>();
    static_assert(s->kind == sk::type_kind::string);
    static_assert(s->fields.empty());
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_simple_struct) {
    constexpr auto* s = sk::get_schema<simple_struct>();
    static_assert(s->kind == sk::type_kind::structure);
    static_assert(s->fields.size() == 3);

    static_assert(s->fields[0].wire_name == "x");
    static_assert(s->fields[0].kind == sk::type_kind::integer);
    static_assert(s->fields[0].required == true);
    static_assert(s->fields[0].nested != nullptr);
    static_assert(s->fields[0].nested->kind == sk::type_kind::integer);

    static_assert(s->fields[1].wire_name == "y");
    static_assert(s->fields[1].kind == sk::type_kind::string);
    static_assert(s->fields[1].nested->kind == sk::type_kind::string);

    static_assert(s->fields[2].wire_name == "z");
    static_assert(s->fields[2].kind == sk::type_kind::boolean);
    static_assert(s->fields[2].nested->kind == sk::type_kind::boolean);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_with_optional) {
    constexpr auto* s = sk::get_schema<with_optional>();
    static_assert(s->kind == sk::type_kind::structure);
    static_assert(s->fields.size() == 2);

    static_assert(s->fields[0].wire_name == "id");
    static_assert(s->fields[0].required == true);

    static_assert(s->fields[1].wire_name == "note");
    static_assert(s->fields[1].required == false);
    static_assert(s->fields[1].kind == sk::type_kind::string);
    static_assert(s->fields[1].nested->kind == sk::type_kind::string);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_vector_element) {
    constexpr auto* s = sk::get_schema<std::vector<int>>();
    static_assert(s->kind == sk::type_kind::array);
    static_assert(s->fields.empty());
    static_assert(s->element != nullptr);
    static_assert(s->element->kind == sk::type_kind::integer);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_nested_struct) {
    constexpr auto* s = sk::get_schema<nested_struct>();
    static_assert(s->kind == sk::type_kind::structure);
    static_assert(s->fields.size() == 2);

    static_assert(s->fields[0].wire_name == "inner");
    static_assert(s->fields[0].kind == sk::type_kind::structure);
    static_assert(s->fields[0].nested != nullptr);
    static_assert(s->fields[0].nested->kind == sk::type_kind::structure);
    static_assert(s->fields[0].nested->fields.size() == 3);

    static_assert(s->fields[1].wire_name == "extra");
    static_assert(s->fields[1].kind == sk::type_kind::integer);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_with_rename) {
    constexpr auto* s = sk::get_schema<camel_fields, camel_cfg>();
    static_assert(s->kind == sk::type_kind::structure);
    static_assert(s->fields.size() == 2);
    static_assert(s->fields[0].wire_name == "userId");
    static_assert(s->fields[1].wire_name == "firstName");
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_map_element) {
    constexpr auto* s = sk::get_schema<std::map<std::string, int>>();
    static_assert(s->kind == sk::type_kind::map);
    static_assert(s->fields.empty());
    static_assert(s->key != nullptr);
    static_assert(s->key->kind == sk::type_kind::string);
    static_assert(s->value != nullptr);
    static_assert(s->value->kind == sk::type_kind::integer);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_type_erased_pointer_stable) {
    constexpr auto* s1 = sk::get_schema<simple_struct>();
    constexpr auto* s2 = sk::get_schema<simple_struct>();
    static_assert(s1 == s2);
    EXPECT_TRUE(true);
}

// ─── wire_name_static ───────────────────────────────────────────────────────

TEST_CASE(wire_name_static_camel) {
    using ws = sk::detail::wire_name_static<camel_fields, 0, rename_policy::lower_camel>;
    static_assert(ws::view == "userId");
    static_assert(ws::length() == 6);

    using ws2 = sk::detail::wire_name_static<camel_fields, 1, rename_policy::lower_camel>;
    static_assert(ws2::view == "firstName");
    static_assert(ws2::length() == 9);
    EXPECT_TRUE(true);
}

TEST_CASE(wire_name_static_upper_snake) {
    using ws = sk::detail::wire_name_static<camel_fields, 0, rename_policy::upper_snake>;
    static_assert(ws::view == "USER_ID");
    static_assert(ws::length() == 7);
    EXPECT_TRUE(true);
}

// ═══════════════════════════════════════════════════════════════════════════
//  New type_kind tests
// ═══════════════════════════════════════════════════════════════════════════

static_assert(sk::kind_of<std::set<int>>() == sk::type_kind::set);
static_assert(sk::kind_of<std::unordered_map<std::string, int>>() == sk::type_kind::map);
static_assert(sk::kind_of<std::unique_ptr<int>>() == sk::type_kind::pointer);
static_assert(sk::kind_of<std::shared_ptr<int>>() == sk::type_kind::pointer);

TEST_CASE(kind_of_new_types) {
    static_assert(sk::kind_of<std::set<int>>() == sk::type_kind::set);
    static_assert(sk::kind_of<std::tuple<int, double, std::string>>() == sk::type_kind::tuple);
    static_assert(sk::kind_of<std::pair<int, std::string>>() == sk::type_kind::tuple);
    static_assert(sk::kind_of<std::map<std::string, int>>() == sk::type_kind::map);
    static_assert(sk::kind_of<std::unique_ptr<int>>() == sk::type_kind::pointer);
    static_assert(sk::kind_of<std::shared_ptr<int>>() == sk::type_kind::pointer);
    static_assert(sk::kind_of<std::optional<int>>() == sk::type_kind::optional);
    static_assert(sk::kind_of<color>() == sk::type_kind::enumeration);
    static_assert(sk::kind_of<char>() == sk::type_kind::character);
    EXPECT_TRUE(true);
}

// ─── scalar_kind_of tests ───────────────────────────────────────────────────

TEST_CASE(scalar_kind_of_types) {
    static_assert(sk::scalar_kind_of<bool>() == sk::scalar_kind::bool_v);
    static_assert(sk::scalar_kind_of<char>() == sk::scalar_kind::char_v);
    static_assert(sk::scalar_kind_of<int>() == sk::scalar_kind::int32);
    static_assert(sk::scalar_kind_of<unsigned>() == sk::scalar_kind::uint32);
    static_assert(sk::scalar_kind_of<std::int8_t>() == sk::scalar_kind::int8);
    static_assert(sk::scalar_kind_of<std::int16_t>() == sk::scalar_kind::int16);
    static_assert(sk::scalar_kind_of<std::int64_t>() == sk::scalar_kind::int64);
    static_assert(sk::scalar_kind_of<std::uint8_t>() == sk::scalar_kind::uint8);
    static_assert(sk::scalar_kind_of<std::uint16_t>() == sk::scalar_kind::uint16);
    static_assert(sk::scalar_kind_of<std::uint64_t>() == sk::scalar_kind::uint64);
    static_assert(sk::scalar_kind_of<float>() == sk::scalar_kind::float32);
    static_assert(sk::scalar_kind_of<double>() == sk::scalar_kind::float64);
    static_assert(sk::scalar_kind_of<std::string>() == sk::scalar_kind::none);
    static_assert(sk::scalar_kind_of<std::vector<int>>() == sk::scalar_kind::none);
    EXPECT_TRUE(true);
}

// ─── type_flags_of tests ────────────────────────────────────────────────────

TEST_CASE(type_flags_of_types) {
    constexpr auto int_flags = sk::type_flags_of<int>();
    static_assert(sk::has_flag(int_flags, sk::type_flags::is_trivial));
    static_assert(sk::has_flag(int_flags, sk::type_flags::is_standard_layout));
    static_assert(sk::has_flag(int_flags, sk::type_flags::is_trivially_copyable));
    static_assert(sk::has_flag(int_flags, sk::type_flags::is_signed));

    constexpr auto uint_flags = sk::type_flags_of<unsigned>();
    static_assert(!sk::has_flag(uint_flags, sk::type_flags::is_signed));

    constexpr auto string_flags = sk::type_flags_of<std::string>();
    static_assert(!sk::has_flag(string_flags, sk::type_flags::is_trivial));

    constexpr auto struct_flags = sk::type_flags_of<simple_struct>();
    static_assert(!sk::has_flag(struct_flags, sk::type_flags::is_trivial));
    EXPECT_TRUE(true);
}

// ─── kind_to_hint_index tests ───────────────────────────────────────────────

TEST_CASE(kind_to_hint_index_mapping) {
    static_assert(sk::kind_to_hint_index(sk::type_kind::null_like) == 0);
    static_assert(sk::kind_to_hint_index(sk::type_kind::boolean) == 1);
    static_assert(sk::kind_to_hint_index(sk::type_kind::integer) == 2);
    static_assert(sk::kind_to_hint_index(sk::type_kind::enumeration) == 2);
    static_assert(sk::kind_to_hint_index(sk::type_kind::floating) == 3);
    static_assert(sk::kind_to_hint_index(sk::type_kind::string) == 4);
    static_assert(sk::kind_to_hint_index(sk::type_kind::character) == 4);
    static_assert(sk::kind_to_hint_index(sk::type_kind::array) == 5);
    static_assert(sk::kind_to_hint_index(sk::type_kind::set) == 5);
    static_assert(sk::kind_to_hint_index(sk::type_kind::tuple) == 5);
    static_assert(sk::kind_to_hint_index(sk::type_kind::bytes) == 5);
    static_assert(sk::kind_to_hint_index(sk::type_kind::structure) == 6);
    static_assert(sk::kind_to_hint_index(sk::type_kind::map) == 6);
    EXPECT_TRUE(true);
}

// ─── kind_compatible new combinations ───────────────────────────────────────

TEST_CASE(kind_compatible_new_kinds) {
    using K = sk::type_kind;
    static_assert(sk::detail::kind_compatible(K::integer, K::enumeration));
    static_assert(sk::detail::kind_compatible(K::enumeration, K::integer));
    static_assert(sk::detail::kind_compatible(K::structure, K::map));
    static_assert(sk::detail::kind_compatible(K::map, K::structure));
    static_assert(sk::detail::kind_compatible(K::string, K::character));
    static_assert(sk::detail::kind_compatible(K::character, K::string));
    static_assert(sk::detail::kind_compatible(K::array, K::set));
    static_assert(sk::detail::kind_compatible(K::array, K::tuple));
    static_assert(sk::detail::kind_compatible(K::array, K::bytes));
    static_assert(!sk::detail::kind_compatible(K::structure, K::array));
    static_assert(!sk::detail::kind_compatible(K::set, K::map));
    EXPECT_TRUE(true);
}

// ─── Enriched type_schema_view tests ────────────────────────────────────────

TEST_CASE(get_schema_enriched_int) {
    constexpr auto* s = sk::get_schema<int>();
    static_assert(s->kind == sk::type_kind::integer);
    static_assert(s->scalar == sk::scalar_kind::int32);
    static_assert(sk::has_flag(s->flags, sk::type_flags::is_trivial));
    static_assert(s->cpp_size == sizeof(int));
    static_assert(s->cpp_align == alignof(int));
    static_assert(s->element == nullptr);
    static_assert(s->key == nullptr);
    static_assert(s->value == nullptr);
    static_assert(s->tuple_elements.empty());
    static_assert(s->alternatives.empty());
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_enriched_struct) {
    constexpr auto* s = sk::get_schema<simple_struct>();
    static_assert(s->kind == sk::type_kind::structure);
    static_assert(s->scalar == sk::scalar_kind::none);
    static_assert(s->cpp_size == sizeof(simple_struct));
    static_assert(s->cpp_align == alignof(simple_struct));
    static_assert(s->fields.size() == 3);
    static_assert(s->alternatives.empty());
    static_assert(s->tuple_elements.empty());
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_map_key_value) {
    constexpr auto* s = sk::get_schema<std::map<std::string, int>>();
    static_assert(s->kind == sk::type_kind::map);
    static_assert(s->key != nullptr);
    static_assert(s->key->kind == sk::type_kind::string);
    static_assert(s->value != nullptr);
    static_assert(s->value->kind == sk::type_kind::integer);
    static_assert(s->element == nullptr);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_tuple_elements) {
    constexpr auto* s = sk::get_schema<std::tuple<int, double, std::string>>();
    static_assert(s->kind == sk::type_kind::tuple);
    static_assert(s->tuple_elements.size() == 3);
    static_assert(s->tuple_elements[0]->kind == sk::type_kind::integer);
    static_assert(s->tuple_elements[1]->kind == sk::type_kind::floating);
    static_assert(s->tuple_elements[2]->kind == sk::type_kind::string);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_pair_elements) {
    constexpr auto* s = sk::get_schema<std::pair<int, std::string>>();
    static_assert(s->kind == sk::type_kind::tuple);
    static_assert(s->tuple_elements.size() == 2);
    static_assert(s->tuple_elements[0]->kind == sk::type_kind::integer);
    static_assert(s->tuple_elements[1]->kind == sk::type_kind::string);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_variant_alternatives) {
    using V = std::variant<int, std::string, double>;
    constexpr auto* s = sk::get_schema<V>();
    static_assert(s->kind == sk::type_kind::variant);
    static_assert(s->alternatives.size() == 3);
    static_assert(s->alternatives[0]->kind == sk::type_kind::integer);
    static_assert(s->alternatives[1]->kind == sk::type_kind::string);
    static_assert(s->alternatives[2]->kind == sk::type_kind::floating);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_optional) {
    constexpr auto* s = sk::get_schema<std::optional<int>>();
    static_assert(s->kind == sk::type_kind::optional);
    static_assert(s->element != nullptr);
    static_assert(s->element->kind == sk::type_kind::integer);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_set) {
    constexpr auto* s = sk::get_schema<std::set<int>>();
    static_assert(s->kind == sk::type_kind::set);
    static_assert(s->element != nullptr);
    static_assert(s->element->kind == sk::type_kind::integer);
    EXPECT_TRUE(true);
}

TEST_CASE(get_schema_enum) {
    constexpr auto* s = sk::get_schema<color>();
    static_assert(s->kind == sk::type_kind::enumeration);
    static_assert(s->scalar != sk::scalar_kind::none);
    EXPECT_TRUE(true);
}

// ─── JSON Schema generation tests ──────────────────────────────────────────

TEST_CASE(json_schema_primitive_int) {
    auto schema = sk::to_json_schema<int>();
    EXPECT_EQ(schema, "{\"type\":\"integer\"}");
}

TEST_CASE(json_schema_primitive_string) {
    auto schema = sk::to_json_schema<std::string>();
    EXPECT_EQ(schema, "{\"type\":\"string\"}");
}

TEST_CASE(json_schema_primitive_bool) {
    auto schema = sk::to_json_schema<bool>();
    EXPECT_EQ(schema, "{\"type\":\"boolean\"}");
}

TEST_CASE(json_schema_primitive_double) {
    auto schema = sk::to_json_schema<double>();
    EXPECT_EQ(schema, "{\"type\":\"number\"}");
}

TEST_CASE(json_schema_primitive_null) {
    auto schema = sk::to_json_schema<std::monostate>();
    EXPECT_EQ(schema, "{\"type\":\"null\"}");
}

TEST_CASE(json_schema_vector) {
    auto schema = sk::to_json_schema<std::vector<int>>();
    EXPECT_EQ(schema, "{\"type\":\"array\",\"items\":{\"type\":\"integer\"}}");
}

TEST_CASE(json_schema_map) {
    auto schema = sk::to_json_schema<std::map<std::string, int>>();
    EXPECT_EQ(schema, "{\"type\":\"object\",\"additionalProperties\":{\"type\":\"integer\"}}");
}

TEST_CASE(json_schema_tuple) {
    auto schema = sk::to_json_schema<std::tuple<int, std::string>>();
    EXPECT_EQ(schema,
              "{\"type\":\"array\",\"prefixItems\":[{\"type\":\"integer\"},"
              "{\"type\":\"string\"}],\"items\":false}");
}

TEST_CASE(json_schema_simple_struct) {
    auto schema = sk::to_json_schema<simple_struct>();
    EXPECT_TRUE(schema.find("\"$defs\"") != std::string::npos);
    EXPECT_TRUE(schema.find("\"$ref\"") != std::string::npos);
    EXPECT_TRUE(schema.find("\"x\"") != std::string::npos);
    EXPECT_TRUE(schema.find("\"y\"") != std::string::npos);
    EXPECT_TRUE(schema.find("\"z\"") != std::string::npos);
    EXPECT_TRUE(schema.find("\"required\"") != std::string::npos);
}

TEST_CASE(json_schema_optional_field) {
    auto schema = sk::to_json_schema<with_optional>();
    EXPECT_TRUE(schema.find("\"required\"") != std::string::npos);
    EXPECT_TRUE(schema.find("\"id\"") != std::string::npos);
    EXPECT_TRUE(schema.find("\"note\"") != std::string::npos);
}

TEST_CASE(json_schema_variant) {
    using V = std::variant<int, std::string>;
    auto schema = sk::to_json_schema<V>();
    EXPECT_TRUE(schema.find("\"oneOf\"") != std::string::npos);
    EXPECT_TRUE(schema.find("\"integer\"") != std::string::npos);
    EXPECT_TRUE(schema.find("\"string\"") != std::string::npos);
}

// ─── Tagged variant schema tests ────────────────────────────────────────────

struct dog { std::string breed; };
struct cat { int lives = 9; };

using untagged_animal = std::variant<dog, cat>;
using ext_tagged_animal = annotation<std::variant<dog, cat>, schema::tagged<>>;
using int_tagged_animal = annotation<std::variant<dog, cat>, schema::tagged<"type">>;
using adj_tagged_animal = annotation<std::variant<dog, cat>, schema::tagged<"t", "c">>;

TEST_CASE(tag_mode_untagged_variant) {
    auto* sv = sk::get_schema<untagged_animal>();
    EXPECT_EQ(sv->kind, sk::type_kind::variant);
    EXPECT_EQ(sv->tagging, sk::tag_mode::none);
    EXPECT_TRUE(sv->tag_field.empty());
    EXPECT_TRUE(sv->content_field.empty());
    EXPECT_TRUE(sv->alt_names.empty());
    EXPECT_EQ(sv->alternatives.size(), 2u);
}

TEST_CASE(tag_mode_externally_tagged) {
    auto* sv = sk::get_schema<ext_tagged_animal>();
    EXPECT_EQ(sv->kind, sk::type_kind::variant);
    EXPECT_EQ(sv->tagging, sk::tag_mode::external);
    EXPECT_TRUE(sv->tag_field.empty());
    EXPECT_TRUE(sv->content_field.empty());
    EXPECT_EQ(sv->alt_names.size(), 2u);
    EXPECT_EQ(sv->alt_names[0], "dog");
    EXPECT_EQ(sv->alt_names[1], "cat");
    EXPECT_EQ(sv->alternatives.size(), 2u);
}

TEST_CASE(tag_mode_internally_tagged) {
    auto* sv = sk::get_schema<int_tagged_animal>();
    EXPECT_EQ(sv->kind, sk::type_kind::variant);
    EXPECT_EQ(sv->tagging, sk::tag_mode::internal);
    EXPECT_EQ(sv->tag_field, "type");
    EXPECT_TRUE(sv->content_field.empty());
    EXPECT_EQ(sv->alt_names.size(), 2u);
    EXPECT_EQ(sv->alt_names[0], "dog");
    EXPECT_EQ(sv->alt_names[1], "cat");
}

TEST_CASE(tag_mode_adjacently_tagged) {
    auto* sv = sk::get_schema<adj_tagged_animal>();
    EXPECT_EQ(sv->kind, sk::type_kind::variant);
    EXPECT_EQ(sv->tagging, sk::tag_mode::adjacent);
    EXPECT_EQ(sv->tag_field, "t");
    EXPECT_EQ(sv->content_field, "c");
    EXPECT_EQ(sv->alt_names.size(), 2u);
}

};  // TEST_SUITE(serde_schema)

}  // namespace

}  // namespace eventide::serde
