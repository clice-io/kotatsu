#pragma once

// Config fixtures for the codec's own knobs. The field_rename configs meta
// reads live in tests/fixtures/configs.h.

#include "kota/support/naming.h"
#include "kota/codec/visit/config.h"

namespace kota::test {

struct EnumStringConfig {
    constexpr static auto enum_repr = codec::enum_repr::String;
};

struct EnumRenameConfig {
    constexpr static auto enum_repr = codec::enum_repr::String;
    using enum_rename = naming::rename_policy::lower_camel;
};

struct NanNullConfig {
    constexpr static auto nan_repr = codec::nan_repr::Null;
};

struct NanStringConfig {
    constexpr static auto nan_repr = codec::nan_repr::String;
};

struct NanErrorConfig {
    constexpr static auto nan_repr = codec::nan_repr::Error;
};

struct StrictConfig {
    constexpr static bool deny_unknown_fields = true;
};

struct DefaultedConfig {
    constexpr static bool defaulted_fields = true;
};

struct NoPathConfig {
    constexpr static bool detailed_error = false;
};

struct NotHumanReadableConfig {
    constexpr static bool human_readable = false;
};

}  // namespace kota::test
