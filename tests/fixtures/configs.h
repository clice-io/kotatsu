#pragma once

// Settings fixtures: one rename_all per built-in policy, plus identity. Read
// by type_info_of as the knobs of a format, and by the kit through
// kit_format_t.

#include "kotatsu/support/naming.h"

namespace kotatsu::test {

struct CamelConfig {
    using rename_all = naming::rename_policy::lower_camel;
};

struct PascalConfig {
    using rename_all = naming::rename_policy::upper_camel;
};

struct UpperSnakeConfig {
    using rename_all = naming::rename_policy::upper_snake;
};

struct LowerSnakeConfig {
    using rename_all = naming::rename_policy::lower_snake;
};

struct IdentityConfig {
    using rename_all = naming::rename_policy::identity;
};

}  // namespace kotatsu::test
