#pragma once

// Fixtures several files of the JSON schema tests use.

#include <cstdint>
#include <string>
#include <variant>

#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/attrs.h"
#include "kotatsu/codec/macro.h"

namespace kotatsu::test {

enum class color_i8 : std::int8_t { red = 0, green = 1, blue = 2 };

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

KANNOTATION(root_external_annotation, tagged = true, tag_names = {"integer", "text"});
using root_external_variant =
    meta::annotate<root_external_annotation>::type<std::variant<std::int32_t, std::string>>;

}  // namespace kotatsu::test
