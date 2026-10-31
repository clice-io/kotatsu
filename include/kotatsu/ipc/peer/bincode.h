#pragma once

#include "kotatsu/ipc/codec/bincode.h"
#include "kotatsu/ipc/peer.h"

namespace kotatsu::ipc {

using BincodePeer = Peer<BincodeCodec>;

extern template class Peer<BincodeCodec>;

}  // namespace kotatsu::ipc
