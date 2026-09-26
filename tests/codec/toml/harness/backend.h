#pragma once

#include <string>
#include <string_view>

#include "codec/harness/visit/kit.h"
#include "kota/codec/toml/toml.h"

namespace kota::test {

/// The toml backend for the protocol kit. Its documents are the text
/// to_string writes. A root that is not table-shaped by its declared type
/// travels under the `__value` key, so the kit compares such values in a
/// Field; toml's own tests pin the routing.
struct Toml {
    constexpr static std::string_view name = "toml";
    /// Not full_uint64: TOML integers are signed 64-bit, and the writer's
    /// visit_uint fails above int64's maximum. Not null_elements: TOML has no
    /// null, so the writer omits a null field or map value, which then reads
    /// back as absent, and fails on a null array element. non_finite: TOML has
    /// nan and inf literals, which nan_repr::Passthrough writes. Not
    /// struct_keys: map keys are table keys, and MapKeyWriter writes only
    /// strings and integers. Not untrusted_input, although TOML text does
    /// come from outside the program: toml++ v3.4.0 asserts, and under NDEBUG
    /// assumes the impossible, on a table header whose key starts with a
    /// character no key may start with (`[`a]`), which the hostile sweep
    /// reaches. Set it once toml++ rejects that header.
    constexpr static Caps caps{
        .self_describing = true,
        .absent_fields = true,
        .non_finite = true,
        .string_knobs = true,
        .dynamic_repr = true,
        .format_tag = true,
    };
    using Encoded = std::string;

    template <typename Config = void, typename T>
    static auto encode(const T& value) {
        return codec::toml::to_string<Config>(value);
    }

    template <typename Config = void, typename T>
    static auto decode(const Encoded& text, T& out) {
        return codec::toml::from_string<Config>(text, out);
    }

    /// The text as it is: toml++ already writes one key per line.
    static std::string render(const Encoded& text) {
        return text;
    }
};

}  // namespace kota::test
