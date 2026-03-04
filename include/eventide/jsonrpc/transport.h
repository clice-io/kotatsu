#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "eventide/async/stream.h"
#include "eventide/async/task.h"

namespace eventide::jsonrpc {

class Transport {
public:
    virtual ~Transport() = default;

    virtual task<std::optional<std::string>> read_message() = 0;

    virtual task<bool> write_message(std::string_view payload) = 0;

    virtual std::expected<void, std::string> close_output();
};

class StreamTransport : public Transport {
public:
    StreamTransport(stream input, stream output);
    explicit StreamTransport(stream stream);

    static std::expected<std::unique_ptr<StreamTransport>, std::string>
        open_stdio(event_loop& loop);

    static task<std::expected<std::unique_ptr<StreamTransport>, std::string>>
        connect_tcp(std::string_view host, int port, event_loop& loop);

    static std::expected<std::unique_ptr<StreamTransport>, std::string> open_tcp(int fd,
                                                                                 event_loop& loop);

    task<std::optional<std::string>> read_message() override;

    task<bool> write_message(std::string_view payload) override;

    std::expected<void, std::string> close_output() override;

private:
    stream read_stream;
    stream write_stream;
    bool shared_stream = false;
};

}  // namespace eventide::jsonrpc
