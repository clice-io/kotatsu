#pragma once

// Golden-value factories — canonical populated instances for round-trip tests.

#include <cstdint>
#include <string>
#include <utility>

#include "fixtures/schema/containers.h"
#include "fixtures/schema/enums.h"
#include "fixtures/schema/primitives.h"
#include "fixtures/schema/recursive.h"
#include "fixtures/schema/rename.h"
#include "fixtures/schema/schema_attrs.h"

namespace kota::meta::fixtures {

inline auto make_simple() -> SimpleStruct {
    return {.x = 42, .name = "simple", .score = 3.5F};
}

inline auto make_all_primitives() -> AllPrimitives {
    return {
        .b = true,
        .i8 = -8,
        .i16 = -16,
        .i32 = -32,
        .i64 = -64,
        .u8 = 8,
        .u16 = 16,
        .u32 = 32,
        .u64 = 64,
        .f32 = 0.5F,
        .f64 = 0.25,
        .c = 'k',
        .s = "kota",
    };
}

inline auto make_single_field() -> SingleFieldStruct {
    return {.only = 1};
}

inline auto make_nested() -> NestedStruct {
    return {
        .items = {make_simple(), {.x = 7, .name = "b", .score = 1.0F}}
    };
}

inline auto make_tree() -> TreeNode {
    TreeNode root{.value = "root", .children = {}};
    root.children.push_back({.value = "leaf-1", .children = {}});
    root.children.push_back({.value = "leaf-2", .children = {}});
    return root;
}

inline auto make_map_recursive() -> MapRecursive {
    MapRecursive root{.name = "root", .nested = {}};
    root.nested.emplace("child", MapRecursive{.name = "child", .nested = {}});
    return root;
}

inline auto make_optional_recursive() -> OptionalRecursive {
    OptionalRecursive root{.id = 1, .sub_items = std::nullopt};
    return root;
}

inline auto make_annotated() -> AnnotatedStruct {
    return {.user_id = 99, .internal = "hidden", .value = 2.0F};
}

inline auto make_alias() -> AliasStruct {
    return {.id = 5, .name = "ada"};
}

inline auto make_inner() -> Inner {
    return {.a = 1, .b = 2};
}

inline auto make_outer() -> Outer {
    return {.x = 10, .inner = make_inner(), .y = 20};
}

inline auto make_deep_outer() -> DeepOuter {
    DeepOuter v{};
    v.head = 100;
    v.mid.m = 50;
    v.mid.deep.p = 1;
    v.mid.deep.q = 2;
    v.tail = 200;
    return v;
}

inline auto make_default_literal() -> DefaultLiteralStruct {
    return {.with_default = 0, .version = "v1", .plain = 3};
}

inline auto make_rename_target() -> RenameTarget {
    return {.user_name = 7, .display_name = "ada"};
}

inline auto make_rename_all_target() -> RenameAllTarget {
    return {.user_name = 1, .total_score = 99.5F, .item_id = "abc"};
}

}  // namespace kota::meta::fixtures
