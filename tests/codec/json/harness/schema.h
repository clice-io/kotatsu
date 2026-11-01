#pragma once

// Fixtures several files of the JSON schema tests use.

#include <cstdint>
#include <string>
#include <variant>

#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/macro.h"
#include "kotatsu/codec/repr.h"

namespace kotatsu::test {

enum class color_i8 : std::int8_t { red = 0, green = 1, blue = 2 };

/// color_i8, travelling as its enumerators' names (an enum_names repr).
enum class named_color : std::int8_t { red, green, blue };

enum class shouting_color : std::int8_t;
KANNOTATE_TYPE(shouting_color, rename_all = Casing::UpperSnake);

/// named_color, its names spelled by the rename_all of its own annotation.
enum class shouting_color : std::int8_t { red, green, blue };

struct point2d {
    std::int32_t x;
    std::int32_t y;
};

struct inner {
    std::int32_t a;
};

struct with_enum {
    color_i8 c;
    std::string name;
};

struct casing_child {
    std::int32_t first_value;
};

KANNOTATE_TYPE(struct root_external_variant, tagged = true, tag_names = {"integer", "text"});

struct root_external_variant : std::variant<std::int32_t, std::string> {
    using variant::variant;
};

}  // namespace kotatsu::test

template <>
struct kotatsu::codec::repr<kotatsu::test::named_color> : codec::enum_names<> {};

template <>
struct kotatsu::codec::repr<kotatsu::test::shouting_color> : codec::enum_names<> {};
