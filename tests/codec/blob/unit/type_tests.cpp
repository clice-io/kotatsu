#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "codec/blob/harness/fixtures.h"
#include "kota/zest/zest.h"
#include "kota/codec/blob/blob.h"

namespace kota::codec::blob {

namespace {

ZEST_SUITE(codec_blob_type) {

ZEST_CASE(view_t_maps_owned_forms) {
    ZEXPECT(zest::type_eq<view_t<test::Sample>, test::SampleOf<Vec>>());
    ZEXPECT(zest::type_eq<view_t<std::string>, Vec<char>>());
    ZEXPECT(zest::type_eq<view_t<std::vector<std::string>>, Vec<Vec<char>>>());
    ZEXPECT(zest::type_eq<view_t<std::vector<test::Leaf>>, Vec<test::LeafOf<Vec>>>());
    // A struct of scalars only, and a scalar, are their own view.
    ZEXPECT(zest::type_eq<view_t<test::PaddedPoint>, test::PaddedPoint>());
    ZEXPECT(zest::type_eq<view_t<std::uint32_t>, std::uint32_t>());
}

ZEST_CASE(owned_picks_string_for_characters) {
    ZEXPECT(zest::type_eq<Owned<char>, std::string>());
    ZEXPECT(zest::type_eq<Owned<std::uint8_t>, std::vector<std::uint8_t>>());
}

ZEST_CASE(vec_is_two_words_read_in_place) {
    // Its offset counts from its own address, so a copy would point elsewhere.
    ZSTATIC_EXPECT(!std::is_copy_constructible_v<Vec<std::uint32_t>>);
    ZSTATIC_EXPECT(!std::is_copy_assignable_v<Vec<std::uint32_t>>);
    // An implicit-lifetime type, which bytes hold without a constructor run.
    ZSTATIC_EXPECT(std::is_trivially_default_constructible_v<Vec<std::uint32_t>>);
    ZSTATIC_EXPECT(std::is_trivially_destructible_v<Vec<std::uint32_t>>);
    ZSTATIC_EXPECT(sizeof(Vec<std::uint64_t>) == 8);
    ZSTATIC_EXPECT(sizeof(Vec<char>) == 8);
}

ZEST_CASE(empty_vec_has_null_data) {
    auto bytes = test::blob_of(test::empty_probe());
    auto viewed = view<test::Probe>(bytes);
    ZASSERT(viewed);
    // Pointers compare as themselves.
    ZEXPECT(((*viewed)->numbers.data() == nullptr));
    ZEXPECT(((*viewed)->text.data() == nullptr));
    ZEXPECT(((*viewed)->numbers.begin() == (*viewed)->numbers.end()));
    ZEXPECT(std::string_view((*viewed)->text).empty());
}

};  // ZEST_SUITE(codec_blob_type)

}  // namespace

}  // namespace kota::codec::blob
