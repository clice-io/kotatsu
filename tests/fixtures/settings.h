#pragma once

// Settings fixtures: one rename_all per built-in policy, plus identity. Read
// by type_info::of as the knobs of a format, and by the kit through
// kit_format_t.

#include "kotatsu/support/naming.h"

namespace kotatsu::test {

struct CamelSettings {
    using rename_all = naming::rename_policy::lower_camel;
};

struct PascalSettings {
    using rename_all = naming::rename_policy::upper_camel;
};

struct UpperSnakeSettings {
    using rename_all = naming::rename_policy::upper_snake;
};

struct LowerSnakeSettings {
    using rename_all = naming::rename_policy::lower_snake;
};

struct IdentitySettings {
    using rename_all = naming::rename_policy::identity;
};

}  // namespace kotatsu::test
