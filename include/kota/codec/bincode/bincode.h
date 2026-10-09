#pragma once

#include "kota/codec/bincode/decode.h"
#include "kota/codec/bincode/encode.h"
#include "kota/codec/bincode/type.h"
#include "kota/codec/macro.h"
#include "kota/codec/visit/common.h"

namespace kota::codec {

template <typename Config>
struct serialize_visit<bincode::Writer, RawValue, Config> {
    static bool visit(bincode::Writer& vis, const RawValue& value) {
        return vis.visit_str(value.data);
    }
};

template <typename Config>
struct deserialize_visit<bincode::Reader, RawValue, Config> {
    static bool visit(bincode::Reader& vis, RawValue& value) {
        return vis.visit_str(value.data);
    }
};

}  // namespace kota::codec
