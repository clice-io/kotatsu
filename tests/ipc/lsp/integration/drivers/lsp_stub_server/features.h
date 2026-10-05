#pragma once

// What lsp_stub_server's main.cpp shares with features.cpp, the language
// features. The codec instantiations of their requests are a translation unit
// of their own, which compiles beside main's.

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
