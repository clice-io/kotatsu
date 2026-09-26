#include <cstdint>

#include "kota/zest/zest.h"
#include "kota/meta/enum.h"

namespace kota::meta {

namespace {

enum class Color {
    Red,
    Green,
    Blue,
};

enum Plain {
    Zero,
    One,
    Two,
};

enum class Tiny : std::uint8_t {
    A,
    B,
    C,
};

enum class Sparse : int {
    Neg = -12,
    Mid = 7,
    High = 15,
};

enum class Edge8 : std::int8_t {
    Min = -128,
    Max = 127,
};

static_assert(reflection<Color>::member_count == 3);
static_assert(reflection<Color>::member_names.size() == 3);
static_assert(reflection<Color>::member_names[0] == "Red");
static_assert(reflection<Color>::member_names[1] == "Green");
static_assert(reflection<Color>::member_names[2] == "Blue");

static_assert(reflection<Plain>::member_count == 3);
static_assert(reflection<Plain>::member_names.size() == 3);
static_assert(reflection<Plain>::member_names[0] == "Zero");
static_assert(reflection<Plain>::member_names[1] == "One");
static_assert(reflection<Plain>::member_names[2] == "Two");

static_assert(reflection<Tiny>::member_count == 3);
static_assert(reflection<Tiny>::member_names.size() == 3);
static_assert(reflection<Tiny>::member_names[0] == "A");
static_assert(reflection<Tiny>::member_names[1] == "B");
static_assert(reflection<Tiny>::member_names[2] == "C");

static_assert(reflection<Sparse>::member_count == 3);
static_assert(reflection<Sparse>::member_values[0] == Sparse::Neg);
static_assert(reflection<Sparse>::member_values[1] == Sparse::Mid);
static_assert(reflection<Sparse>::member_values[2] == Sparse::High);
static_assert(reflection<Sparse>::member_names[0] == "Neg");
static_assert(reflection<Sparse>::member_names[1] == "Mid");
static_assert(reflection<Sparse>::member_names[2] == "High");

static_assert(reflection<Edge8>::member_count == 2);
static_assert(reflection<Edge8>::member_values[0] == Edge8::Min);
static_assert(reflection<Edge8>::member_values[1] == Edge8::Max);
static_assert(reflection<Edge8>::member_names[0] == "Min");
static_assert(reflection<Edge8>::member_names[1] == "Max");

ZEST_SUITE(reflection){

    ZEST_CASE(enum_member_names){EXPECT(enum_name(Color::Red) == "Red");
EXPECT(enum_name(Color::Green) == "Green");
EXPECT(enum_name(Color::Blue) == "Blue");

EXPECT(enum_name(Plain::Zero) == "Zero");
EXPECT(enum_name(Plain::One) == "One");
EXPECT(enum_name(Plain::Two) == "Two");

EXPECT(enum_name(Tiny::A) == "A");
EXPECT(enum_name(Tiny::B) == "B");
EXPECT(enum_name(Tiny::C) == "C");

EXPECT(enum_name(Sparse::Neg) == "Neg");
EXPECT(enum_name(Sparse::Mid) == "Mid");
EXPECT(enum_name(Sparse::High) == "High");
EXPECT(enum_name(static_cast<Sparse>(42)) == "");
EXPECT(enum_name(static_cast<Sparse>(42), "Unknown") == "Unknown");

EXPECT(enum_name(Edge8::Min) == "Min");
EXPECT(enum_name(Edge8::Max) == "Max");

}  // namespace

};  // namespace kota::meta

}  // namespace

}  // namespace kota::meta
