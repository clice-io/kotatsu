#pragma once

#include "backend.h"
#include "config.h"
#include "kota/support/config.h"
#include "kota/codec/detail/ser_dispatch.h"

namespace kota::codec {

template <serializer_like S, typename V, typename T, typename E>
constexpr auto serialize(S& s, const V& v) -> std::expected<T, E> {
    using Serde = serialize_traits<S, V>;

    if constexpr(requires { Serde::serialize(s, v); }) {
        return Serde::serialize(s, v);
    } else {
        detail::StreamingCtx<S> ctx{s};
        return detail::
            unified_serialize<config::config_of<S>, detail::StreamingCtx<S>, std::tuple<>>(ctx, v);
    }
}

}  // namespace kota::codec
