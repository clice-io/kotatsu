#pragma once

#include <string>
#include <string_view>

#include "codec/harness/visit/kit.h"
#include "kota/codec/json/json.h"

namespace kota::test {

/// The json backend for the protocol kit.
struct Json {
    constexpr static std::string_view name = "json";
    /// Not non_finite: JSON has no literal for NaN or the infinities, so the
    /// writer emits null for them even under nan_repr::Passthrough.
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

    template <typename Config = void, typename T>
    static auto encode(const T& value) {
        return codec::json::to_string<Config>(value);
    }

    template <typename Config = void, typename T>
    static auto decode(const Encoded& text, T& out) {
        return codec::json::from_string<Config>(text, out);
    }

    /// Indented; text that does not parse, which only a broken encoder
    /// writes, is shown as it is.
    static std::string render(const Encoded& text) {
        auto pretty = codec::json::prettify(text);
        return pretty ? *pretty : text;
    }
};

}  // namespace kota::test
