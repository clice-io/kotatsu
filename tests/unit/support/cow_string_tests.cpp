#include <string>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/support/cow_string.h"

namespace kota {
namespace {

constexpr bool constexpr_cow_string_operations() {
    // borrowed
    cow_string a("hello");
    if(a.size() != 5)
        return false;
    if(!a.is_borrowed())
        return false;

    // owned
    cow_string b = cow_string::owned(string_ref("world"));
    if(b.size() != 5)
        return false;
    if(!b.is_owned())
        return false;

    // copy: borrowed stays borrowed
    cow_string c(a);
    if(!c.is_borrowed())
        return false;

    // copy: owned deep copies
    cow_string d(b);
    if(!d.is_owned())
        return false;

    // move
    cow_string e(std::move(b));
    if(!e.is_owned())
        return false;
    if(!b.empty())
        return false;

    // make_owned
    cow_string f("test");
    f.make_owned();
    if(!f.is_owned())
        return false;
    if(f.ref() != "test")
        return false;

    // release
    cow_string g = cow_string::owned(string_ref("release"));
    small_string<0> s = g.release();
    if(s.ref() != "release")
        return false;
    if(!g.empty())
        return false;

    // comparison
    cow_string h("abc");
    cow_string i = cow_string::owned(string_ref("abc"));
    if(!(h == i))
        return false;

    return true;
}

ZEST_SUITE(cow_string){

    ZEST_CASE(constexpr){static_assert(constexpr_cow_string_operations());

}  // namespace

ZEST_CASE(default_construction) {
    cow_string s;
    EXPECT(s.empty());
    EXPECT(s.size() == 0U);
    EXPECT(s.is_borrowed());
    EXPECT(!s.is_owned());
    EXPECT(s.data() == nullptr);
}

ZEST_CASE(borrowed_construction) {
    const char* literal = "hello world";
    string_ref sr{literal};
    cow_string s{sr};

    EXPECT(s.size() == 11U);
    EXPECT(!s.empty());
    EXPECT(s.is_borrowed());
    EXPECT(!s.is_owned());
    EXPECT(s.data() == literal);
    EXPECT(s.ref() == "hello world");
}

ZEST_CASE(explicit_borrowed) {
    const char* literal = "test";
    cow_string s = cow_string::borrowed(literal);

    EXPECT(s.is_borrowed());
    EXPECT(s.data() == literal);
    EXPECT(s.ref() == "test");
}

ZEST_CASE(owned_from_rvalue_string) {
    std::string original = "owned data";
    cow_string s = cow_string::owned(std::move(original));

    EXPECT(s.is_owned());
    EXPECT(!s.is_borrowed());
    EXPECT(s.ref() == "owned data");
    EXPECT(s.size() == 10U);
}

ZEST_CASE(owned_from_string_ref) {
    const char* literal = "copy me";
    cow_string s = cow_string::owned(string_ref{literal});

    EXPECT(s.is_owned());
    EXPECT(s.ref() == "copy me");
    EXPECT(s.data() != literal);
}

ZEST_CASE(owned_empty) {
    cow_string s = cow_string::owned(string_ref{""});
    EXPECT(s.empty());
    EXPECT(s.is_borrowed());
}

ZEST_CASE(copy_borrowed_stays_borrowed) {
    const char* literal = "shared";
    cow_string a{string_ref{literal}};
    cow_string b{a};

    EXPECT(a.is_borrowed());
    EXPECT(b.is_borrowed());
    EXPECT(a.data() == literal);
    EXPECT(b.data() == literal);
    EXPECT(a.ref() == b.ref());
}

ZEST_CASE(copy_owned_deep_copies) {
    cow_string a = cow_string::owned(string_ref{"deep"});
    cow_string b{a};

    EXPECT(a.is_owned());
    EXPECT(b.is_owned());
    EXPECT(a.data() != b.data());
    EXPECT(a.ref() == b.ref());
    EXPECT(b.ref() == "deep");
}

ZEST_CASE(move_transfers_ownership) {
    cow_string a = cow_string::owned(string_ref{"move me"});
    const char* original_data = a.data();

    cow_string b{std::move(a)};

    EXPECT(b.is_owned());
    EXPECT(b.data() == original_data);
    EXPECT(b.ref() == "move me");

    EXPECT(a.empty());
    EXPECT(a.data() == nullptr);
    EXPECT(a.size() == 0U);
}

ZEST_CASE(move_borrowed) {
    const char* literal = "borrow";
    cow_string a{string_ref{literal}};
    cow_string b{std::move(a)};

    EXPECT(b.is_borrowed());
    EXPECT(b.data() == literal);
    EXPECT(a.empty());
}

ZEST_CASE(copy_assignment) {
    cow_string a = cow_string::owned(string_ref{"original"});
    cow_string b;
    b = a;

    EXPECT(b.is_owned());
    EXPECT(b.ref() == "original");
    EXPECT(a.data() != b.data());
}

ZEST_CASE(move_assignment) {
    cow_string a = cow_string::owned(string_ref{"transfer"});
    const char* data = a.data();
    cow_string b;
    b = std::move(a);

    EXPECT(b.is_owned());
    EXPECT(b.data() == data);
    EXPECT(b.ref() == "transfer");
    EXPECT(a.empty());
}

ZEST_CASE(make_owned) {
    const char* literal = "convert";
    cow_string s{string_ref{literal}};
    EXPECT(s.is_borrowed());
    EXPECT(s.data() == literal);

    s.make_owned();
    EXPECT(s.is_owned());
    EXPECT(s.data() != literal);
    EXPECT(s.ref() == "convert");
}

ZEST_CASE(make_owned_already_owned) {
    cow_string s = cow_string::owned(string_ref{"already"});
    const char* data = s.data();

    s.make_owned();
    EXPECT(s.is_owned());
    EXPECT(s.data() == data);
    EXPECT(s.ref() == "already");
}

ZEST_CASE(make_owned_empty) {
    cow_string s;
    s.make_owned();
    EXPECT(s.is_borrowed());
    EXPECT(s.empty());
}

ZEST_CASE(string_ref_interop) {
    cow_string s{string_ref{"interop"}};

    string_ref sr = s;
    EXPECT(sr == "interop");

    EXPECT(s.ref() == "interop");
    EXPECT(s.ref().size() == 7U);

    EXPECT(zest::starts_with(s.ref(), "inter"));
    EXPECT(zest::ends_with(s.ref(), "op"));
}

ZEST_CASE(to_string) {
    cow_string s{string_ref{"convert"}};
    std::string result = s.to_string();
    EXPECT(result == "convert");
}

ZEST_CASE(comparison) {
    cow_string a{string_ref{"hello"}};
    cow_string b = cow_string::owned(string_ref{"hello"});
    cow_string c{string_ref{"world"}};

    EXPECT(a == b);
    EXPECT(a != c);
    EXPECT(a == "hello");
    EXPECT(a == string_ref{"hello"});
}

ZEST_CASE(swap) {
    cow_string a{string_ref{"aaa"}};
    cow_string b = cow_string::owned(string_ref{"bbb"});

    EXPECT(a.is_borrowed());
    EXPECT(b.is_owned());

    a.swap(b);

    EXPECT(a.ref() == "bbb");
    EXPECT(a.is_owned());
    EXPECT(b.ref() == "aaa");
    EXPECT(b.is_borrowed());
}

ZEST_CASE(self_assignment) {
    cow_string s = cow_string::owned(string_ref{"self"});
    const char* data = s.data();

    auto& alias = s;
    s = alias;

    EXPECT(s.ref() == "self");
    EXPECT(s.data() == data);
    EXPECT(s.is_owned());
}

ZEST_CASE(release_owned) {
    cow_string s = cow_string::owned(string_ref{"release me"});
    const char* data = s.data();

    small_string<0> ss = s.release();

    // cow_string is now empty.
    EXPECT(s.empty());
    EXPECT(s.data() == nullptr);
    EXPECT(s.is_borrowed());

    // small_string holds the buffer without copy.
    EXPECT(ss.data() == data);
    EXPECT(ss.ref() == "release me");
}

ZEST_CASE(release_borrowed_makes_copy) {
    const char* literal = "borrow then release";
    cow_string s{string_ref{literal}};
    EXPECT(s.is_borrowed());

    small_string<0> ss = s.release();

    // Should have made an owned copy before releasing.
    EXPECT(ss.data() != literal);
    EXPECT(ss.ref() == "borrow then release");
}

ZEST_CASE(release_empty) {
    cow_string s;
    small_string<0> ss = s.release();

    EXPECT(ss.empty());
}

ZEST_CASE(release_usable_as_small_string) {
    cow_string s = cow_string::owned(string_ref{"growable"});

    small_string<0> ss = s.release();
    EXPECT(ss.ref() == "growable");

    // The released small_string is fully functional.
    ss += "!";
    EXPECT(ss.ref() == "growable!");
}

ZEST_CASE(release_borrowed_fits_inline) {
    // "hi" (2 chars) fits in small_string<32>'s inline buffer.
    const char* literal = "hi";
    cow_string s{string_ref{literal}};
    EXPECT(s.is_borrowed());

    small_string<32> ss = s.release<32>();

    // Data should be in inline storage, not a heap copy.
    EXPECT(ss.inlined());
    EXPECT(ss.ref() == "hi");
}

};  // namespace kota

}  // namespace
}  // namespace kota
