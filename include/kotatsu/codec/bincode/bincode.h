#pragma once

#include "kotatsu/codec/bincode/decode.h"
#include "kotatsu/codec/bincode/encode.h"
#include "kotatsu/codec/bincode/type.h"
#include "kotatsu/codec/macro.h"
#include "kotatsu/codec/visit/common.h"

namespace kotatsu::codec {

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

}  // namespace kotatsu::codec
