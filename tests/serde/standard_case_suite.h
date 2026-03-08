#pragma once

// Standard backend-agnostic serde test case macros.
//
// Usage:
//   auto roundtrip = []<typename T>(const T& input) { ... return std::expected<T, E>; };
//   SERDE_STANDARD_TEST_CASES_ALL(roundtrip)
//
// Or pick subsets:
//   SERDE_STANDARD_TEST_CASES_PRIMITIVES(roundtrip)
//   SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(roundtrip)
//   SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(roundtrip)
//   SERDE_STANDARD_TEST_CASES_MAPS(roundtrip)
//   SERDE_STANDARD_TEST_CASES_OPTIONAL(roundtrip)
//   SERDE_STANDARD_TEST_CASES_POINTERS(roundtrip)
//   SERDE_STANDARD_TEST_CASES_VARIANT(roundtrip)
//   SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(roundtrip)
//   SERDE_STANDARD_TEST_CASES_ATTRS(roundtrip)
//   SERDE_STANDARD_TEST_CASES_TAGGED_VARIANTS(roundtrip)
//   SERDE_STANDARD_TEST_CASES_STL_CONTAINERS(roundtrip) // aggregate of the six groups above
//   SERDE_STANDARD_TEST_CASES_EXTENDED(roundtrip) // numeric/attrs/tagged aggregate
//   SERDE_STANDARD_TEST_CASES_ERROR_PATHS_TEXT(decode_text) // optional, needs text decoder hook
//   SERDE_STANDARD_TEST_CASES_COMPLEX(roundtrip)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "eventide/serde/serde/attrs.h"

namespace eventide::serde::standard_case {

struct Basic {
    bool is_valid{};
    std::int32_t i32{};
    double f64{};
    std::string text;

    auto operator==(const Basic&) const -> bool = default;
};

struct Compound {
    std::vector<std::string> string_list;
    std::array<float, 3> fixed_array{};
    std::tuple<int, bool, std::string> heterogeneous_tuple;

    auto operator==(const Compound&) const -> bool = default;
};

struct Nullables {
    std::optional<int> opt_value;
    std::optional<std::string> opt_empty;
    std::unique_ptr<Basic> heap_allocated;

    auto operator==(const Nullables& other) const -> bool {
        if(opt_value != other.opt_value || opt_empty != other.opt_empty) {
            return false;
        }
        if(static_cast<bool>(heap_allocated) != static_cast<bool>(other.heap_allocated)) {
            return false;
        }
        if(!heap_allocated) {
            return true;
        }
        return *heap_allocated == *other.heap_allocated;
    }
};

enum class Role : std::uint8_t {
    admin = 1,
    user = 2,
    guest = 3,
};

struct ADTs {
    Role role{};
    std::variant<std::monostate, int, std::string, Basic> multi_variant;

    auto operator==(const ADTs&) const -> bool = default;
};

struct HardMap {
    std::map<int, std::string> int_keyed_map;

    auto operator==(const HardMap&) const -> bool = default;
};

struct TreeNode {
    std::string name;
    std::vector<std::unique_ptr<TreeNode>> children;

    auto operator==(const TreeNode& other) const -> bool {
        if(name != other.name || children.size() != other.children.size()) {
            return false;
        }
        for(std::size_t index = 0; index < children.size(); ++index) {
            const auto& lhs_child = children[index];
            const auto& rhs_child = other.children[index];
            if(static_cast<bool>(lhs_child) != static_cast<bool>(rhs_child)) {
                return false;
            }
            if(lhs_child && *lhs_child != *rhs_child) {
                return false;
            }
        }
        return true;
    }
};

struct Ultimate {
    Basic basic;
    Compound compound;
    Nullables nullables;
    ADTs adts;
    HardMap hard_map;
    TreeNode root;

    auto operator==(const Ultimate&) const -> bool = default;
};

struct Scalars {
    bool b{};
    std::int8_t i8{};
    std::int16_t i16{};
    std::int32_t i32{};
    std::int64_t i64{};
    std::uint8_t u8{};
    std::uint16_t u16{};
    std::uint32_t u32{};
    std::uint64_t u64{};
    float f32{};
    double f64{};
    char ch{};
    std::string str;

    auto operator==(const Scalars&) const -> bool = default;
};

struct NestedContainers {
    std::vector<std::vector<int>> nested_vec;
    std::map<std::string, std::vector<int>> map_of_vecs;
    std::optional<std::vector<int>> opt_vec;

    auto operator==(const NestedContainers&) const -> bool = default;
};

struct EmptyContainers {
    std::vector<int> empty_vec;
    std::map<std::string, int> empty_map;
    std::string empty_str;
    std::optional<int> empty_opt;

    auto operator==(const EmptyContainers&) const -> bool = default;
};

template <typename Ptr>
inline auto pointer_value_equal(const Ptr& lhs, const Ptr& rhs) -> bool {
    if(static_cast<bool>(lhs) != static_cast<bool>(rhs)) {
        return false;
    }
    if(!lhs) {
        return true;
    }
    return *lhs == *rhs;
}

struct SmartPointers {
    std::unique_ptr<Basic> unique_basic;
    std::shared_ptr<Basic> shared_basic;
    std::shared_ptr<Basic> shared_empty;
    std::vector<std::shared_ptr<Basic>> shared_list;
    std::optional<std::shared_ptr<Basic>> opt_shared;

    auto operator==(const SmartPointers& other) const -> bool {
        if(!pointer_value_equal(unique_basic, other.unique_basic) ||
           !pointer_value_equal(shared_basic, other.shared_basic) ||
           !pointer_value_equal(shared_empty, other.shared_empty)) {
            return false;
        }

        if(shared_list.size() != other.shared_list.size()) {
            return false;
        }
        for(std::size_t i = 0; i < shared_list.size(); ++i) {
            if(!pointer_value_equal(shared_list[i], other.shared_list[i])) {
                return false;
            }
        }

        if(opt_shared.has_value() != other.opt_shared.has_value()) {
            return false;
        }
        if(!opt_shared.has_value()) {
            return true;
        }
        return pointer_value_equal(*opt_shared, *other.opt_shared);
    }
};

enum class AccessLevel : std::uint8_t {
    admin = 1,
    viewer = 2,
    guest = 3,
};

struct AttrProfile {
    std::string first;
    int age{};

    auto operator==(const AttrProfile&) const -> bool = default;
};

struct AttrPayload {
    int id{};
    rename_alias<std::string, "displayName", "name"> display_name;
    skip<int> internal_id = 1000;
    skip_if_none<std::string> note;
    flatten<AttrProfile> profile;
    enum_string<AccessLevel> level = AccessLevel::viewer;

    auto operator==(const AttrPayload&) const -> bool = default;
};

struct StructLevelPayload {
    int user_id{};
    int login_count{};

    auto operator==(const StructLevelPayload&) const -> bool = default;
};

using RenamedStructLevelPayload =
    annotation<StructLevelPayload, schema::rename_all<rename_policy::lower_camel>>;
using StrictRenamedStructLevelPayload = annotation<StructLevelPayload,
                                                   schema::rename_all<rename_policy::lower_camel>,
                                                   schema::deny_unknown_fields>;

using EnumStringAccess = enum_string<AccessLevel>;

using TaggedExternalVariant =
    annotation<std::variant<int, std::string, Basic>,
               schema::externally_tagged::names<"integer", "text", "basic">>;
using TaggedAdjacentVariant =
    annotation<std::variant<int, std::string, Basic>,
               schema::adjacently_tagged<"type", "value">::names<"integer", "text", "basic">>;

struct TaggedCircle {
    double radius{};

    auto operator==(const TaggedCircle&) const -> bool = default;
};

struct TaggedRect {
    double width{};
    double height{};

    auto operator==(const TaggedRect&) const -> bool = default;
};

using TaggedInternalVariant =
    annotation<std::variant<TaggedCircle, TaggedRect>,
               schema::internally_tagged<"kind">::names<"circle", "rect">>;

struct TaggedExternalHolder {
    std::string name;
    TaggedExternalVariant data;

    auto operator==(const TaggedExternalHolder&) const -> bool = default;
};

struct TaggedAdjacentHolder {
    std::string name;
    TaggedAdjacentVariant data;

    auto operator==(const TaggedAdjacentHolder&) const -> bool = default;
};

struct TaggedInternalHolder {
    std::string label;
    TaggedInternalVariant shape;

    auto operator==(const TaggedInternalHolder&) const -> bool = default;
};

using scalar_tuple = std::tuple<bool,
                                char,
                                std::int8_t,
                                std::uint8_t,
                                std::int16_t,
                                std::uint16_t,
                                std::int32_t,
                                std::uint32_t,
                                std::int64_t,
                                std::uint64_t,
                                float,
                                double,
                                std::string>;

inline auto make_basic(bool is_valid, std::int32_t i32, double f64, std::string text) -> Basic {
    return Basic{
        .is_valid = is_valid,
        .i32 = i32,
        .f64 = f64,
        .text = std::move(text),
    };
}

inline auto make_scalars() -> Scalars {
    return Scalars{
        .b = true,
        .i8 = -42,
        .i16 = -1000,
        .i32 = -100000,
        .i64 = -9999999999LL,
        .u8 = 200,
        .u16 = 50000,
        .u32 = 3000000000U,
        .u64 = 10000000000ULL,
        .f32 = 3.14F,
        .f64 = 2.718281828,
        .ch = 'Z',
        .str = "hello scalars",
    };
}

inline auto make_nested_containers() -> NestedContainers {
    return NestedContainers{
        .nested_vec = {{1, 2, 3}, {4, 5}, {6}},
        .map_of_vecs = {{"a", {10, 20}}, {"b", {30}}},
        .opt_vec = std::vector<int>{1, 2, 3},
    };
}

inline auto make_empty_containers() -> EmptyContainers {
    return EmptyContainers{
        .empty_vec = {},
        .empty_map = {},
        .empty_str = "",
        .empty_opt = std::nullopt,
    };
}

inline auto make_ultimate() -> Ultimate {
    Ultimate out;

    out.basic = Basic{
        .is_valid = true,
        .i32 = -42,
        .f64 = 3.1415926,
        .text = "basic",
    };

    out.compound = Compound{
        .string_list = {"alpha", "beta", "gamma"},
        .fixed_array = {1.5F,    -2.25F, 0.0F   },
        .heterogeneous_tuple = std::tuple<int, bool, std::string>{7,       true,   "tuple"},
    };

    out.nullables = Nullables{
        .opt_value = 17,
        .opt_empty = std::nullopt,
        .heap_allocated = std::make_unique<Basic>(Basic{
            .is_valid = false,
            .i32 = 128,
            .f64 = -9.75,
            .text = "heap",
        }),
    };

    out.adts = ADTs{
        .role = Role::user,
        .multi_variant =
            Basic{
                  .is_valid = true,
                  .i32 = 64,
                  .f64 = 2.5,
                  .text = "variant",
                  },
    };

    out.hard_map = HardMap{
        .int_keyed_map = {{-2, "minus-two"}, {0, "zero"}, {7, "seven"}},
    };

    out.root.name = "root";
    auto child_a = std::make_unique<TreeNode>();
    child_a->name = "child-a";
    auto leaf = std::make_unique<TreeNode>();
    leaf->name = "leaf";
    child_a->children.push_back(std::move(leaf));

    auto child_b = std::make_unique<TreeNode>();
    child_b->name = "child-b";

    out.root.children.push_back(std::move(child_a));
    out.root.children.push_back(std::move(child_b));

    return out;
}

inline auto make_smart_pointers() -> SmartPointers {
    SmartPointers out;

    out.unique_basic = std::make_unique<Basic>(make_basic(true, 77, 7.7, "uniq"));
    out.shared_basic = std::make_shared<Basic>(make_basic(false, -88, -8.8, "shared"));
    out.shared_empty.reset();

    out.shared_list.push_back(std::make_shared<Basic>(make_basic(true, 1, 1.1, "s0")));
    out.shared_list.push_back(nullptr);
    out.shared_list.push_back(std::make_shared<Basic>(make_basic(false, -2, -2.2, "s1")));

    out.opt_shared = std::make_shared<Basic>(make_basic(true, 303, 30.3, "opt"));
    return out;
}

inline auto make_attr_payload() -> AttrPayload {
    AttrPayload out{};
    out.id = 7;
    out.display_name = "alice";
    out.internal_id = 1000;
    out.note = std::string("note");
    annotated_value(out.profile).first = "Alice";
    annotated_value(out.profile).age = 30;
    out.level = AccessLevel::admin;
    return out;
}

inline auto make_renamed_struct_level_payload() -> RenamedStructLevelPayload {
    RenamedStructLevelPayload out{};
    out.user_id = 17;
    out.login_count = 99;
    return out;
}

inline auto make_strict_renamed_struct_level_payload() -> StrictRenamedStructLevelPayload {
    StrictRenamedStructLevelPayload out{};
    out.user_id = 1;
    out.login_count = 2;
    return out;
}

inline auto make_tagged_external_holder() -> TaggedExternalHolder {
    TaggedExternalHolder out{};
    out.name = "ext";
    out.data = Basic{
        .is_valid = true,
        .i32 = 64,
        .f64 = 2.5,
        .text = "external",
    };
    return out;
}

inline auto make_tagged_adjacent_holder() -> TaggedAdjacentHolder {
    TaggedAdjacentHolder out{};
    out.name = "adj";
    out.data = std::string("adjacent");
    return out;
}

inline auto make_tagged_internal_holder() -> TaggedInternalHolder {
    TaggedInternalHolder out{};
    out.label = "shape";
    out.shape = TaggedRect{
        .width = 10.0,
        .height = 20.0,
    };
    return out;
}

}  // namespace eventide::serde::standard_case

#define SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, ...)                                                   \
    {                                                                                              \
        auto input = __VA_ARGS__;                                                                  \
        auto output = (rt)(input);                                                                 \
        ASSERT_TRUE(output.has_value());                                                           \
        EXPECT_EQ(input, *output);                                                                 \
    }

#define SERDE_STANDARD_ASSERT_TEXT_DECODE_FAIL(decode_text, payload_literal, value)                \
    {                                                                                              \
        auto status = (decode_text)(payload_literal, value);                                       \
        EXPECT_FALSE(status.has_value());                                                          \
    }

/// Primitive scalars.

#define SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)                                                   \
    TEST_CASE(standard_primitives_roundtrip) {                                                     \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, true);                                                 \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::int32_t(-42));                                    \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::uint64_t(42));                                    \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, 3.5F);                                                 \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, 2.75);                                                 \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, 'x');                                                  \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::string("hello-serde"));                           \
    }

/// Numeric boundaries.

#define SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)                                           \
    TEST_CASE(standard_numeric_boundaries_roundtrip) {                                             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int8_t>::min());              \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int8_t>::max());              \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint8_t>::min());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint8_t>::max());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int16_t>::min());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int16_t>::max());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint16_t>::min());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint16_t>::max());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int32_t>::min());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int32_t>::max());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint32_t>::min());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint32_t>::max());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int64_t>::min());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int64_t>::max());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint64_t>::min());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint64_t>::max());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<float>::lowest());                 \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<float>::max());                    \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<double>::lowest());                \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<double>::max());                   \
    }

#define SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES_TOML_SAFE(rt)                                 \
    TEST_CASE(standard_numeric_boundaries_roundtrip) {                                             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int8_t>::min());              \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int8_t>::max());              \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint8_t>::min());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint8_t>::max());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int16_t>::min());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int16_t>::max());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint16_t>::min());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint16_t>::max());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int32_t>::min());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int32_t>::max());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint32_t>::min());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint32_t>::max());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int64_t>::min());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::int64_t>::max());             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<std::uint64_t>::min());            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<float>::lowest());                 \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<float>::max());                    \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<double>::lowest());                \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::numeric_limits<double>::max());                   \
    }

/// Tuple-like (tuple / pair / array).

#define SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)                                                   \
    TEST_CASE(standard_tuple_like_roundtrip) {                                                     \
        using basic_t = eventide::serde::standard_case::Basic;                                     \
        using scalar_tuple_t = eventide::serde::standard_case::scalar_tuple;                       \
        const scalar_tuple_t scalar_a{true,                                                        \
                                      'q',                                                         \
                                      std::int8_t(-12),                                            \
                                      std::uint8_t(210),                                           \
                                      std::int16_t(-1024),                                         \
                                      std::uint16_t(4096),                                         \
                                      std::int32_t(-123456),                                       \
                                      std::uint32_t(123456),                                       \
                                      std::int64_t(-9876543210LL),                                 \
                                      std::uint64_t(9876543210ULL),                                \
                                      1.5F,                                                        \
                                      -9.25,                                                       \
                                      std::string("all-scalars-a")};                               \
        const scalar_tuple_t scalar_b{false,                                                       \
                                      'z',                                                         \
                                      std::int8_t(-8),                                             \
                                      std::uint8_t(8),                                             \
                                      std::int16_t(-16),                                           \
                                      std::uint16_t(16),                                           \
                                      std::int32_t(-32),                                           \
                                      std::uint32_t(32),                                           \
                                      std::int64_t(-64),                                           \
                                      std::uint64_t(64),                                           \
                                      -0.75F,                                                      \
                                      -12.125,                                                     \
                                      std::string("all-scalars-b")};                               \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::pair<int, std::string>{9, "pair"});               \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::pair<std::uint64_t, bool>{42ULL, true});          \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            std::pair<basic_t, basic_t>{                                                           \
                eventide::serde::standard_case::make_basic(true, 11, 1.5, "lhs"),                  \
                eventide::serde::standard_case::make_basic(false, -22, -2.5, "rhs")});             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::tuple<int, bool, std::string>{7, true, "tuple"}); \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, scalar_a);                                             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            std::tuple<basic_t, std::array<int, 3>, std::pair<std::uint32_t, float>>{              \
                eventide::serde::standard_case::make_basic(true, 33, 3.75, "tuple-struct"),        \
                std::array<int, 3>{1, 3, 5},                                                       \
                std::pair<std::uint32_t, float>{99U, 6.25F} \
        });                                     \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::array<int, 3>{4, 5, 6});                          \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            std::array<basic_t, 2>{                                                                \
                eventide::serde::standard_case::make_basic(true, 101, 10.1, "arr-a"),              \
                eventide::serde::standard_case::make_basic(false, -202, -20.2, "arr-b")});         \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            std::pair<scalar_tuple_t, scalar_tuple_t>{scalar_a, scalar_b});                        \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::array<scalar_tuple_t, 2>{scalar_a, scalar_b});    \
    }

/// Sequence / set containers (vector / set / unordered_set).

#define SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)                                                 \
    TEST_CASE(standard_sequence_set_roundtrip) {                                                   \
        using basic_t = eventide::serde::standard_case::Basic;                                     \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::vector<int>{1, 2, 3, 5});                         \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::vector<std::uint64_t>{1ULL, 5ULL, 9ULL});         \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::vector<std::string>{"a", "bb", "ccc"});           \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::vector<std::byte>{std::byte{0}, std::byte{127}}); \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            std::vector<basic_t>{                                                                  \
                eventide::serde::standard_case::make_basic(true, 1, 1.25, "v0"),                   \
                eventide::serde::standard_case::make_basic(false, -2, -2.5, "v1")});               \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::vector<std::tuple<std::int32_t, bool, std::string>>{  \
                                            {1, true,  "x"},                                        \
                                            {2, false, "y"} \
        });                                     \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            std::vector<std::array<int, 3>>{{{1, 2, 3}}, {{4, 5, 6}}});                            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::set<int>{1, 3, 5, 7});                            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::set<std::string>{"alpha", "beta", "gamma"});      \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::set<std::pair<int, std::string>>{                     \
                                            {1, "a"},                                              \
                                            {2, "b"} \
        });                                            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::unordered_set<int>{2, 4, 6, 8});                  \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::unordered_set<std::string>{"one", "two", "three"});   \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::unordered_set<std::uint64_t>{11ULL, 22ULL, 33ULL});   \
    }

/// Map containers (map / unordered_map).

#define SERDE_STANDARD_TEST_CASES_MAPS(rt)                                                         \
    TEST_CASE(standard_map_roundtrip) {                                                            \
        using basic_t = eventide::serde::standard_case::Basic;                                     \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::map<std::string, int>{                                \
                                            {"a", 1},                                              \
                                            {"b", 2} \
        });                                            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::map<int, std::string>{                                \
                                            {-2, "minus-two"},                                     \
                                            {0,  "zero"     },                                           \
                                            {7,  "seven"    } \
        });                                        \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            std::map<std::string, basic_t>{                                                        \
                {"x", eventide::serde::standard_case::make_basic(true,  10,  1.0,  "mx")},            \
                {"y", eventide::serde::standard_case::make_basic(false, -20, -2.0, "my")} \
        });       \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::map<int, std::tuple<bool, double, std::string>>{      \
                                            {1, {true, 1.25, "one"} },                              \
                                            {2, {false, -2.5, "two"}} \
        });                           \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::unordered_map<std::string, int>{                      \
                                            {"k1", 1},                                             \
                                            {"k2", 2} \
        });                                           \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::unordered_map<int, std::string>{                      \
                                            {1, "one"  },                                            \
                                            {2, "two"  },                                            \
                                            {3, "three"} \
        });                                        \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            std::unordered_map<std::string, basic_t>{                                              \
                {"u1", eventide::serde::standard_case::make_basic(true,  7,  7.7,  "ux")},            \
                {"u2", eventide::serde::standard_case::make_basic(false, -8, -8.8, "uy")} \
        });       \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::unordered_map<std::uint64_t, std::vector<int>>{       \
                                            {10ULL, {1, 2}   },                                       \
                                            {20ULL, {3, 4, 5}} \
        });                                  \
    }

/// Optional values.

#define SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)                                                     \
    TEST_CASE(standard_optional_roundtrip) {                                                       \
        using basic_t = eventide::serde::standard_case::Basic;                                     \
        using scalar_tuple_t = eventide::serde::standard_case::scalar_tuple;                       \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::optional<int>{17});                               \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::optional<int>{std::nullopt});                     \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::optional<double>{-3.5});                          \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::optional<std::string>{std::string("opt-text")});  \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            std::optional<basic_t>{                                                                \
                eventide::serde::standard_case::make_basic(true, 55, 5.5, "opt-basic")});          \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::optional<basic_t>{std::nullopt});                 \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::optional<scalar_tuple_t>{                             \
                                            scalar_tuple_t{true,                                   \
                                                           'o', std::int8_t(-3),                        \
                                                           std::uint8_t(3),                        \
                                                           std::int16_t(-30),                      \
                                                           std::uint16_t(30),                      \
                                                           std::int32_t(-300),                     \
                                                           std::uint32_t(300),                     \
                                                           std::int64_t(-3000),                    \
                                                           std::uint64_t(3000),                    \
                                                           3.0F, 3.0,                                    \
                                                           std::string("opt-tuple")} \
        });            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        std::optional<std::vector<int>>{                           \
                                            std::vector<int>{4, 5, 6} \
        });                           \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, std::optional<std::vector<int>>{std::nullopt});        \
    }

/// Smart pointers (unique_ptr / shared_ptr).

#define SERDE_STANDARD_TEST_CASES_POINTERS(rt)                                                     \
    TEST_CASE(standard_smart_pointers_roundtrip) {                                                 \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        eventide::serde::standard_case::make_smart_pointers());    \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_smart_pointers();                    \
            input.unique_basic.reset();                                                            \
            input.shared_basic.reset();                                                            \
            input.opt_shared.reset();                                                              \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(input, *output);                                                             \
        }                                                                                          \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_smart_pointers();                    \
            input.shared_list.clear();                                                             \
            input.shared_list.push_back(nullptr);                                                  \
            input.shared_list.push_back(std::make_shared<eventide::serde::standard_case::Basic>(   \
                eventide::serde::standard_case::make_basic(true, 9, 0.9, "tail")));                \
            input.opt_shared = std::shared_ptr<eventide::serde::standard_case::Basic>{};           \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(input, *output);                                                             \
        }                                                                                          \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_smart_pointers();                    \
            auto aliased = std::make_shared<eventide::serde::standard_case::Basic>(                \
                eventide::serde::standard_case::make_basic(true, 1234, 12.34, "aliased"));         \
            input.shared_basic = aliased;                                                          \
            input.shared_list = {aliased, aliased};                                                \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            ASSERT_EQ(output->shared_list.size(), 2U);                                             \
            ASSERT_TRUE(output->shared_list[0] != nullptr);                                        \
            ASSERT_TRUE(output->shared_list[1] != nullptr);                                        \
            EXPECT_EQ(*output->shared_list[0], *output->shared_list[1]);                           \
        }                                                                                          \
    }

#define SERDE_STANDARD_TEST_CASES_POINTERS_WIRE_SAFE(rt)                                           \
    TEST_CASE(standard_smart_pointers_roundtrip) {                                                 \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        eventide::serde::standard_case::make_smart_pointers());    \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_smart_pointers();                    \
            input.unique_basic.reset();                                                            \
            input.shared_basic.reset();                                                            \
            input.opt_shared.reset();                                                              \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(input, *output);                                                             \
        }                                                                                          \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_smart_pointers();                    \
            auto aliased = std::make_shared<eventide::serde::standard_case::Basic>(                \
                eventide::serde::standard_case::make_basic(true, 1234, 12.34, "aliased"));         \
            input.shared_basic = aliased;                                                          \
            input.shared_list = {aliased, aliased};                                                \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            ASSERT_EQ(output->shared_list.size(), 2U);                                             \
            ASSERT_TRUE(output->shared_list[0] != nullptr);                                        \
            ASSERT_TRUE(output->shared_list[1] != nullptr);                                        \
            EXPECT_EQ(*output->shared_list[0], *output->shared_list[1]);                           \
        }                                                                                          \
    }

#define SERDE_STANDARD_TEST_CASES_POINTERS_TEXT_SAFE(rt)                                           \
    SERDE_STANDARD_TEST_CASES_POINTERS_WIRE_SAFE(rt)

#define SERDE_STANDARD_TEST_CASES_POINTERS_TOML_SAFE(rt)                                           \
    TEST_CASE(standard_smart_pointers_roundtrip) {                                                 \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_smart_pointers();                    \
            input.shared_list.erase(                                                               \
                std::remove(input.shared_list.begin(), input.shared_list.end(), nullptr),          \
                input.shared_list.end());                                                          \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(input, *output);                                                             \
        }                                                                                          \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_smart_pointers();                    \
            input.shared_list.erase(                                                               \
                std::remove(input.shared_list.begin(), input.shared_list.end(), nullptr),          \
                input.shared_list.end());                                                          \
            input.unique_basic.reset();                                                            \
            input.shared_basic.reset();                                                            \
            input.opt_shared.reset();                                                              \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(input, *output);                                                             \
        }                                                                                          \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_smart_pointers();                    \
            auto aliased = std::make_shared<eventide::serde::standard_case::Basic>(                \
                eventide::serde::standard_case::make_basic(true, 1234, 12.34, "aliased"));         \
            input.shared_basic = aliased;                                                          \
            input.shared_empty.reset();                                                            \
            input.shared_list = {aliased, aliased};                                                \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            ASSERT_EQ(output->shared_list.size(), 2U);                                             \
            ASSERT_TRUE(output->shared_list[0] != nullptr);                                        \
            ASSERT_TRUE(output->shared_list[1] != nullptr);                                        \
            EXPECT_EQ(*output->shared_list[0], *output->shared_list[1]);                           \
        }                                                                                          \
    }

/// Attribute behaviors and schema attrs.

#define SERDE_STANDARD_TEST_CASES_ATTRS(rt)                                                        \
    TEST_CASE(standard_attrs_roundtrip) {                                                          \
        using payload_t = eventide::serde::standard_case::AttrPayload;                             \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_attr_payload();                      \
            input.internal_id = 4242;                                                              \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(output->id, input.id);                                                       \
            EXPECT_EQ(eventide::serde::annotated_value(output->display_name),                      \
                      std::string("alice"));                                                       \
            EXPECT_EQ(eventide::serde::annotated_value(output->internal_id), 1000);                \
            EXPECT_EQ(eventide::serde::annotated_value(output->note),                              \
                      std::optional<std::string>{"note"});                                         \
            EXPECT_EQ(eventide::serde::annotated_value(output->profile),                           \
                      eventide::serde::annotated_value(input.profile));                            \
            EXPECT_EQ(eventide::serde::annotated_value(output->level),                             \
                      eventide::serde::standard_case::AccessLevel::admin);                         \
        }                                                                                          \
        {                                                                                          \
            payload_t input{};                                                                     \
            input.id = 9;                                                                          \
            input.display_name = "bob";                                                            \
            eventide::serde::annotated_value(input.note) = std::nullopt;                           \
            eventide::serde::annotated_value(input.profile).first = "Bob";                         \
            eventide::serde::annotated_value(input.profile).age = 21;                              \
            input.level = eventide::serde::standard_case::AccessLevel::guest;                      \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(output->id, 9);                                                              \
            EXPECT_EQ(eventide::serde::annotated_value(output->display_name), std::string("bob")); \
            EXPECT_EQ(eventide::serde::annotated_value(output->note), std::nullopt);               \
            EXPECT_EQ(eventide::serde::annotated_value(output->profile).first, "Bob");             \
            EXPECT_EQ(eventide::serde::annotated_value(output->profile).age, 21);                  \
            EXPECT_EQ(eventide::serde::annotated_value(output->level),                             \
                      eventide::serde::standard_case::AccessLevel::guest);                         \
        }                                                                                          \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            eventide::serde::standard_case::make_renamed_struct_level_payload());                  \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            eventide::serde::standard_case::make_strict_renamed_struct_level_payload());           \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        eventide::serde::standard_case::EnumStringAccess{          \
                                            eventide::serde::standard_case::AccessLevel::viewer}); \
    }

/// Tagged variants (external / adjacent / internal).

#define SERDE_STANDARD_TEST_CASES_TAGGED_VARIANTS(rt)                                              \
    TEST_CASE(standard_tagged_variants_roundtrip) {                                                \
        using ext_t = eventide::serde::standard_case::TaggedExternalVariant;                       \
        using adj_t = eventide::serde::standard_case::TaggedAdjacentVariant;                       \
        using int_t = eventide::serde::standard_case::TaggedInternalVariant;                       \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, ext_t{42});                                            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, ext_t{std::string("hello")});                          \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        ext_t{                                                     \
                                            eventide::serde::standard_case::Basic{                 \
                                                                                  .is_valid = true,                                  \
                                                                                  .i32 = 11,                                         \
                                                                                  .f64 = 1.25,                                       \
                                                                                  .text = "ext-basic",                               \
                                                                                  } \
        });                                                   \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            eventide::serde::standard_case::make_tagged_external_holder());                        \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, adj_t{9});                                             \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, adj_t{std::string("adj")});                            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            eventide::serde::standard_case::make_tagged_adjacent_holder());                        \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            int_t{eventide::serde::standard_case::TaggedCircle{.radius = 3.14}});                  \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            int_t{                                                                                 \
                eventide::serde::standard_case::TaggedRect{.width = 10.0, .height = 20.0} \
        });       \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            eventide::serde::standard_case::make_tagged_internal_holder());                        \
    }

/// Text decoder error paths.
/// decode_text signature:
///   template <typename T>
///   auto decode_text(std::string_view text, T& out) -> std::expected<void, E>;

#define SERDE_STANDARD_TEST_CASES_ERROR_PATHS_TEXT(decode_text)                                    \
    TEST_CASE(standard_error_paths_text) {                                                         \
        std::int32_t i32 = 0;                                                                      \
        SERDE_STANDARD_ASSERT_TEXT_DECODE_FAIL(decode_text, R"("bad-int")", i32);                  \
        eventide::serde::standard_case::StrictRenamedStructLevelPayload strict{};                  \
        SERDE_STANDARD_ASSERT_TEXT_DECODE_FAIL(decode_text,                                        \
                                               R"({"userId":3,"loginCount":4,"extra":9})",         \
                                               strict);                                            \
        eventide::serde::standard_case::TaggedExternalVariant ext{};                               \
        SERDE_STANDARD_ASSERT_TEXT_DECODE_FAIL(decode_text, R"({"unknown":42})", ext);             \
        eventide::serde::standard_case::TaggedAdjacentVariant adj{};                               \
        SERDE_STANDARD_ASSERT_TEXT_DECODE_FAIL(decode_text,                                        \
                                               R"({"type":"unknown","value":42})",                 \
                                               adj);                                               \
        eventide::serde::standard_case::TaggedInternalVariant internal{};                          \
        SERDE_STANDARD_ASSERT_TEXT_DECODE_FAIL(decode_text, R"({"radius":5.0})", internal);        \
    }

/// Variant values.

#define SERDE_STANDARD_TEST_CASES_VARIANT(rt)                                                      \
    TEST_CASE(standard_variant_roundtrip) {                                                        \
        using basic_t = eventide::serde::standard_case::Basic;                                     \
        using primary_variant_t = std::variant<std::monostate, int, double, std::string, basic_t>; \
        using nested_variant_t = std::                                                             \
            variant<std::tuple<int, std::string>, std::vector<int>, std::array<int, 3>, basic_t>;  \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, primary_variant_t{std::in_place_index<0>});            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, primary_variant_t{123});                               \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, primary_variant_t{2.75});                              \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, primary_variant_t{std::string("variant-text")});       \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            primary_variant_t{                                                                     \
                eventide::serde::standard_case::make_basic(true, 64, 2.5, "variant-basic")});      \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        nested_variant_t{                                          \
                                            std::tuple<int, std::string>{7, "seven"} \
        });            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        nested_variant_t{                                          \
                                            std::vector<int>{1, 2, 3} \
        });                           \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        nested_variant_t{                                          \
                                            std::array<int, 3>{8, 9, 10} \
        });                        \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            nested_variant_t{                                                                      \
                eventide::serde::standard_case::make_basic(false, -9, -1.25, "nested")});          \
    }

#define SERDE_STANDARD_TEST_CASES_VARIANT_WIRE_SAFE(rt)                                            \
    TEST_CASE(standard_variant_roundtrip) {                                                        \
        using basic_t = eventide::serde::standard_case::Basic;                                     \
        using primary_variant_t = std::variant<std::monostate, int, double, std::string, basic_t>; \
        using nested_variant_t =                                                                   \
            std::variant<std::tuple<int, std::string>, std::vector<int>, basic_t>;                 \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, primary_variant_t{std::in_place_index<0>});            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, primary_variant_t{123});                               \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, primary_variant_t{2.75});                              \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, primary_variant_t{std::string("variant-text")});       \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            primary_variant_t{                                                                     \
                eventide::serde::standard_case::make_basic(true, 64, 2.5, "variant-basic")});      \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        nested_variant_t{                                          \
                                            std::tuple<int, std::string>{7, "seven"} \
        });            \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        nested_variant_t{                                          \
                                            std::vector<int>{1, 2, 3} \
        });                           \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(                                                           \
            rt,                                                                                    \
            nested_variant_t{                                                                      \
                eventide::serde::standard_case::make_basic(false, -9, -1.25, "nested")});          \
    }

#define SERDE_STANDARD_TEST_CASES_VARIANT_TEXT_SAFE(rt)                                            \
    SERDE_STANDARD_TEST_CASES_VARIANT_WIRE_SAFE(rt)

/// All container-like and sum-type groups.

#define SERDE_STANDARD_TEST_CASES_STL_CONTAINERS(rt)                                               \
    SERDE_STANDARD_TEST_CASES_TUPLE_LIKE(rt)                                                       \
    SERDE_STANDARD_TEST_CASES_SEQUENCE_SET(rt)                                                     \
    SERDE_STANDARD_TEST_CASES_MAPS(rt)                                                             \
    SERDE_STANDARD_TEST_CASES_OPTIONAL(rt)                                                         \
    SERDE_STANDARD_TEST_CASES_POINTERS(rt)                                                         \
    SERDE_STANDARD_TEST_CASES_VARIANT(rt)

/// Extended schema/annotation/tagged coverage.

#define SERDE_STANDARD_TEST_CASES_EXTENDED(rt)                                                     \
    SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)                                               \
    SERDE_STANDARD_TEST_CASES_ATTRS(rt)                                                            \
    SERDE_STANDARD_TEST_CASES_TAGGED_VARIANTS(rt)

/// Complex composed structs.

#define SERDE_STANDARD_TEST_CASES_COMPLEX(rt)                                                      \
    TEST_CASE(standard_complex_roundtrip) {                                                        \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, eventide::serde::standard_case::make_scalars());       \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        eventide::serde::standard_case::make_nested_containers()); \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt,                                                        \
                                        eventide::serde::standard_case::make_empty_containers());  \
        SERDE_STANDARD_ASSERT_ROUNDTRIP(rt, eventide::serde::standard_case::make_ultimate());      \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_ultimate();                          \
            input.adts.multi_variant = std::monostate{};                                           \
            input.nullables.opt_value.reset();                                                     \
            input.nullables.heap_allocated.reset();                                                \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(input, *output);                                                             \
        }                                                                                          \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_ultimate();                          \
            input.adts.multi_variant = 123;                                                        \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(input, *output);                                                             \
        }                                                                                          \
        {                                                                                          \
            auto input = eventide::serde::standard_case::make_ultimate();                          \
            input.adts.multi_variant = std::string("variant-text");                                \
            auto output = (rt)(input);                                                             \
            ASSERT_TRUE(output.has_value());                                                       \
            EXPECT_EQ(input, *output);                                                             \
        }                                                                                          \
    }

/// Full suite.

#define SERDE_STANDARD_TEST_CASES_ALL(rt)                                                          \
    SERDE_STANDARD_TEST_CASES_PRIMITIVES(rt)                                                       \
    SERDE_STANDARD_TEST_CASES_NUMERIC_BOUNDARIES(rt)                                               \
    SERDE_STANDARD_TEST_CASES_STL_CONTAINERS(rt)                                                   \
    SERDE_STANDARD_TEST_CASES_ATTRS(rt)                                                            \
    SERDE_STANDARD_TEST_CASES_TAGGED_VARIANTS(rt)                                                  \
    SERDE_STANDARD_TEST_CASES_COMPLEX(rt)
