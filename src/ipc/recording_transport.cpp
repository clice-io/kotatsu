#include "eventide/ipc/recording_transport.h"

#include <cassert>
#include <format>
#include <utility>

namespace eventide::ipc {

RecordingTransport::RecordingTransport(std::unique_ptr<Transport> transport, std::string path) :
    inner(std::move(transport)), file(std::move(path), std::ios::binary | std::ios::trunc) {
    assert(inner && "RecordingTransport requires a non-null inner transport");
    assert(file.is_open() && "RecordingTransport failed to open trace output file");
}

RecordingTransport::~RecordingTransport() = default;

task<std::optional<std::string>> RecordingTransport::read_message() {
    auto msg = co_await inner->read_message();
    if(msg.has_value()) {
        write_framed(*msg);
    }
    co_return msg;
}

task<void, Error> RecordingTransport::write_message(std::string_view payload) {
    co_await inner->write_message(payload);
}

Result<void> RecordingTransport::close_output() {
    return inner->close_output();
}

Result<void> RecordingTransport::close() {
    file.close();
    return inner->close();
}

void RecordingTransport::write_framed(std::string_view payload) {
    if(!file) {
        return;
    }
    auto header = std::format("Content-Length: {}\r\n\r\n", payload.size());
    file.write(header.data(), static_cast<std::streamsize>(header.size()));
    file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    file.flush();
}

}  // namespace eventide::ipc
