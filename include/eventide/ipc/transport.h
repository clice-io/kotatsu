#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "eventide/ipc/codec.h"
#include "eventide/async/async.h"

namespace eventide::ipc {

class Transport {
public:
    virtual ~Transport() = default;

    virtual task<std::optional<std::string>> read_message() = 0;

    virtual task<void, Error> write_message(std::string_view payload) = 0;

    virtual Result<void> close_output();
};

class StreamTransport : public Transport {
public:
    StreamTransport(stream input, stream output);
    explicit StreamTransport(stream stream);

    static Result<std::unique_ptr<StreamTransport>> open_stdio(event_loop& loop);

    static task<std::unique_ptr<StreamTransport>, Error> connect_tcp(std::string_view host,
                                                                     int port,
                                                                     event_loop& loop);

    static Result<std::unique_ptr<StreamTransport>> open_tcp(int fd, event_loop& loop);

    task<std::optional<std::string>> read_message() override;

    task<void, Error> write_message(std::string_view payload) override;

    Result<void> close_output() override;

private:
    stream read_stream;
    stream write_stream;
    bool shared_stream = false;
};

}  // namespace eventide::ipc
