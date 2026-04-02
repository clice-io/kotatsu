#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include "eventide/ipc/transport.h"

namespace eventide::ipc {

/// Transport decorator that records client-to-server messages to a file.
/// The recorded file uses standard LSP wire format (Content-Length framing)
/// and can be piped directly to stdin for replay.
class RecordingTransport : public Transport {
public:
    /// @param transport  The real transport to wrap.
    /// @param path       File path to write the recorded trace.
    RecordingTransport(std::unique_ptr<Transport> transport, std::string path);
    ~RecordingTransport();

    task<std::optional<std::string>> read_message() override;
    task<void, Error> write_message(std::string_view payload) override;
    Result<void> close_output() override;
    Result<void> close() override;

private:
    void write_framed(std::string_view payload);

    std::unique_ptr<Transport> inner;
    std::ofstream file;
};

}  // namespace eventide::ipc
