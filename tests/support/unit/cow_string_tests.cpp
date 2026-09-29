#include <string>
#include <string_view>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/support/cow_string.h"

namespace kota {

namespace {

constexpr bool borrow_own_and_release() {
    cow_string borrowed("hello");
    cow_string owned = cow_string::owned(string_ref("world"));
    cow_string copy(owned);
    copy.make_owned();
    cow_string moved(std::move(owned));
    small_string<0> released = moved.release();
    return borrowed.is_borrowed() && copy.is_owned() && copy.ref() == "world" &&
           released.ref() == "world" && moved.empty() && owned.empty();
}

ZEST_SUITE(support_cow_string) {

ZEST_CASE(works_in_constant_evaluation) {
    STATIC_EXPECT(borrow_own_and_release());
}

ZEST_CASE(default_is_empty_and_borrowed) {
    cow_string s;
    EXPECT(s.empty());
    EXPECT(s.size() == 0U);
    EXPECT(s.is_borrowed());
    EXPECT(!s.is_owned());
    EXPECT(s.data() == nullptr);
}

ZEST_CASE(borrowed_views_the_text) {
    const char* text = "hello world";
    cow_string implicit{string_ref(text)};
    cow_string named = cow_string::borrowed(text);
    EXPECT(implicit.is_borrowed());
    EXPECT(implicit.data() == text);
    EXPECT(named.data() == text);
    EXPECT(implicit.ref() == "hello world");
}

ZEST_CASE(owned_copies_the_text) {
    const std::string text = "copy me";
    cow_string s = cow_string::owned(text);
    EXPECT(s.is_owned());
    EXPECT(s.data() != text.data());
    EXPECT(s.ref() == "copy me");
}

ZEST_CASE(owned_empty_text_stays_borrowed) {
    cow_string s = cow_string::owned(string_ref(""));
    EXPECT(s.empty());
    EXPECT(s.is_borrowed());
}

ZEST_CASE(copy_of_borrowed_borrows) {
    const char* text = "shared";
    cow_string a(text);
    cow_string b(a);
    EXPECT(b.is_borrowed());
    EXPECT(b.data() == text);
}

ZEST_CASE(copy_of_owned_owns_its_own) {
    cow_string a = cow_string::owned(string_ref("deep"));
    cow_string b(a);
    EXPECT(b.is_owned());
    EXPECT(b.data() != a.data());
    EXPECT(b.ref() == "deep");
}

ZEST_CASE(move_hands_over_and_empties) {
    cow_string a = cow_string::owned(string_ref("move me"));
    const auto* data = a.data();
    cow_string b(std::move(a));
    EXPECT(b.is_owned());
    EXPECT(b.data() == data);
    EXPECT(a.empty());
    EXPECT(a.data() == nullptr);
    EXPECT(a.is_borrowed());
}

ZEST_CASE(copy_assignment_owns_its_own) {
    cow_string a = cow_string::owned(string_ref("original"));
    cow_string b("other");
    b = a;
    EXPECT(b.is_owned());
    EXPECT(b.data() != a.data());
    EXPECT(b.ref() == "original");

    const auto* data = b.data();
    const auto& same = b;
    b = same;
    EXPECT(b.data() == data);
}

ZEST_CASE(move_assignment_frees_the_old_text) {
    cow_string a = cow_string::owned(string_ref("transfer"));
    cow_string b = cow_string::owned(string_ref("replaced"));
    const auto* data = a.data();
    b = std::move(a);
    EXPECT(b.data() == data);
    EXPECT(b.ref() == "transfer");
    EXPECT(a.empty());

    auto& same = b;
    b = std::move(same);
    EXPECT(b.ref() == "transfer");
}

ZEST_CASE(make_owned_copies_borrowed_text_once) {
    const char* text = "convert";
    cow_string s(text);
    s.make_owned();
    EXPECT(s.is_owned());
    EXPECT(s.data() != text);
    const auto* data = s.data();
    s.make_owned();
    EXPECT(s.data() == data);
    EXPECT(s.ref() == "convert");
}

ZEST_CASE(make_owned_of_empty_stays_borrowed) {
    cow_string s;
    s.make_owned();
    EXPECT(s.is_borrowed());
}

ZEST_CASE(release_of_owned_hands_over_the_buffer) {
    cow_string s = cow_string::owned(string_ref("release me"));
    const auto* data = s.data();
    small_string<4> released = s.release<4>();
    EXPECT(released.ref() == "release me");
    EXPECT(released.data() == data);
    EXPECT(s.empty());
    EXPECT(s.is_borrowed());
}

ZEST_CASE(release_of_borrowed_copies_into_the_inline_buffer) {
    cow_string s("tiny");
    small_string<8> released = s.release<8>();
    EXPECT(released.ref() == "tiny");
    EXPECT(released.inlined());
    EXPECT(s.empty());
}

ZEST_CASE(release_of_borrowed_text_larger_than_the_buffer_allocates) {
    cow_string s("larger than two");
    small_string<2> released = s.release<2>();
    EXPECT(released.ref() == "larger than two");
    EXPECT(!released.inlined());
}

ZEST_CASE(converts_to_views_and_strings) {
    cow_string s("interop");
    string_ref ref = s;
    std::string_view view = s;
    EXPECT(ref == "interop");
    EXPECT(view == "interop");
    EXPECT(s.to_string() == "interop");
}

ZEST_CASE(compares_by_text) {
    cow_string borrowed("hello");
    cow_string owned = cow_string::owned(string_ref("hello"));
    // The string's own operators, not the checks' comparison.
    EXPECT((borrowed == owned));
    EXPECT(!(borrowed == cow_string("world")));
    EXPECT((borrowed == "hello"));
    EXPECT((borrowed == string_ref("hello")));
}

ZEST_CASE(swap_exchanges_modes) {
    cow_string a("aaa");
    cow_string b = cow_string::owned(string_ref("bbb"));
    a.swap(b);
    EXPECT(a.ref() == "bbb");
    EXPECT(a.is_owned());
    EXPECT(b.ref() == "aaa");
    EXPECT(b.is_borrowed());
}

};  // ZEST_SUITE(support_cow_string)

}  // namespace

}  // namespace kota
