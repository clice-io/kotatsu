#include <compare>
#include <cstring>
#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/support/small_string.h"

namespace kota {

namespace {

using namespace std::literals;

ZEST_SUITE(support_small_string) {

ZEST_CASE(constructs_from_text) {
    small_string<8> short_text("abc");
    EXPECT(std::string_view(short_text) == "abc");
    EXPECT(short_text.inlined());

    small_string<2> long_text("a longer text"sv);
    EXPECT(std::string_view(long_text) == "a longer text");
    EXPECT(!long_text.inlined());
}

ZEST_CASE(constructs_from_parts_joined) {
    small_string<4> joined{"ab", "", "cd", "ef"};
    EXPECT(std::string_view(joined) == "abcdef");
}

ZEST_CASE(default_is_empty) {
    small_string<4> empty;
    EXPECT(empty.empty());
    EXPECT(std::string_view(empty).empty());
}

ZEST_CASE(append_text_and_characters) {
    small_string<4> s("ab");
    s.append("cd"sv);
    s += "ef";
    s += 'g';
    EXPECT(std::string_view(s) == "abcdefg");
}

ZEST_CASE(append_parts_joined) {
    small_string<4> s("a");
    s.append({"bc", "d", "efgh"});
    EXPECT(std::string_view(s) == "abcdefgh");
}

ZEST_CASE(append_itself) {
    small_string<4> inline_text("abcd");
    inline_text += std::string_view(inline_text);
    EXPECT(std::string_view(inline_text) == "abcdabcd");

    small_string<4> heap_text("abcdefgh");
    heap_text += std::string_view(heap_text);
    EXPECT(std::string_view(heap_text) == "abcdefghabcdefgh");
}

ZEST_CASE(append_parts_of_itself) {
    small_string<4> s("abcdefgh");
    const std::string_view whole(s);
    s.append({whole, "!", whole.substr(0, 2)});
    EXPECT(std::string_view(s) == "abcdefghabcdefgh!ab");
}

ZEST_CASE(assign_replaces_the_text) {
    small_string<4> s("abc");
    s.assign("a much longer text"sv);
    EXPECT(std::string_view(s) == "a much longer text");
    s = "xy";
    EXPECT(std::string_view(s) == "xy");
    s.assign({"1", "23"});
    EXPECT(std::string_view(s) == "123");
}

ZEST_CASE(assign_part_of_itself) {
    small_string<16> s("abcdefgh");
    s.assign(std::string_view(s).substr(2));
    EXPECT(std::string_view(s) == "cdefgh");
    s = std::string_view(s).substr(0, 3);
    EXPECT(std::string_view(s) == "cde");
}

ZEST_CASE(assign_parts_of_itself) {
    small_string<4> s("abcdefgh");
    const std::string_view whole(s);
    s.assign({whole.substr(4), whole.substr(0, 4)});
    EXPECT(std::string_view(s) == "efghabcd");
}

ZEST_CASE(c_str_is_terminated) {
    small_string<4> s("abcd");
    const char* text = s.c_str();
    EXPECT(std::strlen(text) == 4U);
    EXPECT(std::string_view(text) == "abcd");
    EXPECT(s.size() == 4U);
}

ZEST_CASE(converts_to_views_and_strings) {
    small_string<4> s("text");
    string_ref ref = s;
    std::string_view view = s;
    EXPECT(ref == "text");
    EXPECT(view == "text");
    EXPECT(s.ref() == "text");
    EXPECT(static_cast<std::string>(s) == "text");
}

ZEST_CASE(compares_with_text) {
    small_string<4> s("abc");
    // The string's own operators, not the checks' comparison.
    EXPECT((s == "abc"sv));
    EXPECT(!(s == "abd"sv));
    EXPECT(((s <=> "abd"sv) == std::strong_ordering::less));
    EXPECT(((s <=> "ab"sv) == std::strong_ordering::greater));
    EXPECT(((s <=> "abc"sv) == std::strong_ordering::equal));
}

ZEST_CASE(from_raw_parts_adopts_the_buffer) {
    auto* buffer = mem::allocate<char>(8);
    std::memcpy(buffer, "raw", 3);
    auto s = small_string<2>::from_raw_parts(buffer, 3, 8);
    EXPECT(std::string_view(s) == "raw");
    EXPECT(s.data() == buffer);
    EXPECT(s.capacity() == 8U);
}

ZEST_CASE(from_raw_parts_of_nothing_is_empty) {
    auto s = small_string<2>::from_raw_parts(nullptr, 0, 0);
    EXPECT(s.empty());
    EXPECT(s.inlined());
}

};  // ZEST_SUITE(support_small_string)

}  // namespace

}  // namespace kota
