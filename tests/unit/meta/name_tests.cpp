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

inline int global_overload(double) {
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

inline long overloaded_fn(int) {
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

TEST_SUITE(reflection) {

TEST_CASE(type_name) {
    EXPECT_EQ(type_name<int>(), "int");

    EXPECT_EQ(type_name<struct_x>(), "struct_x");
    EXPECT_EQ(type_name<class_x>(), "class_x");
    EXPECT_EQ(type_name<enum_x>(), "enum_x");
    EXPECT_EQ(type_name<union_x>(), "union_x");

    struct struct_y;
    class class_y;
    enum class enum_y;
    union union_y;
    EXPECT_EQ(type_name<struct_y>(), "struct_y");
    EXPECT_EQ(type_name<class_y>(), "class_y");
    EXPECT_EQ(type_name<enum_y>(), "enum_y");
    EXPECT_EQ(type_name<union_y>(), "union_y");

    EXPECT_EQ(type_name<local_types::struct_z>(), "struct_z");
    EXPECT_EQ(type_name<local_types::class_z>(), "class_z");
    EXPECT_EQ(type_name<local_types::enum_z>(), "enum_z");
    EXPECT_EQ(type_name<local_types::union_z>(), "union_z");
}

TEST_CASE(qualified_type_name) {
    EXPECT_EQ(short_outer, "Outer");
    EXPECT_TRUE(full_outer.ends_with("qualified_types::Outer"));
    EXPECT_EQ(short_inner, "Inner");
    EXPECT_TRUE(full_inner.ends_with("qualified_types::Outer::Inner"));
}

TEST_CASE(type_name_combinations) {
    EXPECT_TRUE(short_templ.starts_with("box<"));
    EXPECT_TRUE(short_templ.find("Inner") != std::string_view::npos);
    EXPECT_TRUE(full_templ.find("type_cases::box<") != std::string_view::npos);
    EXPECT_TRUE(full_templ.find("qualified_types::Outer::Inner") != std::string_view::npos);

    EXPECT_TRUE(short_nested.starts_with("pair_box<"));
    EXPECT_TRUE(short_nested.find("box<") != std::string_view::npos);
    EXPECT_TRUE(full_nested.find("type_cases::pair_box<") != std::string_view::npos);
    EXPECT_TRUE(full_nested.find("qualified_types::Outer") != std::string_view::npos);
}

TEST_CASE(pointer_name) {
    EXPECT_EQ(pointer_name<&ins_y.x>(), "x");
    EXPECT_EQ(pointer_name<&ins_y.y>(), "y");

    EXPECT_EQ(pointer_name<&ins_y2.x>(), "x");
    EXPECT_EQ(pointer_name<&ins_y2.y>(), "y");
}

TEST_CASE(nested_member_pointer_name) {
    EXPECT_EQ(pointer_name<&nested.value.field>(), "field");
}

TEST_CASE(pointer_name_combinations) {
    EXPECT_EQ(pointer_name<&global_value>(), "global_value");
    EXPECT_EQ(pointer_name<(&global_value)>(), "global_value");
    EXPECT_EQ(pointer_name<&global_fn>(), "global_fn");
    EXPECT_EQ(pointer_name<&global_array>(), "global_array");
    EXPECT_EQ(pointer_name<static_cast<int (*)(int)>(&global_overload)>(), "global_overload");
    EXPECT_EQ(pointer_name<&global_tpl<int>>(), "global_tpl");
    EXPECT_EQ(pointer_name<&global_tpl<std::vector<int>>>(), "global_tpl");

    EXPECT_EQ(pointer_name<&pointer_cases::namespaced_value>(), "namespaced_value");
    EXPECT_EQ(pointer_name<(&pointer_cases::namespaced_value)>(), "namespaced_value");
    EXPECT_EQ(pointer_name<&pointer_cases::namespaced_fn>(), "namespaced_fn");
    EXPECT_EQ(pointer_name<&pointer_cases::namespaced_array>(), "namespaced_array");
    EXPECT_EQ(pointer_name<static_cast<long (*)(double)>(&pointer_cases::overloaded_fn)>(),
              "overloaded_fn");
    EXPECT_EQ(pointer_name<&pointer_cases::namespaced_tpl<long>>(), "namespaced_tpl");

    EXPECT_EQ(pointer_name<&struct_y::static_data>(), "static_data");
    EXPECT_EQ(pointer_name<&struct_y::static_fn>(), "static_fn");
}

TEST_CASE(basic_member_name) {
    EXPECT_EQ(member_name<&struct_y::x>(), "x");
    EXPECT_EQ(member_name<&struct_y::y>(), "y");
    EXPECT_EQ(member_name<&struct_y::clear_x>(), "clear_x");
    EXPECT_EQ(member_name<&struct_y::size>(), "size");
    EXPECT_EQ(member_name<&struct_y::size_lref>(), "size_lref");
    EXPECT_EQ(member_name<&struct_y::size_rref>(), "size_rref");
    EXPECT_EQ(member_name<&struct_y::size_noexcept>(), "size_noexcept");
    EXPECT_EQ(member_name<static_cast<int (struct_y::*)(int)>(&struct_y::overloaded)>(),
              "overloaded");
    EXPECT_EQ(member_name<static_cast<int (struct_y::*)(double) const>(&struct_y::overloaded)>(),
              "overloaded");
    EXPECT_EQ(member_name<&struct_y::cast_size<int>>(), "cast_size");
    EXPECT_EQ(member_name<&nested_holder::value>(), "value");
    EXPECT_EQ(member_name<&nested_holder::leaf::field>(), "field");
}

TEST_CASE(enum_name) {
    EXPECT_EQ(enum_name<enum_y::RED>(), "RED");
    EXPECT_EQ(enum_name<enum_y::YELLOW>(), "YELLOW");
}

TEST_CASE(enum_name_sparse_values) {
    EXPECT_EQ(enum_name<sparse_enum::RED>(), "RED");
    EXPECT_EQ(enum_name<sparse_enum::GREEN>(), "GREEN");
    EXPECT_EQ(enum_name<sparse_enum::BLUE>(), "BLUE");
    EXPECT_EQ(enum_name<Up>(), "Up");
    EXPECT_EQ(enum_name<Down>(), "Down");
    EXPECT_EQ(enum_name<Left>(), "Left");
}

};  // TEST_SUITE(reflection)

}  // namespace

}  // namespace kota::meta
