#pragma once

#include "kota/ipc/codec/json.h"
#include "kota/ipc/peer.h"

namespace kota::ipc {

using JSONPeer = Peer<JSONCodec>;

extern template class Peer<JSONCodec>;

}  // namespace kota::ipc
