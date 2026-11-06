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

/// Members on both sides of where one chunk of probed values ends and the next begins: the
/// scan of -128..127 takes 32 values at a time.
enum class Chunked : int {
    LastOfSecond = -65,
    FirstOfThird = -64,
    LastOfFifth = 31,
    FirstOfSixth = 32,
};

/// Without a fixed underlying type its values are 0..63, which a compiler may take no value
/// outside of as a template argument: it is probed a value at a time.
enum Unfixed {
    First = 0,
    Last = 63,
};

/// Unscoped, with a fixed type: its values print under its namespace, not under its name.
enum Compass : int {
    Up = 85,
    Down = -42,
    Leftmost = -120,
};

/// Its name holds a comma, between its class's template arguments: it is probed a value at a
/// time. Unscoped, as a class template's instantiation instantiates the enumerators of its
/// unscoped enums alone.
template <typename A, typename B>
struct Holder {
    enum Inner : int {
        Left = -3,
        Right = 90,
    };
};

enum class Toggle : bool {
    Off,
    On,
};

static_assert(enum_values<Color>.size() == 3);
static_assert(enum_names<Color>.size() == 3);
static_assert(enum_names<Color>[0] == "Red");
static_assert(enum_names<Color>[1] == "Green");
static_assert(enum_names<Color>[2] == "Blue");

static_assert(enum_values<Plain>.size() == 3);
static_assert(enum_names<Plain>.size() == 3);
static_assert(enum_names<Plain>[0] == "Zero");
static_assert(enum_names<Plain>[1] == "One");
static_assert(enum_names<Plain>[2] == "Two");

static_assert(enum_values<Tiny>.size() == 3);
static_assert(enum_names<Tiny>.size() == 3);
static_assert(enum_names<Tiny>[0] == "A");
static_assert(enum_names<Tiny>[1] == "B");
static_assert(enum_names<Tiny>[2] == "C");

static_assert(enum_values<Sparse>.size() == 3);
static_assert(enum_values<Sparse>[0] == Sparse::Neg);
static_assert(enum_values<Sparse>[1] == Sparse::Mid);
static_assert(enum_values<Sparse>[2] == Sparse::High);
static_assert(enum_names<Sparse>[0] == "Neg");
static_assert(enum_names<Sparse>[1] == "Mid");
static_assert(enum_names<Sparse>[2] == "High");

static_assert(enum_values<Chunked>.size() == 4);
static_assert(enum_names<Chunked>[0] == "LastOfSecond");
static_assert(enum_names<Chunked>[1] == "FirstOfThird");
static_assert(enum_names<Chunked>[2] == "LastOfFifth");
static_assert(enum_names<Chunked>[3] == "FirstOfSixth");
static_assert(enum_values<Chunked>[3] == Chunked::FirstOfSixth);

static_assert(enum_values<Unfixed>.size() == 2);
static_assert(enum_names<Unfixed>[0] == "First");
static_assert(enum_names<Unfixed>[1] == "Last");

static_assert(enum_values<Compass>.size() == 3);
static_assert(enum_names<Compass>[0] == "Leftmost");
static_assert(enum_names<Compass>[1] == "Down");
static_assert(enum_names<Compass>[2] == "Up");

static_assert(enum_values<Holder<int, char>::Inner>.size() == 2);
static_assert(enum_names<Holder<int, char>::Inner>[0] == "Left");
static_assert(enum_names<Holder<int, char>::Inner>[1] == "Right");

static_assert(enum_values<Edge8>.size() == 2);
static_assert(enum_values<Edge8>[0] == Edge8::Min);
static_assert(enum_values<Edge8>[1] == Edge8::Max);
static_assert(enum_names<Edge8>[0] == "Min");
static_assert(enum_names<Edge8>[1] == "Max");

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
    ZSTATIC_EXPECT(enum_values<High8>.size() == 2);
    ZSTATIC_EXPECT(enum_values<High8>[0] == High8::Low);
    ZSTATIC_EXPECT(enum_values<High8>[1] == High8::High);
    ZEXPECT(enum_name(High8::Low) == "Low");
    ZEXPECT(enum_name(High8::High) == "High");

    ZSTATIC_EXPECT(enum_values<EdgeU32>.size() == 4);
    ZSTATIC_EXPECT(enum_values<EdgeU32>[0] == EdgeU32::Zero);
    ZSTATIC_EXPECT(enum_values<EdgeU32>[1] == EdgeU32::Top);
    ZSTATIC_EXPECT(enum_values<EdgeU32>[2] == EdgeU32::Bottom);
    ZSTATIC_EXPECT(enum_values<EdgeU32>[3] == EdgeU32::Max);
    ZEXPECT(enum_name(EdgeU32::Zero) == "Zero");
    ZEXPECT(enum_name(EdgeU32::Top) == "Top");
    ZEXPECT(enum_name(EdgeU32::Bottom) == "Bottom");
    ZEXPECT(enum_name(EdgeU32::Max) == "Max");

    ZSTATIC_EXPECT(enum_values<Byte>.size() == 2);
    ZSTATIC_EXPECT(enum_values<Byte>[0] < enum_values<Byte>[1]);
    ZEXPECT(enum_name(Byte::Letter) == "Letter");
    ZEXPECT(enum_name(Byte::High) == "High");
}

ZEST_CASE(bool_members_are_named_once) {
    ZSTATIC_EXPECT(enum_values<Toggle>.size() == 2);
    ZEXPECT(enum_name(Toggle::Off) == "Off");
    ZEXPECT(enum_name(Toggle::On) == "On");
}

};  // ZEST_SUITE(meta_enum)

}  // namespace

}  // namespace kotatsu::meta
