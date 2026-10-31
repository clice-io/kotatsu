#pragma once

#include "kotatsu/ipc/codec/json.h"
#include "kotatsu/ipc/peer.h"

namespace kotatsu::ipc {

using JSONPeer = Peer<JSONCodec>;

extern template class Peer<JSONCodec>;

}  // namespace kotatsu::ipc
