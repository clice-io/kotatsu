#pragma once

// Plain struct fixtures: no attributes, only fields.

#include <string>
#include <vector>

namespace kota::test {

struct Circle {
    double radius;

    auto operator==(const Circle&) const -> bool = default;
};

struct Rect {
    double width;
    double height;

    auto operator==(const Rect&) const -> bool = default;
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
