#pragma once

// Config fixtures that meta reads: one field_rename per built-in policy, plus
// identity.

#include "kota/support/naming.h"

namespace kota::test {

struct CamelConfig {
    using field_rename = naming::rename_policy::lower_camel;
};

struct PascalConfig {
    using field_rename = naming::rename_policy::upper_camel;
};

struct UpperSnakeConfig {
    using field_rename = naming::rename_policy::upper_snake;
};

struct LowerSnakeConfig {
    using field_rename = naming::rename_policy::lower_snake;
};

struct IdentityConfig {
    using field_rename = naming::rename_policy::identity;
};

}  // namespace kota::test
