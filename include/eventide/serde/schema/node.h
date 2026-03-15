#pragma once

#include <string>
#include <vector>

#include "eventide/serde/schema/kind.h"

namespace eventide::serde::schema {

/// Runtime DOM node for schema-based variant matching.
/// Backends convert their format-specific DOM to this type.
/// Uses type_hint bitmask to allow ambiguous types (e.g., number = integer|floating).
struct schema_node {
    serde::type_hint hints = serde::type_hint::any;

    struct field {
        std::string key;
        serde::type_hint value_hints = serde::type_hint::any;
    };
    std::vector<field> fields;
};

}  // namespace eventide::serde::schema
