#pragma once

// Settings fixtures for the codec's own knobs and options, which the kit
// applies through kit_format_t and kit_encode_options / kit_decode_options.
// The rename_all settings live in tests/fixtures/configs.h.

#include <optional>

#include "kotatsu/support/naming.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/format.h"

namespace kotatsu::test {

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

struct NotHumanReadableConfig {
    constexpr static bool human_readable = false;
};

}  // namespace kotatsu::test
