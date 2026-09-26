#pragma once

// Plain struct fixtures: no attributes, only fields.

#include <compare>
#include <cstdint>
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

struct Point2d {
    double x;
    double y;
};

struct Color3 {
    std::int32_t r;
    std::int32_t g;
    std::int32_t b;
};

struct Circle {
    double radius;
};

struct Rect {
    double width;
    double height;
};

struct Triangle {
    double base;
    double height;
};

struct BoolInt {
    bool is_valid;
    std::int32_t i32;
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

struct WithScores {
    std::string name;
    std::vector<std::int32_t> scores;
};

struct StrictIdName {
    std::int32_t id;
    std::string name;
};

struct SimpleStruct {
    int x;
    std::string name;
    float score;
};

struct NestedStruct {
    std::vector<SimpleStruct> items;
};

}  // namespace kota::test
