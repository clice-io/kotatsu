#include <string>
#include <utility>

#include "eventide/zest/zest.h"
#include "eventide/common/cow_string.h"

namespace eventide {

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

TEST_SUITE(cow_string) {

TEST_CASE(constexpr) {
    static_assert(constexpr_cow_string_operations());
}

TEST_CASE(default_construction) {
    cow_string s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0U);
    EXPECT_TRUE(s.is_borrowed());
    EXPECT_FALSE(s.is_owned());
    EXPECT_EQ(s.data(), nullptr);
}

TEST_CASE(borrowed_construction) {
    const char* literal = "hello world";
    string_ref sr{literal};
    cow_string s{sr};

    EXPECT_EQ(s.size(), 11U);
    EXPECT_FALSE(s.empty());
    EXPECT_TRUE(s.is_borrowed());
    EXPECT_FALSE(s.is_owned());
    EXPECT_EQ(s.data(), literal);
    EXPECT_EQ(s.ref(), "hello world");
}

TEST_CASE(explicit_borrowed) {
    const char* literal = "test";
    cow_string s = cow_string::borrowed(literal);

    EXPECT_TRUE(s.is_borrowed());
    EXPECT_EQ(s.data(), literal);
    EXPECT_EQ(s.ref(), "test");
}

TEST_CASE(owned_from_rvalue_string) {
    std::string original = "owned data";
    cow_string s = cow_string::owned(std::move(original));

    EXPECT_TRUE(s.is_owned());
    EXPECT_FALSE(s.is_borrowed());
    EXPECT_EQ(s.ref(), "owned data");
    EXPECT_EQ(s.size(), 10U);
}

TEST_CASE(owned_from_string_ref) {
    const char* literal = "copy me";
    cow_string s = cow_string::owned(string_ref{literal});

    EXPECT_TRUE(s.is_owned());
    EXPECT_EQ(s.ref(), "copy me");
    EXPECT_NE(s.data(), literal);
}

TEST_CASE(owned_empty) {
    cow_string s = cow_string::owned(string_ref{""});
    EXPECT_TRUE(s.empty());
    EXPECT_TRUE(s.is_borrowed());
}

TEST_CASE(copy_borrowed_stays_borrowed) {
    const char* literal = "shared";
    cow_string a{string_ref{literal}};
    cow_string b{a};

    EXPECT_TRUE(a.is_borrowed());
    EXPECT_TRUE(b.is_borrowed());
    EXPECT_EQ(a.data(), literal);
    EXPECT_EQ(b.data(), literal);
    EXPECT_EQ(a.ref(), b.ref());
}

TEST_CASE(copy_owned_deep_copies) {
    cow_string a = cow_string::owned(string_ref{"deep"});
    cow_string b{a};

    EXPECT_TRUE(a.is_owned());
    EXPECT_TRUE(b.is_owned());
    EXPECT_NE(a.data(), b.data());
    EXPECT_EQ(a.ref(), b.ref());
    EXPECT_EQ(b.ref(), "deep");
}

TEST_CASE(move_transfers_ownership) {
    cow_string a = cow_string::owned(string_ref{"move me"});
    const char* original_data = a.data();

    cow_string b{std::move(a)};

    EXPECT_TRUE(b.is_owned());
    EXPECT_EQ(b.data(), original_data);
    EXPECT_EQ(b.ref(), "move me");

    EXPECT_TRUE(a.empty());
    EXPECT_EQ(a.data(), nullptr);
    EXPECT_EQ(a.size(), 0U);
}

TEST_CASE(move_borrowed) {
    const char* literal = "borrow";
    cow_string a{string_ref{literal}};
    cow_string b{std::move(a)};

    EXPECT_TRUE(b.is_borrowed());
    EXPECT_EQ(b.data(), literal);
    EXPECT_TRUE(a.empty());
}

TEST_CASE(copy_assignment) {
    cow_string a = cow_string::owned(string_ref{"original"});
    cow_string b;
    b = a;

    EXPECT_TRUE(b.is_owned());
    EXPECT_EQ(b.ref(), "original");
    EXPECT_NE(a.data(), b.data());
}

TEST_CASE(move_assignment) {
    cow_string a = cow_string::owned(string_ref{"transfer"});
    const char* data = a.data();
    cow_string b;
    b = std::move(a);

    EXPECT_TRUE(b.is_owned());
    EXPECT_EQ(b.data(), data);
    EXPECT_EQ(b.ref(), "transfer");
    EXPECT_TRUE(a.empty());
}

TEST_CASE(make_owned) {
    const char* literal = "convert";
    cow_string s{string_ref{literal}};
    EXPECT_TRUE(s.is_borrowed());
    EXPECT_EQ(s.data(), literal);

    s.make_owned();
    EXPECT_TRUE(s.is_owned());
    EXPECT_NE(s.data(), literal);
    EXPECT_EQ(s.ref(), "convert");
}

TEST_CASE(make_owned_already_owned) {
    cow_string s = cow_string::owned(string_ref{"already"});
    const char* data = s.data();

    s.make_owned();
    EXPECT_TRUE(s.is_owned());
    EXPECT_EQ(s.data(), data);
    EXPECT_EQ(s.ref(), "already");
}

TEST_CASE(make_owned_empty) {
    cow_string s;
    s.make_owned();
    EXPECT_TRUE(s.is_borrowed());
    EXPECT_TRUE(s.empty());
}

TEST_CASE(string_ref_interop) {
    cow_string s{string_ref{"interop"}};

    string_ref sr = s;
    EXPECT_EQ(sr, "interop");

    EXPECT_EQ(s.ref(), "interop");
    EXPECT_EQ(s.ref().size(), 7U);

    EXPECT_TRUE(s.ref().starts_with("inter"));
    EXPECT_TRUE(s.ref().ends_with("op"));
}

TEST_CASE(to_string) {
    cow_string s{string_ref{"convert"}};
    std::string result = s.to_string();
    EXPECT_EQ(result, "convert");
}

TEST_CASE(comparison) {
    cow_string a{string_ref{"hello"}};
    cow_string b = cow_string::owned(string_ref{"hello"});
    cow_string c{string_ref{"world"}};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a == "hello");
    EXPECT_TRUE(a == string_ref{"hello"});
}

TEST_CASE(swap) {
    cow_string a{string_ref{"aaa"}};
    cow_string b = cow_string::owned(string_ref{"bbb"});

    EXPECT_TRUE(a.is_borrowed());
    EXPECT_TRUE(b.is_owned());

    a.swap(b);

    EXPECT_EQ(a.ref(), "bbb");
    EXPECT_TRUE(a.is_owned());
    EXPECT_EQ(b.ref(), "aaa");
    EXPECT_TRUE(b.is_borrowed());
}

TEST_CASE(self_assignment) {
    cow_string s = cow_string::owned(string_ref{"self"});
    const char* data = s.data();

    s = s;

    EXPECT_EQ(s.ref(), "self");
    EXPECT_EQ(s.data(), data);
    EXPECT_TRUE(s.is_owned());
}

TEST_CASE(release_owned) {
    cow_string s = cow_string::owned(string_ref{"release me"});
    const char* data = s.data();

    small_string<0> ss = s.release();

    // cow_string is now empty.
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.data(), nullptr);
    EXPECT_TRUE(s.is_borrowed());

    // small_string holds the buffer without copy.
    EXPECT_EQ(ss.data(), data);
    EXPECT_EQ(ss.ref(), "release me");
}

TEST_CASE(release_borrowed_makes_copy) {
    const char* literal = "borrow then release";
    cow_string s{string_ref{literal}};
    EXPECT_TRUE(s.is_borrowed());

    small_string<0> ss = s.release();

    // Should have made an owned copy before releasing.
    EXPECT_NE(ss.data(), literal);
    EXPECT_EQ(ss.ref(), "borrow then release");
}

TEST_CASE(release_empty) {
    cow_string s;
    small_string<0> ss = s.release();

    EXPECT_TRUE(ss.empty());
}

TEST_CASE(release_usable_as_small_string) {
    cow_string s = cow_string::owned(string_ref{"growable"});

    small_string<0> ss = s.release();
    EXPECT_EQ(ss.ref(), "growable");

    // The released small_string is fully functional.
    ss += "!";
    EXPECT_EQ(ss.ref(), "growable!");
}

TEST_CASE(release_borrowed_fits_inline) {
    // "hi" (2 chars) fits in small_string<32>'s inline buffer.
    const char* literal = "hi";
    cow_string s{string_ref{literal}};
    EXPECT_TRUE(s.is_borrowed());

    small_string<32> ss = s.release<32>();

    // Data should be in inline storage, not a heap copy.
    EXPECT_TRUE(ss.inlined());
    EXPECT_EQ(ss.ref(), "hi");
}

};  // TEST_SUITE(cow_string)

}  // namespace eventide
