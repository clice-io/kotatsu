#pragma once

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

#include "eventide/ipc/transport.h"

namespace eventide::ipc {

/// Transport decorator that records client-to-server messages to a JSONL file.
/// Each line is: {"ts":<ms_since_start>,"msg":"<escaped_json>"}
/// The timestamp enables faithful replay pacing.
class RecordingTransport : public Transport {
public:
    /// @param transport  The real transport to wrap.
    /// @param path       File path to write the recorded trace (.jsonl).
    RecordingTransport(std::unique_ptr<Transport> transport, std::string path);
    ~RecordingTransport();

    task<std::optional<std::string>> read_message() override;
    task<void, Error> write_message(std::string_view payload) override;
    Result<void> close_output() override;
    Result<void> close() override;

private:
    void write_record(std::string_view payload);

    std::unique_ptr<Transport> inner;
    std::FILE* file = nullptr;
    std::chrono::steady_clock::time_point start;
};

}  // namespace eventide::ipc
