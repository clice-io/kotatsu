#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/support/buffer_ref.h"
#include "kota/support/small_string.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

using namespace std::literals;

/// Writes `text` into `out`, as a function that takes a buffer does.
void put(buffer_ref out, std::string_view text) {
    out.assign(text);
}

ZEST_SUITE(support_buffer_ref) {

ZEST_CASE(contiguous_resizable_buffers_qualify) {
    ZSTATIC_EXPECT(string_buffer<std::string>);
    ZSTATIC_EXPECT(string_buffer<std::vector<char>>);
    ZSTATIC_EXPECT(string_buffer<small_string<16>>);
    ZSTATIC_EXPECT(string_buffer<small_vector<char, 8>>);
    ZSTATIC_EXPECT(string_buffer<std::u16string, char16_t>);
    ZSTATIC_EXPECT(!string_buffer<std::u16string>);
    ZSTATIC_EXPECT(!string_buffer<const std::string>);
    ZSTATIC_EXPECT(!string_buffer<std::string_view>);
    // A temporary, which the text would not outlive, is no buffer to write.
    ZSTATIC_EXPECT(std::is_convertible_v<std::string&, buffer_ref>);
    ZSTATIC_EXPECT(!std::is_convertible_v<std::string, buffer_ref>);
}

ZEST_CASE(writes_reach_a_buffer_of_any_type) {
    std::string string;
    small_string<16> text;
    small_vector<char, 8> vector;
    std::vector<char> std_vector;
    put(string, "text");
    put(text, "text");
    put(vector, "a text longer than eight");
    put(std_vector, "text");
    ZEXPECT(string == "text");
    ZEXPECT(std::string_view(text) == "text");
    ZEXPECT(std::string_view(vector.data(), vector.size()) == "a text longer than eight");
    ZEXPECT(std::string_view(std_vector.data(), std_vector.size()) == "text");
}

ZEST_CASE(view_reads_the_buffer_as_it_is_now) {
    std::string text = "abc";
    buffer_ref ref(text);
    ZEXPECT(ref.view() == "abc");
    ZEXPECT(ref.size() == 3u);
    ZEXPECT(!ref.empty());
    text += "de";
    ZEXPECT(ref.view() == "abcde");
}

ZEST_CASE(resize_keeps_the_first_characters) {
    small_string<4> text("ab");
    buffer_ref ref(text);
    auto chars = ref.resize(6);
    // Where the growth moved the characters.
    ZEXPECT((chars.data() == text.data()));
    ZEXPECT(chars.size() == 6u);
    std::ranges::copy("cdef"sv, chars.begin() + 2);
    ZEXPECT(std::string_view(text) == "abcdef");
    ref.chars()[0] = 'A';
    ref.resize(2);
    ZEXPECT(std::string_view(text) == "Ab");
}

ZEST_CASE(append_and_push_back_add_at_the_end) {
    std::vector<char> chars;
    buffer_ref ref(chars);
    ref.append("ab");
    ref.push_back('c');
    ref.append("");
    ZEXPECT(ref.view() == "abc");
    ref.clear();
    ZEXPECT(ref.empty());
    ZEXPECT(chars.empty());
}

ZEST_CASE(assign_replaces_the_whole_buffer) {
    small_vector<char, 8> chars;
    buffer_ref ref(chars);
    ref.assign("some old text");
    ref.assign("new");
    ZEXPECT(ref.view() == "new");
    ref.assign("");
    ZEXPECT(chars.empty());
}

ZEST_CASE(assign_takes_text_that_views_the_buffer) {
    std::string text = "abcdef";
    buffer_ref ref(text);
    ref.assign(ref.view().substr(2));
    ZEXPECT(text == "cdef");
    ref.assign(ref.view().substr(1, 2));
    ZEXPECT(text == "de");
    ref.assign(ref.view().substr(0, 1));
    ZEXPECT(text == "d");
}

ZEST_CASE(a_small_string_grows_past_its_inline_capacity) {
    small_string<16> text("0123456789");
    buffer_ref ref(text);
    ref.append("abcdefghij");
    ZEXPECT(!text.inlined());
    ZEXPECT(std::string_view(text) == "0123456789abcdefghij");
    ZEXPECT(ref.view() == "0123456789abcdefghij");
}

ZEST_CASE(buffers_of_other_characters_work_alike) {
    std::u16string wide;
    basic_buffer_ref<char16_t> ref(wide);
    ref.append(u"ab");
    ref.push_back(u'c');
    ZEXPECT(wide == u"abc");
}

};  // ZEST_SUITE(support_buffer_ref)

}  // namespace

}  // namespace kota
