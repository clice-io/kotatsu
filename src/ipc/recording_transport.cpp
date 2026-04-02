#include "eventide/ipc/recording_transport.h"

#include <cassert>
#include <format>
#include <utility>

namespace eventide::ipc {

RecordingTransport::RecordingTransport(std::unique_ptr<Transport> transport, std::string path) :
    inner(std::move(transport)), file(std::fopen(path.c_str(), "wb")),
    start(std::chrono::steady_clock::now()) {
    assert(inner && "RecordingTransport requires a non-null inner transport");
    // If fopen fails, keep transport functional; write_record() no-ops on null file.
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
    for(unsigned char uc: payload) {
        switch(uc) {
            case '"': line.append(R"(\")"); break;
            case '\\': line.append(R"(\\)"); break;
            case '\b': line.append(R"(\b)"); break;
            case '\f': line.append(R"(\f)"); break;
            case '\n': line.append(R"(\n)"); break;
            case '\r': line.append(R"(\r)"); break;
            case '\t': line.append(R"(\t)"); break;
            default:
                if(uc < 0x20) {
                    line.append(std::format("\\u{:04X}", uc));
                } else {
                    line.push_back(static_cast<char>(uc));
                }
                break;
        }
    }
    line.append("\"}\n");
    auto written = std::fwrite(line.data(), 1, line.size(), file);
    if(written != line.size() || std::fflush(file) != 0) {
        std::fclose(file);
        file = nullptr;
    }
}

}  // namespace eventide::ipc
