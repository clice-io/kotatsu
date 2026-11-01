#pragma once

// Config fixtures for the codec's own knobs. The field_rename configs meta
// reads live in tests/fixtures/configs.h.

#include <optional>

#include "kotatsu/support/naming.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/format.h"

namespace kotatsu::test {

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

struct AllOptionalConfig {
    constexpr static std::optional<codec::presence_kind> presence_all =
        codec::presence_kind::optional;
};

struct ReplaceUTF8Config {
    constexpr static auto invalid_utf8 = codec::invalid_utf8::Replace;
};

struct OmitUnstatedConfig {
    constexpr static bool omit_unstated_defaults = true;
};

struct NoPathConfig {
    constexpr static bool detailed_error = false;
};

struct NotHumanReadableConfig {
    constexpr static bool human_readable = false;
};

}  // namespace kotatsu::test
