#include <cstddef>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/support/comptime.h"

namespace kota::comptime {

namespace {

/// Fills two pools and a byte buffer the same way in both passes: counting, which measures
/// them, and sized, which holds them in the object with the record the counting produced.
template <auto record = counting_flag<2>>
struct Build {
    using Resource = ComptimeMemoryResource<record>;

    Resource resource{};
    ComptimeVector<int, Resource, 0> numbers;
    ComptimeVector<char, Resource, 1> letters;
    const char* text = nullptr;

    constexpr Build() : numbers(resource), letters(resource) {
        numbers.reserve(2);
        for(int i = 0; i < 5; ++i) {
            numbers.push_back(i);
        }
        // The pool held 5 at its peak, and ends with 3.
        numbers.pop_back();
        numbers.pop_back();
        letters.emplace_back('x');
        letters.clear();
        letters.push_back('y');
        char* bytes = resource.template allocate_type<char>(4);
        if constexpr(Resource::is_counting) {
            resource.template deallocate_type<char>(bytes, 4);
        } else {
            for(std::size_t i = 0; i < 3; ++i) {
                bytes[i] = "abc"[i];
            }
            bytes[3] = '\0';
            text = bytes;
        }
    }
};

consteval auto count_build() {
    const Build<> counter;
    return counter.resource.gen_record();
}

constexpr auto record = count_build();

constexpr Build<record> sized;

ZEST_SUITE(support_comptime) {

ZEST_CASE(counting_records_the_peak_of_each_pool) {
    STATIC_EXPECT(!record.counting);
    STATIC_EXPECT(record.data[0] == 5U);
    STATIC_EXPECT(record.data[1] == 1U);
    STATIC_EXPECT(record.count >= 4U);
    STATIC_EXPECT(Build<record>::Resource::read_reserved<0>() == 5U);
    STATIC_EXPECT(Build<record>::Resource::read_reserved(1) == 1U);
}

ZEST_CASE(sized_pools_hold_what_the_counting_saw) {
    STATIC_EXPECT(sized.numbers.size() == 3U);
    STATIC_EXPECT(sized.numbers.capacity() == 5U);
    STATIC_EXPECT(sized.numbers.front() == 0);
    STATIC_EXPECT(sized.numbers[1] == 1);
    STATIC_EXPECT(sized.numbers.back() == 2);
    STATIC_EXPECT(!sized.numbers.empty());
    STATIC_EXPECT(sized.letters.size() == 1U);
    STATIC_EXPECT(sized.letters.front() == 'y');
}

ZEST_CASE(sized_buffer_holds_the_bytes) {
    STATIC_EXPECT((std::string_view(sized.text) == "abc"));
    STATIC_EXPECT(sized.resource.used_size() >= 4U);
}

ZEST_CASE(pools_iterate_and_compare) {
    int sum = 0;
    for(int value: sized.numbers) {
        sum += value;
    }
    EXPECT(sum == 3);
    EXPECT(sized.numbers.end() - sized.numbers.begin() == 3);
    EXPECT(sized.numbers.cend() - sized.numbers.cbegin() == 3);
    EXPECT(sized.numbers.data() == sized.numbers.begin());
    // The pool's own comparison, not the checks'.
    EXPECT((sized.numbers == sized.numbers));
}

};  // ZEST_SUITE(support_comptime)

}  // namespace

}  // namespace kota::comptime
