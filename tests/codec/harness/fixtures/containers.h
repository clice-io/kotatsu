#pragma once

// Container fixtures only the codec's tests use: a field of every container
// and nullable kind, in composites the kit roundtrips.

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
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

#include "codec/harness/fixtures/structs.h"
#include "fixtures/containers.h"
#include "fixtures/enums.h"
#include "kota/meta/compare.h"

namespace kota::test {

/// A variant with a smart-pointer alternative, compared by pointee. It
/// starts on the other alternative, so a null that did not reach the pointer
/// shows.
template <typename Pointer, typename Other>
struct PointerOr {
    std::variant<Pointer, Other> value = Other{};

    bool operator==(const PointerOr& other) const {
        if(value.index() != other.value.index()) {
            return false;
        }
        if(value.index() == 1) {
            return meta::eq(std::get<1>(value), std::get<1>(other.value));
        }
        return same_pointee(std::get<0>(value), std::get<0>(other.value));
    }
};

/// Every nullable kind, and one nullable in another, engaged or empty.
struct Nullables {
    std::optional<int> number;
    std::optional<std::string> text;
    std::optional<std::vector<int>> list;
    std::optional<std::map<std::string, int>> table;
    std::unique_ptr<Point> owned;
    std::shared_ptr<std::string> shared;
    std::optional<std::shared_ptr<Point>> nested;

    static Nullables engaged() {
        Point point{.x = 1, .y = -2};
        return {
            .number = 17,
            .text = "text",
            .list = std::vector<int>{4, 5, 6},
            .table = std::map<std::string, int>{{"a", 1}},
            .owned = std::make_unique<Point>(point),
            .shared = std::make_shared<std::string>("shared"),
            .nested = std::make_shared<Point>(Point{.x = 3, .y = 4}
              ),
        };
    }

    bool operator==(const Nullables& other) const {
        return number == other.number && text == other.text && list == other.list &&
               table == other.table && same_pointee(owned, other.owned) &&
               same_pointee(shared, other.shared) &&
               nested.has_value() == other.nested.has_value() &&
               (!nested || same_pointee(*nested, *other.nested));
    }
};

struct Sequences {
    std::vector<int> numbers;
    std::array<int, 3> triple;
    std::deque<std::string> words;
    std::vector<bool> flags;
    std::vector<std::vector<int>> rows;
    std::vector<Point> points;
    std::vector<std::map<std::string, int>> tables;

    static Sequences typical() {
        return {
            .numbers = {1, 2, 3, 5},
            .triple = {4, 5, 6},
            .words = {"alpha", "beta", "gamma"},
            .flags = {true, false, true},
            .rows = {{1, 2, 3}, {}, {6}},
            .points = {{.x = 1, .y = 2}, {.x = -3, .y = 4}},
            .tables = {{{"a", 1}, {"b", 2}}, {}},
        };
    }
};

struct Sets {
    std::set<int> numbers;
    std::set<std::string> words;
    /// One element: after a decode, several would come out in an unspecified
    /// order, and the document would not be stable. That every element of a
    /// larger one is read is a case of its own.
    std::unordered_set<int> hashed;

    static Sets typical() {
        return {
            .numbers = {1, 3, 5, 7},
            .words = {"alpha", "beta", "gamma"},
            .hashed = {42},
        };
    }
};

struct Maps {
    std::map<std::string, int> by_name;
    std::map<int, std::string> by_id;
    /// A key beyond int64's maximum travels as its own decimal form.
    std::map<std::uint64_t, int> by_wide_id;
    std::map<Color, int> by_color;
    std::map<std::string, Point> places;
    std::map<std::string, std::vector<int>> lists;
    std::map<std::string, std::map<std::string, int>> nested;
    /// One entry, for the reason Sets::hashed has one element.
    std::unordered_map<std::string, int> hashed;

    static Maps typical() {
        return {
            .by_name = {{"a", 1}, {"b", 2}},
            .by_id = {{-2, "minus two"}, {0, "zero"}, {7, "seven"}},
            .by_wide_id = {{1, 1}, {9223372036854775809ULL, 2}},
            .by_color = {{Color::red, 1}, {Color::blue, 3}},
            .places = {{"home", {.x = 1, .y = 2}}},
            .lists = {{"a", {10, 20}}, {"b", {}}},
            .nested = {{"outer", {{"inner", 1}}}, {"none", {}}},
            .hashed = {{"only", 1}},
        };
    }
};

struct Tuples {
    std::tuple<> none;
    std::tuple<int> single;
    std::tuple<int, bool, std::string> mixed;
    std::pair<std::uint64_t, double> pair;
    std::tuple<Point, std::array<int, 2>> nested;

    static Tuples typical() {
        return {
            .none = {},
            .single = {7},
            .mixed = {7, true, "tuple"},
            .pair = {42, -2.5},
            .nested = {Point{.x = 1, .y = 2}, {3, 4}},
        };
    }
};

/// Nulls where a sequence element or a map value stands.
struct NullElements {
    std::vector<std::optional<int>> optionals;
    std::map<std::string, std::optional<int>> by_name;
    std::vector<std::shared_ptr<Point>> pointers;

    /// One object is pointed at twice: it travels as two copies.
    static NullElements typical() {
        Point point{.x = 1, .y = 2};
        auto shared = std::make_shared<Point>(point);
        return {
            .optionals = {1, std::nullopt, 3},
            .by_name = {{"a", 1}, {"b", std::nullopt}},
            .pointers = {shared, nullptr, shared},
        };
    }

    bool operator==(const NullElements& other) const {
        return optionals == other.optionals && by_name == other.by_name &&
               std::ranges::equal(pointers, other.pointers, [](const auto& lhs, const auto& rhs) {
                   return same_pointee(lhs, rhs);
               });
    }
};

/// HoldsExplicit where decoding makes a value of its own: a sequence element
/// and a map value.
struct ExplicitElements {
    std::vector<HoldsExplicit> items;
    std::map<std::string, HoldsExplicit> by_name;

    static ExplicitElements typical() {
        return {
            .items = {{.list = {1, 2}, .count = 2}, {.list = ExplicitList(), .count = 0}},
            .by_name = {{"a", {.list = {3}, .count = 1}}},
        };
    }
};

}  // namespace kota::test
