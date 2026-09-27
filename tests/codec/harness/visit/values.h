#pragma once

// Values: every type_kind the protocol encodes and decodes, at the root and
// inside composite structs, with the boundary values of each scalar kind.

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
#include <variant>
#include <vector>

#include "codec/harness/fixtures/containers.h"
#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/everything.h"
#include "codec/harness/fixtures/scalars.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/harness/visit/kit.h"
#include "fixtures/recursive.h"

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
    // A char's value, 0-255, is one codepoint, which a text backend writes
    // in two bytes of UTF-8 above 0x7F.
    roundtrip(kit, "char_above_ascii_roundtrip", [] { return static_cast<char>(0x80); });
    roundtrip(kit, "string_roundtrip", [] { return std::string("hello"); });
    // A C string writes its text; a null one has no text and writes null.
    encodes_as(
        kit,
        "c_string_encodes_as_string",
        [] { return Field<const char*>{"text"}; },
        [] { return Field<std::string>{"text"}; });
    encodes_as(
        kit,
        "null_c_string_encodes_as_null",
        [] { return Field<const char*>{nullptr}; },
        [] { return Field<std::nullptr_t>{nullptr}; });
    // A weak pointer writes what the shared pointer it locks to writes.
    encodes_as(
        kit,
        "weak_ptr_encodes_as_shared_ptr",
        [] {
            const static auto owner = std::make_shared<int>(7);
            return Field<std::weak_ptr<int>>{owner};
        },
        [] { return Field<std::shared_ptr<int>>{std::make_shared<int>(7)}; });
    encodes_as(
        kit,
        "c_string_map_key_encodes_as_string_key",
        [] { return Field<std::map<const char*, int>>{{{"k", 1}}}; },
        [] { return Field<std::map<std::string, int>>{{{"k", 1}}}; });
    if constexpr(B::caps.self_describing) {
        // A keyed document's map key is text, with no null to write.
        write_fails(kit,
                    "null_c_string_map_key_fails",
                    [] { return Field<std::map<const char*, int>>{{{nullptr, 1}}}; },
                    {.message = "null C string map key", .path = "value[0]"});
    }
    roundtrip(kit, "enum_roundtrip", [] { return SignedEnum::neg; });
    roundtrip(kit, "unsigned_enum_roundtrip", [] { return UInt8Enum::c; });
    roundtrip(kit, "char_enum_roundtrip", [] { return Letter::z; });
    roundtrip(kit, "monostate_roundtrip", [] { return std::monostate{}; });

    roundtrip(kit, "scalars_typical_roundtrip", [] { return Scalars::typical(); });
    roundtrip(kit, "scalars_lowest_roundtrip", [] { return Scalars::lowest(); });
    roundtrip(kit, "scalars_highest_roundtrip", [] { return Scalars::highest(); });
    roundtrip(kit, "strings_roundtrip", [] { return Strings::typical(); });
    roundtrip(kit, "bytes_roundtrip", [] { return Bytes::typical(); });
    roundtrip(kit, "nullables_engaged_roundtrip", [] { return Nullables::engaged(); });
    // Decoded over engaged values, so a null that did not reset its field
    // shows. Where nulls do not travel, a null field is absent, and an
    // absent field is left alone.
    roundtrip_over(
        kit,
        "nullables_empty_roundtrip",
        [] { return Nullables{}; },
        [] { return B::caps.nested_nulls ? Nullables::engaged() : Nullables{}; });
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
    // Tuple-likes at the root and as elements, which a backend may lay out
    // otherwise than as fields.
    roundtrip(kit, "tuple_root_roundtrip", [] {
        return std::tuple<int, std::string, double>{7, "seven", 2.5};
    });
    roundtrip(kit, "pair_root_roundtrip", [] {
        return std::pair<std::string, Point>{
            "p",
            {.x = 1, .y = 2}
        };
    });
    roundtrip(kit, "array_root_roundtrip", [] { return std::array<int, 3>{1, 2, 3}; });
    roundtrip(kit, "tuple_elements_roundtrip", [] {
        return std::vector<std::tuple<int, std::string>>{
            {1, "one"},
            {2, "two"}
        };
    });
    roundtrip(kit, "array_elements_roundtrip", [] {
        return std::vector<std::array<int, 2>>{
            {1, 2},
            {3, 4}
        };
    });
    roundtrip(kit, "pair_set_roundtrip", [] {
        return std::set<std::pair<int, std::string>>{
            {1, "one"},
            {2, "two"}
        };
    });
    if constexpr(B::caps.nested_nulls) {
        roundtrip(kit, "null_elements_roundtrip", [] { return NullElements::typical(); });
    } else {
        write_fails(kit,
                    "null_elements_fails",
                    [] { return NullElements::typical(); },
                    {.message = "", .path = "optionals[1]"});
    }
    // A keyed document's map is an object, whose keys are text.
    if constexpr(!B::caps.self_describing) {
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
    // Decoded over a typical value, so a field the decode did not write
    // shows; `present` holds nulls, which are absent where they do not
    // travel.
    roundtrip_over(
        kit,
        "everything_default_roundtrip",
        [] { return Everything{}; },
        [] {
            auto start = Everything::typical();
            if constexpr(!B::caps.nested_nulls) {
                start.present = {};
            }
            return start;
        });
    if constexpr(B::caps.untrusted_input) {
        hostile(kit, "hostile_everything", [] { return Everything::typical(); });
    }

    if constexpr(B::caps.self_describing) {
        // The leaf error is the backend's to word; its path is the protocol's.
        // A value that does not read leaves the target as it was.
        read_in_field_fails_over(
            kit,
            "int8_out_of_range_fails",
            [] { return 300; },
            [] { return std::int8_t{5}; },
            {.message = "", .path = "value"});
        read_in_field_fails_over(
            kit,
            "enum_out_of_range_fails",
            [] { return 300; },
            [] { return UInt8Enum::c; },
            {.message = "", .path = "value"});
        read_in_field_fails_over(
            kit,
            "unsigned_enum_from_negative_fails",
            [] { return -1; },
            [] { return UInt8Enum::c; },
            {.message = "", .path = "value"});
        read_in_field_fails_over(
            kit,
            "enum_from_text_fails",
            [] { return std::string("neg"); },
            [] { return SignedEnum::neg; },
            {.message = "", .path = "value"});
        read_in_field_fails<std::nullptr_t>(kit,
                                            "null_from_non_null_fails",
                                            [] { return 0; },
                                            {.message = "", .path = "value"});
        read_in_field_fails<Point>(kit,
                                   "struct_from_array_fails",
                                   [] { return std::vector<int>{1, 2}; },
                                   {.message = "", .path = "value"});
        read_in_field_fails<std::vector<int>>(kit,
                                              "sequence_from_object_fails",
                                              [] { return Point{.x = 1, .y = 2}; },
                                              {.message = "", .path = "value"});
        read_in_field_fails<std::map<std::string, int>>(kit,
                                                        "map_from_array_fails",
                                                        [] { return std::vector<int>{1, 2}; },
                                                        {.message = "", .path = "value"});
        read_in_field_fails<std::optional<int>>(kit,
                                                "optional_payload_mismatch_fails",
                                                [] { return std::string("x"); },
                                                {.message = "", .path = "value"});
    }
}

}  // namespace kota::test
