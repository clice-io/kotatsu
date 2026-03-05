#pragma once

#include "eventide/ipc/codec.h"

namespace eventide::ipc {

class BincodeCodec : public Codec {
public:
    IncomingMessage parse_message(std::string_view payload) override;

    Result<std::string> encode_request(const protocol::RequestID& id,
                                       std::string_view method,
                                       std::string_view params) override;

    Result<std::string> encode_notification(std::string_view method,
                                            std::string_view params) override;

    Result<std::string> encode_success_response(const protocol::RequestID& id,
                                                std::string_view result) override;

    Result<std::string> encode_error_response(const protocol::ResponseID& id,
                                              const RPCError& error) override;

    std::optional<protocol::RequestID> parse_cancel_id(std::string_view params) override;
};

inline auto make_bincode_codec() -> std::unique_ptr<Codec> {
    return std::make_unique<BincodeCodec>();
}

}  // namespace eventide::ipc
