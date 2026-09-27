#pragma once

#include <cassert>
#include <string>
#include <string_view>

#include "codec/harness/visit/kit.h"
#include "kota/codec/debug/debug.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::test {

/// The dyn backend for the protocol kit. Its document is the tree itself.
struct Dyn {
    constexpr static std::string_view name = "dyn";
    /// Not non_finite: the writer stores null for NaN and the infinities
    /// even under nan_repr::Passthrough, as json writes them. Not
    /// untrusted_input: a tree is built in memory,
    /// by the program or by a reader that has checked its own input, so
    /// there is no text or byte sequence to garble. Not format_tag: the tree
    /// is the interchange between formats, so only format-agnostic reprs
    /// apply to it.
    constexpr static Caps caps{
        .self_describing = true,
        .absent_fields = true,
        .full_uint64 = true,
        .nested_nulls = true,
    };
    using Encoded = codec::dyn::Value;

    template <typename Config = void, typename T>
    static auto encode(const T& value) {
        return codec::dyn::to_dyn<Config>(value);
    }

    template <typename Config = void, typename T>
    static auto decode(const Encoded& tree, T& out) {
        return codec::dyn::from_dyn<Config>(tree, out);
    }

    /// The debug codec's indented text, which every tree has. It shows no
    /// integer's signedness and no point in a whole float; codec_dyn_encode
    /// pins the kinds.
    static std::string render(const Encoded& tree) {
        auto text = codec::debug::to_string(tree, true);
        assert(text);
        return *text;
    }
};

}  // namespace kota::test
