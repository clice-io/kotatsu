#include "eventide/ipc/recording_transport.h"

#include <cassert>
#include <format>
#include <utility>

namespace eventide::ipc {

RecordingTransport::RecordingTransport(std::unique_ptr<Transport> transport, std::string path) :
    inner(std::move(transport)), file(std::fopen(path.c_str(), "wb")),
    start(std::chrono::steady_clock::now()) {
    assert(inner && "RecordingTransport requires a non-null inner transport");
    assert(file && "RecordingTransport failed to open trace output file");
}

RecordingTransport::~RecordingTransport() {
    if(file) {
        std::fclose(file);
    }
}

task<std::optional<std::string>> RecordingTransport::read_message() {
    auto msg = co_await inner->read_message();
    if(msg.has_value()) {
        write_record(*msg);
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
    if(file) {
        std::fclose(file);
        file = nullptr;
    }
    return inner->close();
}

void RecordingTransport::write_record(std::string_view payload) {
    if(!file) {
        return;
    }
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    std::string line;
    line.reserve(payload.size() + 64);
    line.append(std::format(R"({{"ts":{},"msg":")", ms));
    for(char c: payload) {
        switch(c) {
            case '"': line.append(R"(\")"); break;
            case '\\': line.append(R"(\\)"); break;
            case '\n': line.append(R"(\n)"); break;
            case '\r': line.append(R"(\r)"); break;
            case '\t': line.append(R"(\t)"); break;
            default: line.push_back(c); break;
        }
    }
    line.append("\"}\n");
    std::fwrite(line.data(), 1, line.size(), file);
    std::fflush(file);
}

}  // namespace eventide::ipc
