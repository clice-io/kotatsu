#include "eventide/ipc/peer.h"

namespace eventide::ipc {

template class Peer<JsonCodec>;
template class Peer<BincodeCodec>;

}  // namespace eventide::ipc
