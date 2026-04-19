#pragma once

// rename_all Config fixtures — one per built-in policy plus identity.

#include <string>

#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::meta::fixtures {

struct CamelConfig {
    using field_rename = rename_policy::lower_camel;
};

struct PascalConfig {
    using field_rename = rename_policy::upper_camel;
};

struct UpperSnakeConfig {
    using field_rename = rename_policy::upper_snake;
};

struct LowerSnakeConfig {
    using field_rename = rename_policy::lower_snake;
};

struct IdentityConfig {
    using field_rename = rename_policy::identity;
};

struct RenameAllTarget {
    int user_name;
    float total_score;
    std::string item_id;
};

struct MixedRenameStruct {
    annotation<int, attrs::rename<"ID">> user_id;
    float total_score;
    std::string item_name;
};

struct AliasRenameAllStruct {
    annotation<int, attrs::alias<"user_id">> id;
    float total_score;
};

}  // namespace kota::meta::fixtures
