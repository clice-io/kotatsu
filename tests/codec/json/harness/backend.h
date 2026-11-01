#pragma once

#include <string>
#include <string_view>

#include "codec/harness/visit/kit.h"
#include "kotatsu/codec/json/json.h"

namespace kotatsu::test {

/// The json backend for the protocol kit.
struct Json {
    constexpr static std::string_view name = "json";
    /// Not non_finite: JSON has no number for NaN or the infinities.
    constexpr static Caps caps{
        .self_describing = true,
        .absent_fields = true,
        .full_uint64 = true,
        .nested_nulls = true,
        .untrusted_input = true,
        .format_tag = true,
        .utf8_text = true,
    };
    using Encoded = std::string;

    template <typename Settings = void, typename T>
    static auto encode(const T& value) {
        codec::json::encode_options options;
        static_cast<codec::encode_options&>(options) = kit_encode_options<Settings>();
        return codec::json::to_string<kit_format_t<codec::json::format, Settings>>(value, options);
    }

    template <typename Settings = void, typename T>
    static auto decode(const Encoded& text, T& out) {
        return codec::json::from_string<kit_format_t<codec::json::format, Settings>>(
            text,
            out,
            kit_decode_options<Settings>());
    }

    /// Indented; text that does not parse, which only a broken encoder
    /// writes, is shown as it is.
    static std::string render(const Encoded& text) {
        auto pretty = codec::json::prettify(text);
        return pretty ? *pretty : text;
    }
};

}  // namespace kotatsu::test
