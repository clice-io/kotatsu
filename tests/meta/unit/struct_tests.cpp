#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/struct.h"

namespace kotatsu::meta {

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

struct Overlapping {
    int i;
    char c;
#ifdef _MSC_VER
    [[msvc::no_unique_address]] Empty e;
#else
    [[no_unique_address]] Empty e;
#endif
};

/// A string as a template argument, which a compiler prints as a literal.
template <std::size_t N>
struct Text {
    char value[N];

    consteval Text(const char (&text)[N]) {
        for(std::size_t i = 0; i < N; ++i) {
            value[i] = text[i];
        }
    }
};

template <Text Label>
struct Labeled {
    int value;
};

/// The printed name of each field's type holds what would end a name or a list.
struct Quoted {
    Labeled<"it's (a) <b>, c"> first;
    int second;
};

struct WithReference {
    int& ref;
    int value;
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

    return field_count<S4> == 4 && field_count<S5> == 5 && field_count<S6> == 6;
}

ZEST_SUITE(meta_struct) {

ZEST_CASE(field_of_is_the_field) {
    Point p{.x = 1, .c = 'A', .z = 2.5, .y = 4};

    ZEXPECT(&field_of<0>(p) == &p.x);
    ZEXPECT(&field_of<1>(p) == &p.c);
    ZEXPECT(&field_of<2>(p) == &p.z);
    ZEXPECT(&field_of<3>(p) == &p.y);

    field_of<0>(p) = 11;
    field_of<3>(p) = 44;
    ZEXPECT(p.x == 11);
    ZEXPECT(p.y == 44);

    const Point& view = p;
    ZSTATIC_EXPECT(std::is_same_v<decltype(field_of<1>(view)), const char&>);
}

ZEST_CASE(field_name_values) {
    ZEXPECT(field_names<Point>.size() == 4U);
    ZEXPECT(field_names<Point>[0] == "x");
    ZEXPECT(field_names<Point>[1] == "c");
    ZEXPECT(field_names<Point>[2] == "z");
    ZEXPECT(field_names<Point>[3] == "y");

    ZEXPECT(field_names<Empty>.empty());

    ZEXPECT(field_names<Many36>.size() == 36U);
    ZEXPECT(field_names<Many36>[0] == "f01");
    ZEXPECT(field_names<Many36>[35] == "f36");
}

ZEST_CASE(field_count_counts_the_fields) {
    ZSTATIC_EXPECT(field_count<Point> == 4U);
    ZSTATIC_EXPECT(field_count<const Point> == 4U);
    ZSTATIC_EXPECT(field_count<Empty> == 0U);
    ZSTATIC_EXPECT(field_count<Many36> == 36U);
    ZSTATIC_EXPECT(field_count_robustness_check<MoveOnly>());
    ZSTATIC_EXPECT(field_count_robustness_check<FromIntOnly>());
}

ZEST_CASE(reflectable_class_takes_aggregate_classes) {
    ZSTATIC_EXPECT(reflectable_class<Point>);
    ZSTATIC_EXPECT(reflectable_class<Empty>);
    ZSTATIC_EXPECT(!reflectable_class<NoDefault>);
    ZSTATIC_EXPECT(!reflectable_class<int>);
    ZSTATIC_EXPECT(!reflectable_class<int[2]>);
}

ZEST_CASE(field_offset_values) {
    ZEXPECT(field_offsets<Layout>[0] == offsetof(Layout, a));
    ZEXPECT(field_offsets<Layout>[1] == offsetof(Layout, b));
    ZEXPECT(field_offsets<Layout>[2] == offsetof(Layout, bb));
    ZEXPECT(field_offsets<Layout>[3] == offsetof(Layout, c));
    ZEXPECT(field_offset(&Layout::a) == offsetof(Layout, a));
    ZEXPECT(field_offset(&Layout::b) == offsetof(Layout, b));
    ZEXPECT(field_offset(&Layout::bb) == offsetof(Layout, bb));
    ZEXPECT(field_offset(&Layout::c) == offsetof(Layout, c));

    ZEXPECT(field_offsets<Packed>[0] == offsetof(Packed, a));
    ZEXPECT(field_offsets<Packed>[1] == offsetof(Packed, b));
    ZEXPECT(field_offsets<Packed>[2] == offsetof(Packed, bb));
    ZEXPECT(field_offsets<Packed>[3] == offsetof(Packed, c));
    ZEXPECT(field_offsets<Packed>[4] == offsetof(Packed, d));
    ZEXPECT(field_offsets<Packed>[5] == offsetof(Packed, e));
    ZEXPECT(field_offset(&Packed::a) == offsetof(Packed, a));
    ZEXPECT(field_offset(&Packed::b) == offsetof(Packed, b));
    ZEXPECT(field_offset(&Packed::bb) == offsetof(Packed, bb));
    ZEXPECT(field_offset(&Packed::c) == offsetof(Packed, c));
    ZEXPECT(field_offset(&Packed::d) == offsetof(Packed, d));
    ZEXPECT(field_offset(&Packed::e) == offsetof(Packed, e));
    ZEXPECT(field_offsets<NoUniqueAddress>[1] == offsetof(NoUniqueAddress, e));
    ZEXPECT(field_offset(&NoUniqueAddress::e) == offsetof(NoUniqueAddress, e));
    ZEXPECT(field_offset(&NoDefault::x) == offsetof(NoDefault, x));
    ZEXPECT(field_offset(&NoDefault::y) == offsetof(NoDefault, y));
}

ZEST_CASE(apply_fields_hands_every_field_at_once) {
    Point p{.x = 3, .c = 'b', .z = 1.0, .y = 5};
    const auto sum = apply_fields(p, [](int& x, char& c, double& z, int& y) {
        x = 4;
        return x + c + static_cast<int>(z) + y;
    });
    ZEXPECT(sum == 4 + 'b' + 1 + 5);
    ZEXPECT(p.x == 4);

    const Point& view = p;
    ZEXPECT(apply_fields(view, []<typename... Fields>(const Fields&...) {
                return sizeof...(Fields);
            }) == 4U);
    Empty empty;
    ZEXPECT(apply_fields(empty, [] { return 0; }) == 0);
}

ZEST_CASE(apply_fields_hands_a_const_object_const_fields) {
    constexpr Point point{.x = 1, .c = 'a', .z = 2.0, .y = 3};
    ZSTATIC_EXPECT(apply_fields(point, [](auto&... fields) {
        return (std::is_const_v<std::remove_reference_t<decltype(fields)>> && ...);
    }));
    ZSTATIC_EXPECT(apply_fields(point, [](const auto&... fields) { return (0 + ... + fields); }) ==
                   1 + 'a' + 2.0 + 3);
}

ZEST_CASE(field_names_past_quotes_in_type_names) {
    ZEXPECT(field_names<Quoted>[0] == "first");
    ZEXPECT(field_names<Quoted>[1] == "second");
}

ZEST_CASE(reference_member_leaves_no_field) {
    // A reference has neither an offset nor an address meta could name it by.
    ZSTATIC_EXPECT(field_count<WithReference> == 0U);
}

ZEST_CASE(field_type_is_the_declared_type) {
    struct Declared {
        const int fixed;
        double value;
    };

    ZEXPECT(zest::type_eq<field_type<Declared, 0>, const int>());
    ZEXPECT(zest::type_eq<field_type<Declared, 1>, double>());
}

ZEST_CASE(field_offsets_lists_every_offset) {
    constexpr std::array<std::size_t, 6> expected = {
        offsetof(Packed, a),
        offsetof(Packed, b),
        offsetof(Packed, bb),
        offsetof(Packed, c),
        offsetof(Packed, d),
        offsetof(Packed, e),
    };
    ZEXPECT(field_offsets<Packed> == expected);
}

ZEST_CASE(field_offset_of_a_member_placed_below_the_one_before) {
    // e, empty and without an address of its own, sits below c where the layout overlaps it
    // with i (the Itanium ABI's), and the search for its offset starts over.
    ZEXPECT(field_offsets<Overlapping>[0] == offsetof(Overlapping, i));
    ZEXPECT(field_offsets<Overlapping>[1] == offsetof(Overlapping, c));
    ZEXPECT(field_offsets<Overlapping>[2] == offsetof(Overlapping, e));
}

ZEST_CASE(apply_fields_visits_many_fields) {
    Many36 v{};
    const int sum = apply_fields(v, [](int&... fields) {
        int next = 0;
        ((fields = ++next), ...);
        return (0 + ... + fields);
    });
    ZEXPECT(sum == 666);
    ZEXPECT(v.f01 == 1);
    ZEXPECT(v.f36 == 36);
}

};  // ZEST_SUITE(meta_struct)

}  // namespace

}  // namespace kotatsu::meta
