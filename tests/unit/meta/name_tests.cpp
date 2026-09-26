#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/name.h"

namespace kota::meta {

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

}  // namespace qualified_types

namespace type_cases {

template <typename T>
struct box {};

template <typename T, typename U>
struct pair_box {};

}  // namespace type_cases

using templ_t = type_cases::box<qualified_types::Outer::Inner>;
using nested_t = type_cases::pair_box<qualified_types::Outer, templ_t>;

enum class sparse_enum : int {
    RED = -12,
    GREEN = 7,
    BLUE = 15,
};

enum sparse_plain {
    Up = 85,
    Down = -42,
    Left = -120,
};

struct nested_holder {
    struct leaf {
        int field;
    };

    leaf value;
};

inline int global_value = 0;

inline int global_fn(double) {
    return 0;
}

inline int global_array[3] = {0, 1, 2};

inline int global_overload(int) {
    return 0;
}

[[maybe_unused]] inline int global_overload(double) {
    return 0;
}

template <typename T>
T global_tpl(T v) {
    return v;
}

namespace pointer_cases {

inline int namespaced_value = 0;

inline void namespaced_fn() {}

inline int namespaced_array[2] = {0, 1};

[[maybe_unused]] inline long overloaded_fn(int) {
    return 0;
}

inline long overloaded_fn(double) {
    return 0;
}

template <typename T>
T namespaced_tpl(T v) {
    return v;
}

}  // namespace pointer_cases

struct struct_y {
    std::string x;
    std::vector<int> y;

    inline static int static_data = 0;

    static void static_fn() {}

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

union union_z {
    std::string x;
    std::vector<int> y;

    union_z() {}

    ~union_z() {}
};

enum class enum_y { RED, YELLOW };

inline struct_y ins_y;
inline union_z ins_y2;
inline nested_holder nested;

constexpr auto short_outer = type_name<qualified_types::Outer>();
constexpr auto full_outer = type_name<qualified_types::Outer>(true);
constexpr auto short_inner = type_name<qualified_types::Outer::Inner>();
constexpr auto full_inner = type_name<qualified_types::Outer::Inner>(true);

constexpr auto short_templ = type_name<templ_t>();
constexpr auto full_templ = type_name<templ_t>(true);
constexpr auto short_nested = type_name<nested_t>();
constexpr auto full_nested = type_name<nested_t>(true);

ZEST_SUITE(reflection){

    ZEST_CASE(type_name){EXPECT(type_name<int>() == "int");

EXPECT(type_name<struct_x>() == "struct_x");
EXPECT(type_name<class_x>() == "class_x");
EXPECT(type_name<enum_x>() == "enum_x");
EXPECT(type_name<union_x>() == "union_x");

struct struct_y;
class class_y;
enum class enum_y;
union union_y;
EXPECT(type_name<struct_y>() == "struct_y");
EXPECT(type_name<class_y>() == "class_y");
EXPECT(type_name<enum_y>() == "enum_y");
EXPECT(type_name<union_y>() == "union_y");

EXPECT(type_name<local_types::struct_z>() == "struct_z");
EXPECT(type_name<local_types::class_z>() == "class_z");
EXPECT(type_name<local_types::enum_z>() == "enum_z");
EXPECT(type_name<local_types::union_z>() == "union_z");

}  // namespace

ZEST_CASE(qualified_type_name) {
    EXPECT(short_outer == "Outer");
    EXPECT(zest::ends_with(full_outer, "qualified_types::Outer"));
    EXPECT(short_inner == "Inner");
    EXPECT(zest::ends_with(full_inner, "qualified_types::Outer::Inner"));
}

ZEST_CASE(type_name_combinations) {
    EXPECT(zest::starts_with(short_templ, "box<"));
    EXPECT(zest::contains(short_templ, "Inner"));
    EXPECT(zest::contains(full_templ, "type_cases::box<"));
    EXPECT(zest::contains(full_templ, "qualified_types::Outer::Inner"));

    EXPECT(zest::starts_with(short_nested, "pair_box<"));
    EXPECT(zest::contains(short_nested, "box<"));
    EXPECT(zest::contains(full_nested, "type_cases::pair_box<"));
    EXPECT(zest::contains(full_nested, "qualified_types::Outer"));
}

ZEST_CASE(pointer_name) {
    EXPECT(pointer_name<&ins_y.x>() == "x");
    EXPECT(pointer_name<&ins_y.y>() == "y");

    EXPECT(pointer_name<&ins_y2.x>() == "x");
    EXPECT(pointer_name<&ins_y2.y>() == "y");
}

ZEST_CASE(nested_member_pointer_name) {
    EXPECT(pointer_name<&nested.value.field>() == "field");
}

ZEST_CASE(pointer_name_combinations) {
    EXPECT(pointer_name<&global_value>() == "global_value");
    EXPECT(pointer_name<(&global_value)>() == "global_value");
    EXPECT(pointer_name<&global_fn>() == "global_fn");
    EXPECT(pointer_name<&global_array>() == "global_array");
    EXPECT(pointer_name<static_cast<int (*)(int)>(&global_overload)>() == "global_overload");
    EXPECT(pointer_name<&global_tpl<int>>() == "global_tpl");
    EXPECT(pointer_name<&global_tpl<std::vector<int>>>() == "global_tpl");

    EXPECT(pointer_name<&pointer_cases::namespaced_value>() == "namespaced_value");
    EXPECT(pointer_name<(&pointer_cases::namespaced_value)>() == "namespaced_value");
    EXPECT(pointer_name<&pointer_cases::namespaced_fn>() == "namespaced_fn");
    EXPECT(pointer_name<&pointer_cases::namespaced_array>() == "namespaced_array");
    EXPECT(pointer_name<static_cast<long (*)(double)>(&pointer_cases::overloaded_fn)>() ==
           "overloaded_fn");
    EXPECT(pointer_name<&pointer_cases::namespaced_tpl<long>>() == "namespaced_tpl");

    EXPECT(pointer_name<&struct_y::static_data>() == "static_data");
    EXPECT(pointer_name<&struct_y::static_fn>() == "static_fn");
}

ZEST_CASE(basic_member_name) {
    EXPECT(member_name<&struct_y::x>() == "x");
    EXPECT(member_name<&struct_y::y>() == "y");
    EXPECT(member_name<&struct_y::clear_x>() == "clear_x");
    EXPECT(member_name<&struct_y::size>() == "size");
    EXPECT(member_name<&struct_y::size_lref>() == "size_lref");
    EXPECT(member_name<&struct_y::size_rref>() == "size_rref");
    EXPECT(member_name<&struct_y::size_noexcept>() == "size_noexcept");
    EXPECT(member_name<static_cast<int (struct_y::*)(int)>(&struct_y::overloaded)>() ==
           "overloaded");
    EXPECT(member_name<static_cast<int (struct_y::*)(double) const>(&struct_y::overloaded)>() ==
           "overloaded");
    EXPECT(member_name<&struct_y::cast_size<int>>() == "cast_size");
    EXPECT(member_name<&nested_holder::value>() == "value");
    EXPECT(member_name<&nested_holder::leaf::field>() == "field");
}

ZEST_CASE(enum_name) {
    EXPECT(enum_name<enum_y::RED>() == "RED");
    EXPECT(enum_name<enum_y::YELLOW>() == "YELLOW");
}

ZEST_CASE(enum_name_sparse_values) {
    EXPECT(enum_name<sparse_enum::RED>() == "RED");
    EXPECT(enum_name<sparse_enum::GREEN>() == "GREEN");
    EXPECT(enum_name<sparse_enum::BLUE>() == "BLUE");
    EXPECT(enum_name<Up>() == "Up");
    EXPECT(enum_name<Down>() == "Down");
    EXPECT(enum_name<Left>() == "Left");
}

};  // namespace kota::meta

}  // namespace

}  // namespace kota::meta
