#pragma once

// What lsp_stub_server's main() shares with its language features, which are
// a translation unit of their own: in one with main's initialize, the codec
// instantiations of every request took longer to compile than any other test.

#include "kota/ipc/codec/json.h"
#include "kota/ipc/lsp/protocol.h"

namespace kota::test {

inline auto make_range(ipc::protocol::uinteger line,
                       ipc::protocol::uinteger col,
                       ipc::protocol::uinteger end_col) -> ipc::protocol::Range {
    return {
        {line, col    },
        {line, end_col}
    };
}

/// Answers the requests about a document's contents, from textDocument/hover
/// to workspace/symbol, with fixed results.
void add_language_features(ipc::JSONPeer& peer);

}  // namespace kota::test
