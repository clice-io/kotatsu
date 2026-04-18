#pragma once

// Container fixtures — compound type_kind shapes without attrs.

#include <array>
#include <cstddef>
#include <deque>
#include <iterator>
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

#include "fixtures/schema/primitives.h"
#include "kota/support/ranges.h"

namespace kota::meta::fixtures {

using VectorInt = std::vector<int>;
using StdArrayInt3 = std::array<int, 3>;
using DequeInt = std::deque<int>;

using SetInt = std::set<int>;
using UnorderedSetInt = std::unordered_set<int>;

using MapStringInt = std::map<std::string, int>;
using UnorderedMapStringInt = std::unordered_map<std::string, int>;
using MultimapStringInt = std::multimap<std::string, int>;

using OptionalInt = std::optional<int>;
using OptionalOptionalInt = std::optional<std::optional<int>>;
using UniquePtrInt = std::unique_ptr<int>;
using SharedPtrString = std::shared_ptr<std::string>;

using PairIntFloat = std::pair<int, float>;
using TupleInt = std::tuple<int>;
using TupleEmpty = std::tuple<>;
using TupleIntDoubleString = std::tuple<int, double, std::string>;

using VariantIntString = std::variant<int, std::string>;
using VariantSingle = std::variant<int>;

using VectorOfOptional = std::vector<std::optional<int>>;
using MapToVector = std::map<std::string, std::vector<int>>;
using SetOfVectorString = std::set<std::vector<std::string>>;
using UniquePtrVector = std::unique_ptr<std::vector<int>>;

struct NestedStruct {
    std::vector<SimpleStruct> items;
};

// An input_range whose reference type is itself — auto-detected as
// range_format::disabled so it never looks like array / set / map.
struct disabled_range;

struct disabled_range_iter {
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = disabled_range;
    using difference_type = std::ptrdiff_t;

    auto operator*() const -> disabled_range;

    auto operator++() -> disabled_range_iter& {
        return *this;
    }

    auto operator++(int) -> disabled_range_iter {
        return *this;
    }

    auto operator==(std::default_sentinel_t) const -> bool {
        return true;
    }
};

struct disabled_range {
    auto begin() const -> disabled_range_iter {
        return {};
    }

    auto end() const -> std::default_sentinel_t {
        return {};
    }
};

inline auto disabled_range_iter::operator*() const -> disabled_range {
    return {};
}

static_assert(std::ranges::input_range<disabled_range>);
static_assert(kota::format_kind<disabled_range> == kota::range_format::disabled);

}  // namespace kota::meta::fixtures
