#include "kota/ipc/peer/bincode.h"

namespace kota::ipc {

// In an object of its own, apart from the codec's: a program that links only
// the codec takes nothing of the peer, the event loop or libuv.
template class Peer<BincodeCodec>;

}  // namespace kota::ipc
