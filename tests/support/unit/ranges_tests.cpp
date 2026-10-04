#include <deque>
#include <forward_list>
#include <map>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/support/ranges.h"

namespace kota {

namespace {

/// A map whose only insertion takes the key and the value apart, a form
/// insert_map_entry does not know.
struct PairwiseMap {
    using key_type = std::string;
    using mapped_type = int;

    void insert(std::string key, int value);
};

/// A sequence that appends only through push_back.
struct PushOnly {
    std::vector<int> values;

    void push_back(int value) {
        values.push_back(value);
    }
};

/// A sequence that appends only by inserting before an end position.
struct InsertAtEnd {
    std::vector<int> values;

    auto end() {
        return values.end();
    }

    void insert(std::vector<int>::iterator position, int value) {
        values.insert(position, value);
    }
};

/// A map that adds entries only by inserting its value_type.
struct InsertOnlyMap {
    using value_type = std::pair<const std::string, int>;
    std::map<std::string, int> entries;

    void insert(value_type entry) {
        entries.insert(std::move(entry));
    }
};

/// A range whose elements are itself, as std::filesystem::path's are.
struct SelfRange {
    const SelfRange* begin() const {
        return this;
    }

    const SelfRange* end() const {
        return this;
    }
};

/// Pair-like, with `.first` and `.second`, without the tuple protocol.
struct Entry {
    std::string first;
    double second;
};

ZEST_SUITE(support_ranges) {

ZEST_CASE(ranges_are_classified_by_their_shape) {
    ZSTATIC_EXPECT(format_kind<std::vector<int>> == range_format::sequence);
    ZSTATIC_EXPECT(format_kind<std::set<int>> == range_format::set);
    ZSTATIC_EXPECT(format_kind<std::map<int, char>> == range_format::map);
    // A range of itself, such as a path of paths, would recurse forever.
    ZSTATIC_EXPECT(format_kind<SelfRange> == range_format::disabled);
}

ZEST_CASE(range_concepts_follow_the_classification) {
    ZSTATIC_EXPECT(sequence_range<std::vector<int>>);
    ZSTATIC_EXPECT(sequence_range<const std::deque<int>&>);
    ZSTATIC_EXPECT(!sequence_range<int>);
    ZSTATIC_EXPECT(ordered_set_range<std::set<int>>);
    ZSTATIC_EXPECT(unordered_set_range<std::unordered_set<int>>);
    ZSTATIC_EXPECT(ordered_map_range<std::map<int, int>>);
    ZSTATIC_EXPECT(unordered_map_range<std::unordered_map<int, int>>);
    ZSTATIC_EXPECT(!ordered_map_range<std::unordered_map<int, int>>);
    ZSTATIC_EXPECT(!map_range<std::set<int>>);
}

ZEST_CASE(map_entries_by_tuple_protocol_or_members) {
    ZSTATIC_EXPECT(is_map_value_v<std::pair<const int, char>>);
    ZSTATIC_EXPECT(is_map_value_v<std::tuple<int, char>&>);
    ZSTATIC_EXPECT(is_map_value_v<Entry>);
    ZSTATIC_EXPECT(!is_map_value_v<std::tuple<int>>);
    ZSTATIC_EXPECT(!is_map_value_v<int>);
    ZEXPECT(zest::type_eq<map_entry_key_t<const std::pair<const int, char>&>, int>());
    ZEXPECT(zest::type_eq<map_entry_mapped_t<std::pair<const int, char>>, char>());
    ZEXPECT(zest::type_eq<map_entry_key_t<Entry>, std::string>());
    ZEXPECT(zest::type_eq<map_entry_mapped_t<Entry>, double>());
}

ZEST_CASE(sequences_insertable_by_a_known_form) {
    ZSTATIC_EXPECT(detail::sequence_insertable<std::vector<int>, int>);
    ZSTATIC_EXPECT(detail::sequence_insertable<std::set<int>, int>);
    ZSTATIC_EXPECT(detail::sequence_insertable<std::string, char>);
    ZSTATIC_EXPECT(detail::sequence_insertable<PushOnly, int>);
    ZSTATIC_EXPECT(detail::sequence_insertable<InsertAtEnd, int>);
}

ZEST_CASE(sequences_without_an_insertion_are_not) {
    ZSTATIC_EXPECT(!detail::sequence_insertable<std::span<int>, int>);
    ZSTATIC_EXPECT(!detail::sequence_insertable<std::forward_list<int>, int>);
}

// meta compares containers by the concepts of ranges.h, which this suite checks: the
// comparisons below are plain `==`, their operands left undecomposed.

ZEST_CASE(append_sequence_element_uses_the_form_available) {
    std::vector<std::string> emplaced;
    detail::append_sequence_element(emplaced, "a");
    ZEXPECT((emplaced == std::vector<std::string>{"a"}));

    PushOnly pushed;
    detail::append_sequence_element(pushed, 1);
    detail::append_sequence_element(pushed, 2);
    ZEXPECT((pushed.values == std::vector{1, 2}));

    InsertAtEnd inserted;
    detail::append_sequence_element(inserted, 3);
    detail::append_sequence_element(inserted, 4);
    ZEXPECT((inserted.values == std::vector{3, 4}));

    std::set<int> set;
    detail::append_sequence_element(set, 5);
    detail::append_sequence_element(set, 5);
    ZEXPECT((set == std::set{5}));
}

ZEST_CASE(maps_insertable_by_a_known_form) {
    ZSTATIC_EXPECT(detail::map_insertable<std::map<std::string, int>, std::string, int>);
    ZSTATIC_EXPECT(detail::map_insertable<std::unordered_map<int, int>, int, int>);
    ZSTATIC_EXPECT(detail::map_insertable<std::multimap<int, int>, int, int>);
    ZSTATIC_EXPECT(detail::map_insertable<InsertOnlyMap, std::string, int>);
    ZSTATIC_EXPECT(!detail::map_insertable<PairwiseMap, std::string, int>);
}

ZEST_CASE(insert_map_entry_uses_the_form_available) {
    std::map<std::string, int> map = {
        {"a", 1}
    };
    detail::insert_map_entry(map, std::string("a"), 2);
    ZEXPECT((map == std::map<std::string, int>{
                        {"a", 2}
    }));

    std::multimap<int, int> multimap;
    detail::insert_map_entry(multimap, 1, 1);
    detail::insert_map_entry(multimap, 1, 2);
    ZEXPECT(multimap.count(1) == 2U);

    InsertOnlyMap inserted;
    detail::insert_map_entry(inserted, std::string("b"), 3);
    ZEXPECT((inserted.entries == std::map<std::string, int>{
                                     {"b", 3}
    }));
}

};  // ZEST_SUITE(support_ranges)

}  // namespace

}  // namespace kota
