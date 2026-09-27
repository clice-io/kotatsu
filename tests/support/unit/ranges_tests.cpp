#include <forward_list>
#include <map>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
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

ZEST_SUITE(support_ranges) {

ZEST_CASE(sequences_insertable_by_a_known_form) {
    STATIC_EXPECT(detail::sequence_insertable<std::vector<int>, int>);
    STATIC_EXPECT(detail::sequence_insertable<std::set<int>, int>);
    STATIC_EXPECT(detail::sequence_insertable<std::string, char>);
}

ZEST_CASE(sequences_without_an_insertion_are_not) {
    STATIC_EXPECT(!detail::sequence_insertable<std::span<int>, int>);
    STATIC_EXPECT(!detail::sequence_insertable<std::forward_list<int>, int>);
}

ZEST_CASE(maps_insertable_by_a_known_form) {
    STATIC_EXPECT(detail::map_insertable<std::map<std::string, int>, std::string, int>);
    STATIC_EXPECT(detail::map_insertable<std::unordered_map<int, int>, int, int>);
    STATIC_EXPECT(!detail::map_insertable<PairwiseMap, std::string, int>);
}

};  // ZEST_SUITE(support_ranges)

}  // namespace

}  // namespace kota
