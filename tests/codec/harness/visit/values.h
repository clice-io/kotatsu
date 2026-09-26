#pragma once

// Values: every type_kind the protocol encodes and decodes, at the root and
// inside composite structs, with the boundary values of each scalar kind.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "codec/harness/visit/kit.h"
#include "fixtures/containers.h"
#include "fixtures/enums.h"
#include "fixtures/everything.h"
#include "fixtures/recursive.h"
#include "fixtures/scalars.h"
#include "fixtures/structs.h"

namespace kota::test {

template <Backend B>
void values(const Kit<B>& kit) {
    roundtrip(kit, "bool_roundtrip", [] { return true; });
    roundtrip(kit, "int8_lowest_roundtrip", [] {
        return std::numeric_limits<std::int8_t>::lowest();
    });
    roundtrip(kit, "int64_lowest_roundtrip", [] {
        return std::numeric_limits<std::int64_t>::lowest();
    });
    if constexpr(B::caps.full_uint64) {
        roundtrip(kit, "uint64_highest_roundtrip", [] {
            return std::numeric_limits<std::uint64_t>::max();
        });
    } else {
        write_fails(kit,
                    "uint64_highest_fails",
                    [] { return std::numeric_limits<std::uint64_t>::max(); },
                    {.message = "", .path = ""});
    }
    roundtrip(kit, "double_roundtrip", [] { return -2.718281828459045; });
    roundtrip(kit, "float_roundtrip", [] { return 3.14F; });
    roundtrip(kit, "char_roundtrip", [] { return 'Z'; });
    roundtrip(kit, "string_roundtrip", [] { return std::string("hello"); });
    roundtrip(kit, "enum_roundtrip", [] { return SignedEnum::neg; });
    roundtrip(kit, "monostate_roundtrip", [] { return std::monostate{}; });

    roundtrip(kit, "scalars_typical_roundtrip", [] { return Scalars::typical(); });
    roundtrip(kit, "scalars_lowest_roundtrip", [] { return Scalars::lowest(); });
    roundtrip(kit, "scalars_highest_roundtrip", [] { return Scalars::highest(); });
    roundtrip(kit, "strings_roundtrip", [] { return Strings::typical(); });
    roundtrip(kit, "bytes_roundtrip", [] { return Bytes::typical(); });
    roundtrip(kit, "nullables_engaged_roundtrip", [] { return Nullables::engaged(); });
    roundtrip(kit, "nullables_empty_roundtrip", [] { return Nullables{}; });
    roundtrip(kit, "sequences_roundtrip", [] { return Sequences::typical(); });
    roundtrip(kit, "sets_roundtrip", [] { return Sets::typical(); });
    reads<std::unordered_set<int>>(
        kit,
        "unordered_set_reads_every_element",
        [] { return std::vector<int>{2, 4, 6, 8}; },
        [] { return std::unordered_set<int>{2, 4, 6, 8}; });
    roundtrip(kit, "maps_roundtrip", [] { return Maps::typical(); });
    reads<std::unordered_map<std::string, int>>(
        kit,
        "unordered_map_reads_every_entry",
        [] {
            return std::map<std::string, int>{
                {"a", 1},
                {"b", 2},
                {"c", 3}
            };
        },
        [] {
            return std::unordered_map<std::string, int>{
                {"a", 1},
                {"b", 2},
                {"c", 3}
            };
        });
    roundtrip(kit, "tuples_roundtrip", [] { return Tuples::typical(); });
    if constexpr(B::caps.null_elements) {
        roundtrip(kit, "null_elements_roundtrip", [] { return NullElements::typical(); });
    } else {
        write_fails(kit,
                    "null_elements_fails",
                    [] { return NullElements::typical(); },
                    {.message = "", .path = "optionals[1]"});
    }
    if constexpr(B::caps.struct_keys) {
        roundtrip(kit, "struct_keys_roundtrip", [] {
            return std::map<Point, int>{
                {{.x = 1, .y = 2},  1},
                {{.x = -1, .y = 0}, 2}
            };
        });
    }

    roundtrip(kit, "tree_roundtrip", [] {
        return TreeNode{
            .value = "root",
            .children = {{.value = "a", .children = {{.value = "a1", .children = {}}}},
                         {.value = "b", .children = {}}},
        };
    });
    roundtrip(kit, "linked_list_roundtrip", [] {
        auto tail = std::make_unique<LinkedNode>(LinkedNode{.data = 3, .next = nullptr});
        auto middle = std::make_unique<LinkedNode>(LinkedNode{.data = 2, .next = std::move(tail)});
        return LinkedNode{.data = 1, .next = std::move(middle)};
    });
    roundtrip(kit, "map_tree_roundtrip", [] {
        MapRecursive root{.name = "root", .nested = {}};
        root.nested.emplace("child", MapRecursive{.name = "child", .nested = {}});
        return root;
    });
    roundtrip(kit, "empty_roundtrip", [] { return Empty{}; });

    roundtrip(kit, "everything_roundtrip", [] { return Everything::typical(); });
    roundtrip(kit, "everything_default_roundtrip", [] { return Everything{}; });
    snapshot(kit, "lowering", [] { return Everything::typical(); });
    if constexpr(B::caps.untrusted_input) {
        hostile(kit, "hostile_everything", [] { return Everything::typical(); });
    }

    if constexpr(B::caps.self_describing) {
        // The leaf error is the backend's to word; its path is the protocol's.
        read_fails<Field<std::int8_t>>(kit,
                                       "int8_out_of_range_fails",
                                       [] { return Field<int>{300}; },
                                       {.message = "", .path = "value"});
        read_fails<Field<UInt8Enum>>(kit,
                                     "enum_out_of_range_fails",
                                     [] { return Field<int>{300}; },
                                     {.message = "", .path = "value"});
        read_fails<Field<std::nullptr_t>>(kit,
                                          "null_from_non_null_fails",
                                          [] { return Field<int>{0}; },
                                          {.message = "", .path = "value"});
    }
}

}  // namespace kota::test
