#pragma once

// Plain struct fixtures only the codec's tests use: no attributes, only
// fields.

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kota::test {

/// Puts a value in a field position.
template <typename T>
struct Field {
    T value;
};

struct Empty {};

struct Point {
    std::int32_t x;
    std::int32_t y;

    auto operator<=>(const Point&) const = default;
};

/// Constructible only from a value: no value to decode into.
struct NoDefault {
    explicit NoDefault(int number) : number(number) {}

    int number;
};

/// Point with a key Point does not have.
struct PointWithExtra {
    std::int32_t x;
    std::int32_t y;
    std::int32_t extra;
};

struct Point2d {
    double x;
    double y;

    auto operator==(const Point2d&) const -> bool = default;
};

struct Color3 {
    std::int32_t r;
    std::int32_t g;
    std::int32_t b;

    auto operator==(const Color3&) const -> bool = default;
};

struct Segment {
    int line_width;

    auto operator==(const Segment&) const -> bool = default;
};

/// Structs whose required fields overlap, for untagged probing.
struct OneKey {
    int a;
};

struct TwoKeys {
    int a;
    int b;
};

struct OneKeyMaybeTwo {
    int a;
    std::optional<int> b;
};

struct Address {
    std::string city;
    std::int32_t zip;
};

struct Person {
    std::string name;
    std::int32_t age;
    Address addr;
};

struct PersonWithScores {
    std::int32_t id;
    std::string name;
    std::vector<std::int32_t> scores;
    bool active;
};

struct StrictIdName {
    std::int32_t id;
    std::string name;
};

}  // namespace kota::test
