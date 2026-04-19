#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kota::meta::fixtures {

struct Point2i {
    std::int32_t x;
    std::int32_t y;
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

struct Triangle {
    double base;
    double height;
};

struct IntHolder {
    std::int32_t value;
};

struct StringHolder {
    std::string value;
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

}  // namespace kota::meta::fixtures
