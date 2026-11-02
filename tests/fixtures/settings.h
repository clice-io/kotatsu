#pragma once

// Settings fixtures the kit makes a format of through kit_format_t.

#include "kotatsu/support/naming.h"

namespace kotatsu::test {

struct CamelSettings {
    using rename_all = naming::rename_policy::lower_camel;
};

}  // namespace kotatsu::test
