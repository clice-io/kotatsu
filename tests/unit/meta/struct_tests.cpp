#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/meta/struct.h"

namespace kota::meta {

namespace {

struct Point {
    int x;
    char c;
    double z;
    int y;
};

struct Layout {
    float a;
    char b;
    char bb;
    int c;
};

#pragma pack(push, 1)

struct Packed {
    float a;
    char b;
    char bb;
    int c;
    double d;
    char e;
};

#pragma pack(pop)

struct Empty {};

using point_rvalue_refs = decltype(field_refs(std::declval<Point&&>()));

struct MoveOnly {
    MoveOnly() = default;
    MoveOnly(MoveOnly&&) = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(MoveOnly&&) = default;
    MoveOnly& operator=(const MoveOnly&) = delete;
};

struct FromIntOnly {
    FromIntOnly(int) {}
};

struct Many36 {
    int f01;
    int f02;
    int f03;
    int f04;
    int f05;
    int f06;
    int f07;
    int f08;
    int f09;
    int f10;
    int f11;
    int f12;
    int f13;
    int f14;
    int f15;
    int f16;
    int f17;
    int f18;
    int f19;
    int f20;
    int f21;
    int f22;
    int f23;
    int f24;
    int f25;
    int f26;
    int f27;
    int f28;
    int f29;
    int f30;
    int f31;
    int f32;
    int f33;
    int f34;
    int f35;
    int f36;
};

struct NoUniqueAddress {
    int i;
#ifdef _MSC_VER
    [[msvc::no_unique_address]] Empty e;
#else
    [[no_unique_address]] Empty e;
#endif
};

struct NoDefault {
    NoDefault() = delete;

    explicit NoDefault(int value) : x(value) {}

    int x;
    double y;
};

template <class T>
consteval bool field_count_robustness_check() {
    struct S4 {
        T a;
        T b;
        int c;
        T d;
    };

    struct S5 {
        int a;
        T b;
        int c;
        T d;
        int e;
    };

    struct S6 {
        T a;
        T b;
        T c;
        T d;
        T e;
        T f;
    };

    return reflection<S4>::field_count == 4 && reflection<S5>::field_count == 5 &&
           reflection<S6>::field_count == 6;
}

TEST_SUITE(reflection) {

TEST_CASE(field_addr_and_field_of) {
    Point p{.x = 1, .c = 'A', .z = 2.5, .y = 4};

    EXPECT_EQ(field_addr_of<0>(p), &p.x);
    EXPECT_EQ(field_addr_of<1>(p), &p.c);
    EXPECT_EQ(field_addr_of<2>(p), &p.z);
    EXPECT_EQ(field_addr_of<3>(p), &p.y);

    field_of<0>(p) = 11;
    field_of<3>(p) = 44;
    EXPECT_EQ(p.x, 11);
    EXPECT_EQ(p.y, 44);

    field<0, Point> fx{p};
    field<1, Point> fc{p};
    fx.value() = 21;
    fc.value() = 'Z';
    EXPECT_EQ(p.x, 21);
    EXPECT_EQ(p.c, 'Z');
}

TEST_CASE(field_name_values) {
    EXPECT_EQ(field_names<Point>().size(), 4U);
    EXPECT_EQ(field_name<0, Point>(), "x");
    EXPECT_EQ(field_name<1, Point>(), "c");
    EXPECT_EQ(field_name<2, Point>(), "z");
    EXPECT_EQ(field_name<3, Point>(), "y");

    EXPECT_EQ(field_names<Empty>().size(), 1U);
    EXPECT_EQ(field_names<Empty>()[0], "PLACEHOLDER");

    EXPECT_EQ(field_names<Many36>().size(), 36U);
    EXPECT_EQ(field_name<0, Many36>(), "f01");
    EXPECT_EQ(field_name<35, Many36>(), "f36");

    EXPECT_EQ(field<0, Point>::name(), "x");
    EXPECT_EQ(field<1, Point>::name(), "c");
    EXPECT_EQ(field<2, Point>::name(), "z");
    EXPECT_EQ(field<3, Point>::name(), "y");
}

TEST_CASE(reflection_static_values) {
    EXPECT_EQ(reflection<Point>::field_count, 4U);
    EXPECT_EQ(reflection<Empty>::field_count, 0U);
    EXPECT_EQ(reflection<Many36>::field_count, 36U);
    EXPECT_EQ(field_count_robustness_check<MoveOnly>(), true);
    EXPECT_EQ(field_count_robustness_check<FromIntOnly>(), true);

    EXPECT_EQ(std::tuple_size_v<point_rvalue_refs>, 4U);
    EXPECT_EQ(std::is_rvalue_reference_v<std::tuple_element_t<0, point_rvalue_refs>>, true);
    EXPECT_EQ(std::is_rvalue_reference_v<std::tuple_element_t<1, point_rvalue_refs>>, true);
    EXPECT_EQ(std::is_rvalue_reference_v<std::tuple_element_t<2, point_rvalue_refs>>, true);
    EXPECT_EQ(std::is_rvalue_reference_v<std::tuple_element_t<3, point_rvalue_refs>>, true);

    EXPECT_EQ(field<0, Point>::index(), 0U);
    EXPECT_EQ(field<1, Point>::index(), 1U);
    EXPECT_EQ(field<2, Point>::index(), 2U);
    EXPECT_EQ(field<3, Point>::index(), 3U);
}

TEST_CASE(field_offset_values) {
    EXPECT_EQ(field_offset<Layout>(0), offsetof(Layout, a));
    EXPECT_EQ(field_offset<Layout>(1), offsetof(Layout, b));
    EXPECT_EQ(field_offset<Layout>(2), offsetof(Layout, bb));
    EXPECT_EQ(field_offset<Layout>(3), offsetof(Layout, c));
    EXPECT_EQ(field_offset(&Layout::a), offsetof(Layout, a));
    EXPECT_EQ(field_offset(&Layout::b), offsetof(Layout, b));
    EXPECT_EQ(field_offset(&Layout::bb), offsetof(Layout, bb));
    EXPECT_EQ(field_offset(&Layout::c), offsetof(Layout, c));

    EXPECT_EQ(field_offset<Packed>(0), offsetof(Packed, a));
    EXPECT_EQ(field_offset<Packed>(1), offsetof(Packed, b));
    EXPECT_EQ(field_offset<Packed>(2), offsetof(Packed, bb));
    EXPECT_EQ(field_offset<Packed>(3), offsetof(Packed, c));
    EXPECT_EQ(field_offset<Packed>(4), offsetof(Packed, d));
    EXPECT_EQ(field_offset<Packed>(5), offsetof(Packed, e));
    EXPECT_EQ(field_offset(&Packed::a), offsetof(Packed, a));
    EXPECT_EQ(field_offset(&Packed::b), offsetof(Packed, b));
    EXPECT_EQ(field_offset(&Packed::bb), offsetof(Packed, bb));
    EXPECT_EQ(field_offset(&Packed::c), offsetof(Packed, c));
    EXPECT_EQ(field_offset(&Packed::d), offsetof(Packed, d));
    EXPECT_EQ(field_offset(&Packed::e), offsetof(Packed, e));
    EXPECT_EQ(field_offset<NoUniqueAddress>(1), offsetof(NoUniqueAddress, e));
    EXPECT_EQ(field_offset(&NoUniqueAddress::e), offsetof(NoUniqueAddress, e));
    EXPECT_EQ(field_offset(&NoDefault::x), offsetof(NoDefault, x));
    EXPECT_EQ(field_offset(&NoDefault::y), offsetof(NoDefault, y));

    EXPECT_EQ(field<0, Point>::offset(), offsetof(Point, x));
    EXPECT_EQ(field<1, Point>::offset(), offsetof(Point, c));
    EXPECT_EQ(field<2, Point>::offset(), offsetof(Point, z));
    EXPECT_EQ(field<3, Point>::offset(), offsetof(Point, y));
}

TEST_CASE(field_refs_lvalue) {
    Point p{.x = 3, .c = 'b', .z = 1.0, .y = 5};
    auto refs = field_refs(p);

    std::get<0>(refs) = 13;
    std::get<3>(refs) = 15;
    EXPECT_EQ(p.x, 13);
    EXPECT_EQ(p.y, 15);
}

TEST_CASE(for_each_void_callback) {
    Point p{.x = 1, .c = 'a', .z = 2.0, .y = 3};

    std::array<std::string_view, 4> names{};
    std::array<std::size_t, 4> offsets{};
    std::size_t visited = 0;

    const bool all_ok = for_each(p, [&](auto f) {
        names[visited] = f.name();
        offsets[visited] = f.offset();
        ++visited;
    });

    EXPECT_TRUE(all_ok);
    EXPECT_EQ(visited, 4U);
    EXPECT_EQ(names[0], "x");
    EXPECT_EQ(names[1], "c");
    EXPECT_EQ(names[2], "z");
    EXPECT_EQ(names[3], "y");
    EXPECT_EQ(offsets[0], offsetof(Point, x));
    EXPECT_EQ(offsets[1], offsetof(Point, c));
    EXPECT_EQ(offsets[2], offsetof(Point, z));
    EXPECT_EQ(offsets[3], offsetof(Point, y));
}

TEST_CASE(for_each_bool_short_circuit) {
    Point p{.x = 1, .c = 'a', .z = 2.0, .y = 3};

    std::size_t visited = 0;
    const bool all_ok = for_each(p, [&](auto f) {
        ++visited;
        return f.index() < 2;
    });

    EXPECT_FALSE(all_ok);
    EXPECT_EQ(visited, 3U);
}

TEST_CASE(for_each_bool_all_true) {
    Point p{.x = 1, .c = 'a', .z = 2.0, .y = 3};

    std::size_t visited = 0;
    const bool all_ok = for_each(p, [&](auto f) {
        ++visited;
        return f.index() <= 3;
    });

    EXPECT_TRUE(all_ok);
    EXPECT_EQ(visited, 4U);
}

TEST_CASE(for_each_int_return_short_circuit) {
    Point p{.x = 1, .c = 'a', .z = 2.0, .y = 3};

    std::size_t visited = 0;
    const bool all_ok = for_each(p, [&](auto f) {
        ++visited;
        return f.index() == 0 ? 1 : 0;
    });

    EXPECT_FALSE(all_ok);
    EXPECT_EQ(visited, 2U);
}

TEST_CASE(for_each_mutation_void_return) {
    Point p{.x = 1, .c = 'a', .z = 2.0, .y = 3};

    const bool all_ok = for_each(p, [](auto f) {
        using value_t = std::remove_cvref_t<decltype(f.value())>;
        if constexpr(std::is_same_v<value_t, int>) {
            f.value() *= 10;
        }
    });

    EXPECT_TRUE(all_ok);
    EXPECT_EQ(p.x, 10);
    EXPECT_EQ(p.y, 30);
}

TEST_CASE(for_each_empty_object) {
    Empty e{};
    std::size_t visited = 0;
    const bool all_ok = for_each(e, [&](auto) {
        ++visited;
        return true;
    });

    EXPECT_TRUE(all_ok);
    EXPECT_EQ(visited, 0U);
}

TEST_CASE(for_each_many36) {
    Many36 v{
        .f01 = 1,
        .f02 = 2,
        .f03 = 3,
        .f04 = 4,
        .f05 = 5,
        .f06 = 6,
        .f07 = 7,
        .f08 = 8,
        .f09 = 9,
        .f10 = 10,
        .f11 = 11,
        .f12 = 12,
        .f13 = 13,
        .f14 = 14,
        .f15 = 15,
        .f16 = 16,
        .f17 = 17,
        .f18 = 18,
        .f19 = 19,
        .f20 = 20,
        .f21 = 21,
        .f22 = 22,
        .f23 = 23,
        .f24 = 24,
        .f25 = 25,
        .f26 = 26,
        .f27 = 27,
        .f28 = 28,
        .f29 = 29,
        .f30 = 30,
        .f31 = 31,
        .f32 = 32,
        .f33 = 33,
        .f34 = 34,
        .f35 = 35,
        .f36 = 36,
    };

    std::size_t visited = 0;
    int sum = 0;
    const bool all_ok = for_each(v, [&](auto f) {
        using value_t = std::remove_cvref_t<decltype(f.value())>;
        if constexpr(std::is_same_v<value_t, int>) {
            sum += f.value();
        }
        ++visited;
    });

    EXPECT_TRUE(all_ok);
    EXPECT_EQ(visited, 36U);
    EXPECT_EQ(sum, 666);
}

};  // TEST_SUITE(reflection)

}  // namespace

}  // namespace kota::meta
