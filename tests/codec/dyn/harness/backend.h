#pragma once

#include <cassert>
#include <string>
#include <string_view>

#include "codec/harness/visit/kit.h"
#include "kotatsu/codec/debug/debug.h"
#include "kotatsu/codec/dyn/dyn.h"

namespace kotatsu::test {

/// The dyn backend for the protocol kit. Its document is the tree itself.
struct Dyn {
    constexpr static std::string_view name = "dyn";
    /// non_finite: the tree holds a double as it is. Not
    /// untrusted_input: a tree is built in memory,
    /// by the program or by a reader that has checked its own input, so
    /// there is no text or byte sequence to garble. Not format_tag: dyn has
    /// no format of its own; the kit runs it under codec::format, whose reprs
    /// are the ones scoped to no backend.
    constexpr static Caps caps{
        .self_describing = true,
        .absent_fields = true,
        .full_uint64 = true,
        .nested_nulls = true,
        .non_finite = true,
    };
    using Encoded = codec::dyn::Value;

    template <typename Settings = void, typename T>
    static auto encode(const T& value) {
        return codec::dyn::to_dyn<kit_format_t<codec::format, Settings>>(
            value,
            kit_encode_options<Settings>());
    }

    template <typename Settings = void, typename T>
    static auto decode(const Encoded& tree, T& out) {
        return codec::dyn::from_dyn<kit_format_t<codec::format, Settings>>(
            tree,
            out,
            kit_decode_options<Settings>());
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

}  // namespace kotatsu::test
