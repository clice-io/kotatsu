#pragma once

#include "kota/ipc/codec/bincode.h"
#include "kota/ipc/peer.h"

namespace kota::ipc {

using BincodePeer = Peer<BincodeCodec>;

extern template class Peer<BincodeCodec>;

}  // namespace kota::ipc
