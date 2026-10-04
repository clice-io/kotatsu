#include <compare>
#include <cstdint>
#include <limits>

#include "kota/zest/zest.h"
#include "kota/support/numeric.h"

namespace kota {

namespace {

/// narrow_int(value) into a fresh Target, or `untouched` when it does not fit.
template <typename Target, typename Source>
constexpr Target narrowed(Source value, Target untouched = 42) {
    Target out = untouched;
    narrow_int(value, out);
    return out;
}

ZEST_SUITE(support_numeric) {

ZEST_CASE(same_signedness_checks_both_bounds) {
    std::int8_t out = 0;
    ZEXPECT(narrow_int(std::int64_t{127}, out));
    ZEXPECT(out == 127);
    ZEXPECT(narrow_int(std::int64_t{-128}, out));
    ZEXPECT(out == -128);
    ZEXPECT(!narrow_int(std::int64_t{128}, out));
    ZEXPECT(!narrow_int(std::int64_t{-129}, out));
    ZEXPECT(out == -128);

    std::uint16_t small = 0;
    ZEXPECT(narrow_int(std::uint64_t{65535}, small));
    ZEXPECT(!narrow_int(std::uint64_t{65536}, small));
}

ZEST_CASE(signed_into_unsigned_rejects_negatives) {
    std::uint8_t out = 0;
    ZEXPECT(narrow_int(std::int32_t{255}, out));
    ZEXPECT(out == 255);
    ZEXPECT(!narrow_int(std::int32_t{-1}, out));
    ZEXPECT(!narrow_int(std::int32_t{256}, out));
    std::uint32_t same_size = 0;
    ZEXPECT(narrow_int(std::numeric_limits<std::int32_t>::max(), same_size));
    ZEXPECT(!narrow_int(std::numeric_limits<std::int32_t>::min(), same_size));
}

ZEST_CASE(unsigned_into_signed_rejects_what_exceeds_the_maximum) {
    std::int8_t out = 0;
    ZEXPECT(narrow_int(std::uint32_t{127}, out));
    ZEXPECT(!narrow_int(std::uint32_t{128}, out));
    std::int64_t same_size = 0;
    ZEXPECT(narrow_int(std::uint64_t{0x7FFF'FFFF'FFFF'FFFF}, same_size));
    ZEXPECT(!narrow_int(std::numeric_limits<std::uint64_t>::max(), same_size));
}

ZEST_CASE(into_bool_takes_only_zero_and_one) {
    bool out = false;
    ZEXPECT(narrow_int(1, out));
    ZEXPECT(out);
    ZEXPECT(narrow_int(0, out));
    ZEXPECT(!out);
    ZEXPECT(!narrow_int(2, out));
    ZEXPECT(!narrow_int(-1, out));
}

ZEST_CASE(leaves_the_target_alone_when_it_does_not_fit) {
    ZEXPECT(narrowed<std::uint8_t>(300) == 42);
    ZEXPECT(narrowed<std::int16_t>(std::int64_t{1} << 40) == 42);
    ZEXPECT(narrowed<std::uint8_t>(7) == 7);
}

ZEST_CASE(works_in_constant_evaluation) {
    ZSTATIC_EXPECT(narrowed<std::uint8_t>(200) == 200);
    ZSTATIC_EXPECT(narrowed<std::uint8_t>(-5) == 42);
}

ZEST_CASE(compare_exact_takes_the_whole_integer) {
    using std::partial_ordering;
    // 2^53 + 1 is no double: its nearest, 2^53, is below it.
    constexpr std::int64_t odd = (std::int64_t{1} << 53) + 1;
    ZSTATIC_EXPECT(compare_exact(odd, 9007199254740992.0) == partial_ordering::greater);
    ZSTATIC_EXPECT(compare_exact(std::int64_t{3}, 3.0) == partial_ordering::equivalent);
    // A fraction decides between the integers either side of it.
    ZSTATIC_EXPECT(compare_exact(2, 2.5) == partial_ordering::less);
    ZSTATIC_EXPECT(compare_exact(-2, -2.5) == partial_ordering::greater);
    ZSTATIC_EXPECT(compare_exact(-3, -2.5) == partial_ordering::less);
}

ZEST_CASE(compare_exact_past_every_integer) {
    using std::partial_ordering;
    ZSTATIC_EXPECT(compare_exact(std::numeric_limits<std::uint64_t>::max(),
                                 18446744073709551616.0) == partial_ordering::less);
    ZSTATIC_EXPECT(compare_exact(std::uint64_t{1}, 1e30) == partial_ordering::less);
    ZSTATIC_EXPECT(compare_exact(std::numeric_limits<std::int64_t>::min(), -1e30) ==
                   partial_ordering::greater);
    ZSTATIC_EXPECT(compare_exact(std::numeric_limits<std::int64_t>::min(),
                                 -9223372036854775808.0) == partial_ordering::equivalent);
    ZSTATIC_EXPECT(compare_exact(0, std::numeric_limits<double>::infinity()) ==
                   partial_ordering::less);
    ZSTATIC_EXPECT(compare_exact(0, std::numeric_limits<double>::quiet_NaN()) ==
                   partial_ordering::unordered);
}

};  // ZEST_SUITE(support_numeric)

}  // namespace

}  // namespace kota
