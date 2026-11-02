#pragma once

#include "kotatsu/codec/bincode/decode.h"
#include "kotatsu/codec/bincode/encode.h"
#include "kotatsu/codec/bincode/type.h"
#include "kotatsu/codec/macro.h"
#include "kotatsu/codec/repr.h"

namespace kotatsu::codec {

template <typename Format>
struct serialize_visit<bincode::Writer<Format>, raw_value> {
    static bool visit(bincode::Writer<Format>& vis, const raw_value& value) {
        return vis.visit_str(value.data);
    }
};

template <typename Format>
struct deserialize_visit<bincode::Reader<Format>, raw_value> {
    static bool visit(bincode::Reader<Format>& vis, raw_value& value) {
        return vis.visit_str(value.data);
    }
};

}  // namespace kotatsu::codec
