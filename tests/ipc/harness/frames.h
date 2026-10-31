#pragma once

#include <string>
#include <string_view>

#include "kotatsu/ipc/framing.h"

namespace kotatsu::test {

/// `payload` framed as StreamTransport frames it.
inline std::string framed(std::string_view payload) {
    return ipc::frame_header(payload.size()).append(payload);
}

}  // namespace kotatsu::test
