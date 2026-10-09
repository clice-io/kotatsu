#pragma once

#include <string>
#include <string_view>

#include "kota/ipc/framing.h"

namespace kota::test {

/// `payload` framed as StreamTransport frames it.
inline std::string framed(std::string_view payload) {
    return ipc::frame_header(payload.size()).append(payload);
}

}  // namespace kota::test
