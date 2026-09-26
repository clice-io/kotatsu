#pragma once

#include <cassert>
#include <string>
#include <string_view>

#include "codec/harness/visit/kit.h"
#include "kota/codec/json/json.h"

namespace kota::test {

/// The json backend for the protocol kit.
struct Json {
    constexpr static std::string_view name = "json";
    /// Not non_finite: JSON has no literal for NaN or the infinities, so the
    /// writer emits null for them even under nan_repr::Passthrough. Not
    /// struct_keys: map keys are object keys, and MapKeyWriter writes only
    /// strings and integers.
    constexpr static Caps caps{
        .self_describing = true,
        .absent_fields = true,
        .full_uint64 = true,
        .null_elements = true,
        .string_knobs = true,
        .dynamic_repr = true,
        .untrusted_input = true,
    };
    using format = codec::json::format;
    using Encoded = std::string;

    template <typename Config = void, typename T>
    static auto encode(const T& value) {
        return codec::json::to_string<Config>(value);
    }

    template <typename Config = void, typename T>
    static auto decode(const Encoded& text, T& out) {
        return codec::json::from_string<Config>(text, out);
    }

    static std::string render(const Encoded& text) {
        auto pretty = codec::json::prettify(text);
        assert(pretty && "the encoder's output always parses");
        return *pretty;
    }
};

}  // namespace kota::test
