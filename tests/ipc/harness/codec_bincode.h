#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "ipc/harness/codec_kit.h"
#include "kota/ipc/codec/bincode.h"
#include "kota/zest/zest.h"
#include "kota/codec/bincode/bincode.h"
#include "kota/codec/visit/common.h"

namespace kota::test {

// Mirrors of BincodeCodec's envelopes: an envelope is the index of its
// alternative, then its fields in order, with an id as an int64 and a
// params, result or data blob as length-prefixed bytes.

struct BincodeRequest {
    std::int64_t id = 0;
    std::string method;
    codec::RawValue params;
};

struct BincodeNotification {
    std::string method;
    codec::RawValue params;
};

struct BincodeResult {
    std::int64_t id = 0;
    codec::RawValue result;
};

struct BincodeError {
    std::optional<std::int64_t> id;
    std::int32_t code = 0;
    std::string message;
    codec::RawValue data;
};

using BincodeEnvelope =
    std::variant<BincodeRequest, BincodeNotification, BincodeResult, BincodeError>;

/// BincodeCodec for the ipc kits. The remote's messages are the mirrors
/// above, encoded by the bincode codec itself. Error data has no encoding in
/// this format yet (P1.5), so error_response writes none and read leaves it
/// empty.
struct BincodeWire {
    using Codec = ipc::BincodeCodec;
    constexpr static std::string_view name = "bincode";
    /// Not string_ids: an id travels as an int64.
    constexpr static Caps caps{};
    /// An envelope whose alternative index, 9, names none.
    constexpr static std::string_view garbage{"\x09\x00\x00\x00", 4};
    /// One byte, shorter than any integer field.
    constexpr static std::string_view not_a_value{"\x01", 1};

    template <typename T>
    static std::string encode(const T& value) {
        auto bytes = codec::bincode::to_bytes(value);
        ZEST_CONTEXT("BincodeWire::encode");
        EXPECT(bytes.has_value());
        if(!bytes) {
            return {};
        }
        return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    }

    template <typename T>
    static std::optional<T> decode(std::string_view body) {
        T value{};
        if(!codec::bincode::from_bytes(as_bytes(body), value)) {
            return std::nullopt;
        }
        return value;
    }

    static std::string request_raw(const RequestID& id,
                                   std::string_view method,
                                   std::string_view params) {
        return encode(BincodeEnvelope(BincodeRequest{
            .id = std::get<std::int64_t>(id),
            .method = std::string(method),
            .params = {std::string(params)},
        }));
    }

    static std::string notification_raw(std::string_view method, std::string_view params) {
        return encode(BincodeEnvelope(BincodeNotification{
            .method = std::string(method),
            .params = {std::string(params)},
        }));
    }

    static std::string response_raw(const RequestID& id, std::string_view result) {
        return encode(BincodeEnvelope(BincodeResult{
            .id = std::get<std::int64_t>(id),
            .result = {std::string(result)},
        }));
    }

    static std::string error_response(const std::optional<RequestID>& id, const ipc::Error& error) {
        std::optional<std::int64_t> number;
        if(id) {
            number = std::get<std::int64_t>(*id);
        }
        return encode(BincodeEnvelope(BincodeError{
            .id = number,
            .code = error.code,
            .message = error.message,
            .data = {},
        }));
    }

    static std::expected<Message, std::string> read(std::string_view payload) {
        BincodeEnvelope envelope;
        if(auto status = codec::bincode::from_bytes(as_bytes(payload), envelope); !status) {
            return std::unexpected("not an envelope: " + status.error().to_string());
        }
        return std::visit(
            [](auto& alternative) {
                using T = std::remove_cvref_t<decltype(alternative)>;
                Message message;
                if constexpr(std::is_same_v<T, BincodeRequest>) {
                    message.kind = Message::Kind::Request;
                    message.id = RequestID(alternative.id);
                    message.method = std::move(alternative.method);
                    message.body = std::move(alternative.params.data);
                } else if constexpr(std::is_same_v<T, BincodeNotification>) {
                    message.kind = Message::Kind::Notification;
                    message.method = std::move(alternative.method);
                    message.body = std::move(alternative.params.data);
                } else if constexpr(std::is_same_v<T, BincodeResult>) {
                    message.kind = Message::Kind::Result;
                    message.id = RequestID(alternative.id);
                    message.body = std::move(alternative.result.data);
                } else {
                    message.kind = Message::Kind::Error;
                    if(alternative.id) {
                        message.id = RequestID(*alternative.id);
                    }
                    message.error.code = alternative.code;
                    message.error.message = std::move(alternative.message);
                }
                return message;
            },
            envelope);
    }

private:
    static std::span<const std::byte> as_bytes(std::string_view text) {
        return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
    }
};

}  // namespace kota::test
