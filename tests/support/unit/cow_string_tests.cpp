#include <string>
#include <string_view>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/support/cow_string.h"

namespace kota {

namespace {

/// What borrowing, owning, copying, moving and releasing leave in constant evaluation, one
/// fact at a time.
struct Facts {
    bool borrowed = false;
    bool made_owned = false;
    bool owned_text = false;
    bool named_owned = false;
    bool named_text = false;
    bool copy_owned = false;
    bool copy_has_its_own_text = false;
    bool copy_text = false;
    bool moved_from_empty = false;
    bool released_text = false;
    bool released_from_empty = false;
};

constexpr Facts borrow_own_and_release() {
    Facts facts;
    cow_string made("world");
    facts.borrowed = made.is_borrowed();
    made.make_owned();
    facts.made_owned = made.is_owned();
    facts.owned_text = made.ref() == "world";
    const cow_string named = cow_string::owned(string_ref("named"));
    facts.named_owned = named.is_owned();
    facts.named_text = named.ref() == "named";
    cow_string copy(made);
    facts.copy_owned = copy.is_owned();
    facts.copy_has_its_own_text = copy.data() != made.data();
    facts.copy_text = copy.ref() == "world";
    cow_string moved(std::move(made));
    facts.moved_from_empty = made.empty();
    small_string<0> released = moved.release();
    facts.released_text = released.ref() == "world";
    facts.released_from_empty = moved.empty();
    return facts;
}

ZEST_SUITE(support_cow_string) {

ZEST_CASE(works_in_constant_evaluation) {
    constexpr static auto facts = borrow_own_and_release();
    ZSTATIC_EXPECT(facts.borrowed);
    ZSTATIC_EXPECT(facts.made_owned);
    ZSTATIC_EXPECT(facts.owned_text);
    ZSTATIC_EXPECT(facts.named_owned);
    ZSTATIC_EXPECT(facts.named_text);
    ZSTATIC_EXPECT(facts.copy_owned);
    ZSTATIC_EXPECT(facts.copy_has_its_own_text);
    ZSTATIC_EXPECT(facts.copy_text);
    ZSTATIC_EXPECT(facts.moved_from_empty);
    ZSTATIC_EXPECT(facts.released_text);
    ZSTATIC_EXPECT(facts.released_from_empty);
}

ZEST_CASE(default_is_empty_and_borrowed) {
    cow_string s;
    ZEXPECT(s.empty());
    ZEXPECT(s.size() == 0U);
    ZEXPECT(s.is_borrowed());
    ZEXPECT(!s.is_owned());
    ZEXPECT(s.data() == nullptr);
}

ZEST_CASE(borrowed_views_the_text) {
    const char* text = "hello world";
    cow_string implicit{string_ref(text)};
    cow_string named = cow_string::borrowed(text);
    ZEXPECT(implicit.is_borrowed());
    ZEXPECT(implicit.data() == text);
    ZEXPECT(named.data() == text);
    ZEXPECT(implicit.ref() == "hello world");
}

ZEST_CASE(owned_copies_the_text) {
    const std::string text = "copy me";
    cow_string s = cow_string::owned(text);
    ZEXPECT(s.is_owned());
    ZEXPECT(s.data() != text.data());
    ZEXPECT(s.ref() == "copy me");
}

ZEST_CASE(owned_empty_text_stays_borrowed) {
    cow_string s = cow_string::owned(string_ref(""));
    ZEXPECT(s.empty());
    ZEXPECT(s.is_borrowed());
}

ZEST_CASE(copy_of_borrowed_borrows) {
    const char* text = "shared";
    cow_string a(text);
    cow_string b(a);
    ZEXPECT(b.is_borrowed());
    ZEXPECT(b.data() == text);
}

ZEST_CASE(copy_of_owned_owns_its_own) {
    cow_string a = cow_string::owned(string_ref("deep"));
    cow_string b(a);
    ZEXPECT(b.is_owned());
    ZEXPECT(b.data() != a.data());
    ZEXPECT(b.ref() == "deep");
}

ZEST_CASE(move_of_borrowed_borrows_the_same_text) {
    const char* text = "shared";
    cow_string a(text);
    cow_string b(std::move(a));
    ZEXPECT(b.is_borrowed());
    ZEXPECT(b.data() == text);
    ZEXPECT(a.empty());
}

ZEST_CASE(move_hands_over_and_empties) {
    cow_string a = cow_string::owned(string_ref("move me"));
    const auto* data = a.data();
    cow_string b(std::move(a));
    ZEXPECT(b.is_owned());
    ZEXPECT(b.data() == data);
    ZEXPECT(a.empty());
    ZEXPECT(a.data() == nullptr);
    ZEXPECT(a.is_borrowed());
}

ZEST_CASE(copy_assignment_owns_its_own) {
    cow_string a = cow_string::owned(string_ref("original"));
    cow_string b("other");
    b = a;
    ZEXPECT(b.is_owned());
    ZEXPECT(b.data() != a.data());
    ZEXPECT(b.ref() == "original");

    const auto* data = b.data();
    const auto& same = b;
    b = same;
    ZEXPECT(b.data() == data);
}

ZEST_CASE(copy_assignment_into_an_owned_string_frees_its_text) {
    cow_string a = cow_string::owned(string_ref("new text"));
    cow_string b = cow_string::owned(string_ref("old text"));
    b = a;
    ZEXPECT(b.is_owned());
    ZEXPECT(b.ref() == "new text");
    const cow_string borrowed("borrowed");
    b = borrowed;
    ZEXPECT(b.is_borrowed());
    ZEXPECT(b.data() == borrowed.data());
}

ZEST_CASE(move_assignment_frees_the_old_text) {
    cow_string a = cow_string::owned(string_ref("transfer"));
    cow_string b = cow_string::owned(string_ref("replaced"));
    const auto* data = a.data();
    b = std::move(a);
    ZEXPECT(b.data() == data);
    ZEXPECT(b.ref() == "transfer");
    ZEXPECT(a.empty());

    auto& same = b;
    b = std::move(same);
    ZEXPECT(b.ref() == "transfer");
}

ZEST_CASE(make_owned_copies_borrowed_text_once) {
    const char* text = "convert";
    cow_string s(text);
    s.make_owned();
    ZEXPECT(s.is_owned());
    ZEXPECT(s.data() != text);
    const auto* data = s.data();
    s.make_owned();
    ZEXPECT(s.data() == data);
    ZEXPECT(s.ref() == "convert");
}

ZEST_CASE(make_owned_of_empty_stays_borrowed) {
    cow_string s;
    s.make_owned();
    ZEXPECT(s.is_borrowed());
}

ZEST_CASE(release_of_owned_hands_over_the_buffer) {
    cow_string s = cow_string::owned(string_ref("release me"));
    const auto* data = s.data();
    small_string<4> released = s.release<4>();
    ZEXPECT(released.ref() == "release me");
    ZEXPECT(released.data() == data);
    ZEXPECT(s.empty());
    ZEXPECT(s.is_borrowed());
}

ZEST_CASE(released_buffer_grows_like_any_other) {
    cow_string s = cow_string::owned(string_ref("release me"));
    small_string<4> released = s.release<4>();
    released += ", then grow past the buffer handed over";
    ZEXPECT(released.ref() == "release me, then grow past the buffer handed over");
    released.clear();
    released.shrink_to_fit();
    ZEXPECT(released.inlined());
}

ZEST_CASE(release_of_empty_is_empty) {
    cow_string s;
    small_string<4> released = s.release<4>();
    ZEXPECT(released.empty());
    ZEXPECT(released.inlined());
}

ZEST_CASE(release_of_borrowed_copies_into_the_inline_buffer) {
    cow_string s("tiny");
    small_string<8> released = s.release<8>();
    ZEXPECT(released.ref() == "tiny");
    ZEXPECT(released.inlined());
    ZEXPECT(s.empty());
}

ZEST_CASE(release_of_borrowed_text_larger_than_the_buffer_allocates) {
    cow_string s("larger than two");
    small_string<2> released = s.release<2>();
    ZEXPECT(released.ref() == "larger than two");
    ZEXPECT(!released.inlined());
}

ZEST_CASE(converts_to_views_and_strings) {
    cow_string s("interop");
    string_ref ref = s;
    std::string_view view = s;
    ZEXPECT(ref == "interop");
    ZEXPECT(view == "interop");
    ZEXPECT(s.to_string() == "interop");
}

ZEST_CASE(compares_by_text) {
    cow_string borrowed("hello");
    cow_string owned = cow_string::owned(string_ref("hello"));
    // The string's own operators, not the checks' comparison.
    ZEXPECT((borrowed == owned));
    ZEXPECT(!(borrowed == cow_string("world")));
    ZEXPECT((borrowed == "hello"));
    ZEXPECT((borrowed == string_ref("hello")));
}

ZEST_CASE(swap_exchanges_modes) {
    cow_string a("aaa");
    cow_string b = cow_string::owned(string_ref("bbb"));
    a.swap(b);
    ZEXPECT(a.ref() == "bbb");
    ZEXPECT(a.is_owned());
    ZEXPECT(b.ref() == "aaa");
    ZEXPECT(b.is_borrowed());
}

};  // ZEST_SUITE(support_cow_string)

}  // namespace

}  // namespace kota
