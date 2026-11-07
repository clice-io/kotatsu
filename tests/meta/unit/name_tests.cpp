#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/name.h"

namespace kotatsu::meta {

namespace {

struct struct_x;
class class_x;
enum class enum_x;
union union_x;

namespace local_types {

struct struct_z;
class class_z;
enum class enum_z;
union union_z;

}  // namespace local_types

namespace qualified_types {

struct Outer {
    struct Inner {};
};

/// A string as a template argument, a bracket in it.
template <std::size_t N>
struct text {
    char value[N];

    consteval text(const char (&chars)[N]) {
        for(std::size_t i = 0; i < N; ++i) {
            value[i] = chars[i];
        }
    }
};

template <text Label>
struct labeled {};

}  // namespace qualified_types

namespace type_cases {

template <typename T>
struct box {};

template <typename T, typename U>
struct pair_box {};

}  // namespace type_cases

using templ_t = type_cases::box<qualified_types::Outer::Inner>;
using nested_t = type_cases::pair_box<qualified_types::Outer, templ_t>;

struct nested_holder {
    struct leaf {
        int field;
    };

    leaf value;
};

struct first_base {
    int a;
};

struct second_base {
    int b;
};

/// A class with more than one base, a pointer to a member of which MSVC prints with the
/// offsets that adjust it.
struct two_bases : first_base, second_base {
    void run() {}
};

/// Members whose names hold brackets no other closes.
struct ordered {
    int key;

    bool operator<(const ordered& other) const {
        return key < other.key;
    }

    bool operator>=(const ordered& other) const {
        return key >= other.key;
    }

    int operator()(int x) const {
        return key + x;
    }
};

struct struct_y {
    std::string x;
    std::vector<int> y;

    void clear_x() {
        x.clear();
    }

    int size() const {
        return static_cast<int>(y.size());
    }

    int size_lref() & {
        return size();
    }

    int size_rref() && {
        return size();
    }

    int size_noexcept() const noexcept {
        return size();
    }

    int overloaded(int v) {
        return v;
    }

    int overloaded(double v) const {
        return static_cast<int>(v);
    }

    template <typename T>
    T cast_size() const {
        return static_cast<T>(y.size());
    }
};

constexpr auto short_outer = type_name<qualified_types::Outer>;
constexpr auto full_outer = qualified_type_name<qualified_types::Outer>;
constexpr auto short_inner = type_name<qualified_types::Outer::Inner>;
constexpr auto full_inner = qualified_type_name<qualified_types::Outer::Inner>;

constexpr auto short_templ = type_name<templ_t>;
constexpr auto full_templ = qualified_type_name<templ_t>;
constexpr auto short_nested = type_name<nested_t>;
constexpr auto full_nested = qualified_type_name<nested_t>;

ZEST_SUITE(meta_name) {

ZEST_CASE(type_name) {
    ZEXPECT(type_name<int> == "int");
    // A name of more than one word keeps every word.
    ZEXPECT(type_name<unsigned int> == "unsigned int");

    ZEXPECT(type_name<struct_x> == "struct_x");
    ZEXPECT(type_name<class_x> == "class_x");
    ZEXPECT(type_name<enum_x> == "enum_x");
    ZEXPECT(type_name<union_x> == "union_x");

    struct struct_y;
    class class_y;
    enum class enum_y;
    union union_y;
    ZEXPECT(type_name<struct_y> == "struct_y");
    ZEXPECT(type_name<class_y> == "class_y");
    ZEXPECT(type_name<enum_y> == "enum_y");
    ZEXPECT(type_name<union_y> == "union_y");

    ZEXPECT(type_name<local_types::struct_z> == "struct_z");
    ZEXPECT(type_name<local_types::class_z> == "class_z");
    ZEXPECT(type_name<local_types::enum_z> == "enum_z");
    ZEXPECT(type_name<local_types::union_z> == "union_z");
}

ZEST_CASE(type_name_past_a_bracket_in_a_literal) {
    ZEXPECT(zest::starts_with(type_name<qualified_types::labeled<"greater >">>, "labeled<"));
}

ZEST_CASE(qualified_type_name) {
    ZEXPECT(short_outer == "Outer");
    ZEXPECT(zest::ends_with(full_outer, "qualified_types::Outer"));
    ZEXPECT(short_inner == "Inner");
    ZEXPECT(zest::ends_with(full_inner, "qualified_types::Outer::Inner"));
}

ZEST_CASE(type_name_combinations) {
    ZEXPECT(zest::starts_with(short_templ, "box<"));
    ZEXPECT(zest::contains(short_templ, "Inner"));
    ZEXPECT(zest::contains(full_templ, "type_cases::box<"));
    ZEXPECT(zest::contains(full_templ, "qualified_types::Outer::Inner"));

    ZEXPECT(zest::starts_with(short_nested, "pair_box<"));
    ZEXPECT(zest::contains(short_nested, "box<"));
    ZEXPECT(zest::contains(full_nested, "type_cases::pair_box<"));
    ZEXPECT(zest::contains(full_nested, "qualified_types::Outer"));
}

ZEST_CASE(basic_member_name) {
    ZEXPECT(member_name<&struct_y::x> == "x");
    ZEXPECT(member_name<&struct_y::y> == "y");
    ZEXPECT(member_name<&struct_y::clear_x> == "clear_x");
    ZEXPECT(member_name<&struct_y::size> == "size");
    ZEXPECT(member_name<&struct_y::size_lref> == "size_lref");
    ZEXPECT(member_name<&struct_y::size_rref> == "size_rref");
    ZEXPECT(member_name<&struct_y::size_noexcept> == "size_noexcept");
    ZEXPECT(member_name<static_cast<int (struct_y::*)(int)>(&struct_y::overloaded)> ==
            "overloaded");
    ZEXPECT(member_name<static_cast<int (struct_y::*)(double) const>(&struct_y::overloaded)> ==
            "overloaded");
    ZEXPECT(member_name<&struct_y::cast_size<int>> == "cast_size");
    ZEXPECT(member_name<&nested_holder::value> == "value");
    ZEXPECT(member_name<&nested_holder::leaf::field> == "field");
    ZEXPECT(member_name<&two_bases::run> == "run");
    // MSVC spells an operator's name with a space after `operator`.
    constexpr std::string_view less = member_name<(&ordered::operator<)>;
    constexpr std::string_view at_least = member_name<(&ordered::operator>=)>;
    constexpr std::string_view call = member_name<(&ordered::operator())>;
    ZEXPECT(zest::starts_with(less, "operator"));
    ZEXPECT(zest::ends_with(less, "<"));
    ZEXPECT(zest::starts_with(at_least, "operator"));
    ZEXPECT(zest::ends_with(at_least, ">="));
    ZEXPECT(zest::starts_with(call, "operator"));
    ZEXPECT(zest::ends_with(call, "()"));
}

};  // ZEST_SUITE(meta_name)

}  // namespace

}  // namespace kotatsu::meta
