#include <cstdint>

#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/enum.h"

namespace kotatsu::meta {

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

/// Members on both sides of 128: the scan reaches the high one as -56.
enum class High8 : std::uint8_t {
    Low = 1,
    High = 200,
};

/// The ends of both halves of the scan of an unsigned type: 0..127, then the
/// top 128 values, reached as -128..-1.
enum class EdgeU32 : std::uint32_t {
    Zero = 0,
    Top = 127,
    Bottom = 0xFFFF'FF80,
    Max = 0xFFFF'FFFF,
};

/// A high byte, negative where char is signed and above 127 where it is not (Linux on ARM).
enum class Byte : char {
    Letter = 'a',
    High = '\xE9',
};

enum class Toggle : bool {
    Off,
    On,
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

ZEST_SUITE(meta_enum) {

ZEST_CASE(enum_member_names) {
    ZEXPECT(enum_name(Color::Red) == "Red");
    ZEXPECT(enum_name(Color::Green) == "Green");
    ZEXPECT(enum_name(Color::Blue) == "Blue");

    ZEXPECT(enum_name(Plain::Zero) == "Zero");
    ZEXPECT(enum_name(Plain::One) == "One");
    ZEXPECT(enum_name(Plain::Two) == "Two");

    ZEXPECT(enum_name(Tiny::A) == "A");
    ZEXPECT(enum_name(Tiny::B) == "B");
    ZEXPECT(enum_name(Tiny::C) == "C");

    ZEXPECT(enum_name(Sparse::Neg) == "Neg");
    ZEXPECT(enum_name(Sparse::Mid) == "Mid");
    ZEXPECT(enum_name(Sparse::High) == "High");
    ZEXPECT(enum_name(static_cast<Sparse>(42)) == "");
    ZEXPECT(enum_name(static_cast<Sparse>(42), "Unknown") == "Unknown");

    ZEXPECT(enum_name(Edge8::Min) == "Min");
    ZEXPECT(enum_name(Edge8::Max) == "Max");
}

ZEST_CASE(unsigned_members_above_127_ascend) {
    ZSTATIC_EXPECT(reflection<High8>::member_count == 2);
    ZSTATIC_EXPECT(reflection<High8>::member_values[0] == High8::Low);
    ZSTATIC_EXPECT(reflection<High8>::member_values[1] == High8::High);
    ZEXPECT(enum_name(High8::Low) == "Low");
    ZEXPECT(enum_name(High8::High) == "High");

    ZSTATIC_EXPECT(reflection<EdgeU32>::member_count == 4);
    ZSTATIC_EXPECT(reflection<EdgeU32>::member_values[0] == EdgeU32::Zero);
    ZSTATIC_EXPECT(reflection<EdgeU32>::member_values[1] == EdgeU32::Top);
    ZSTATIC_EXPECT(reflection<EdgeU32>::member_values[2] == EdgeU32::Bottom);
    ZSTATIC_EXPECT(reflection<EdgeU32>::member_values[3] == EdgeU32::Max);
    ZEXPECT(enum_name(EdgeU32::Zero) == "Zero");
    ZEXPECT(enum_name(EdgeU32::Top) == "Top");
    ZEXPECT(enum_name(EdgeU32::Bottom) == "Bottom");
    ZEXPECT(enum_name(EdgeU32::Max) == "Max");

    ZSTATIC_EXPECT(reflection<Byte>::member_count == 2);
    ZSTATIC_EXPECT(reflection<Byte>::member_values[0] < reflection<Byte>::member_values[1]);
    ZEXPECT(enum_name(Byte::Letter) == "Letter");
    ZEXPECT(enum_name(Byte::High) == "High");
}

ZEST_CASE(bool_members_are_named_once) {
    ZSTATIC_EXPECT(reflection<Toggle>::member_count == 2);
    ZEXPECT(enum_name(Toggle::Off) == "Off");
    ZEXPECT(enum_name(Toggle::On) == "On");
}

};  // ZEST_SUITE(meta_enum)

}  // namespace

}  // namespace kotatsu::meta
