#include "eventide/ipc/bincode_codec.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace eventide::ipc {

namespace {

// Binary message envelope format:
//   [1 byte: message_type]
//   For request:      [8 bytes: id] [varint: method_len] [method] [varint: payload_len] [payload]
//   For notification: [varint: method_len] [method] [varint: payload_len] [payload]
//   For success:      [8 bytes: id] [varint: payload_len] [payload]
//   For error:        [1 byte: id_present] [8 bytes: id (if present)] [4 bytes: code]
//                     [varint: message_len] [message] [varint: payload_len] [payload]
//
// Message types:
enum class MessageType : std::uint8_t {
    request = 0x01,
    notification = 0x02,
    success_response = 0x03,
    error_response = 0x04,
};

// Varint encoding: LEB128 unsigned
void write_varint(std::string& out, std::uint64_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7F);
        value >>= 7;
        if(value != 0) {
            byte |= 0x80;
        }
        out.push_back(static_cast<char>(byte));
    } while(value != 0);
}

bool read_varint(std::string_view& data, std::uint64_t& value) {
    value = 0;
    unsigned shift = 0;
    while(!data.empty()) {
        auto byte = static_cast<std::uint8_t>(data.front());
        data.remove_prefix(1);
        value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if((byte & 0x80) == 0) {
            return true;
        }
        shift += 7;
        if(shift >= 64) {
            return false;
        }
    }
    return false;
}

void write_u8(std::string& out, std::uint8_t value) {
    out.push_back(static_cast<char>(value));
}

bool read_u8(std::string_view& data, std::uint8_t& value) {
    if(data.empty()) {
        return false;
    }
    value = static_cast<std::uint8_t>(data.front());
    data.remove_prefix(1);
    return true;
}

void write_i32(std::string& out, std::int32_t value) {
    std::uint32_t u;
    std::memcpy(&u, &value, sizeof(u));
    out.push_back(static_cast<char>(u & 0xFF));
    out.push_back(static_cast<char>((u >> 8) & 0xFF));
    out.push_back(static_cast<char>((u >> 16) & 0xFF));
    out.push_back(static_cast<char>((u >> 24) & 0xFF));
}

bool read_i32(std::string_view& data, std::int32_t& value) {
    if(data.size() < 4) {
        return false;
    }
    std::uint32_t u = 0;
    u |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[0]));
    u |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[1])) << 8;
    u |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[2])) << 16;
    u |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[3])) << 24;
    std::memcpy(&value, &u, sizeof(value));
    data.remove_prefix(4);
    return true;
}

void write_i64(std::string& out, std::int64_t value) {
    std::uint64_t u;
    std::memcpy(&u, &value, sizeof(u));
    for(int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((u >> (i * 8)) & 0xFF));
    }
}

bool read_i64(std::string_view& data, std::int64_t& value) {
    if(data.size() < 8) {
        return false;
    }
    std::uint64_t u = 0;
    for(int i = 0; i < 8; ++i) {
        u |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[i])) << (i * 8);
    }
    std::memcpy(&value, &u, sizeof(value));
    data.remove_prefix(8);
    return true;
}

void write_string(std::string& out, std::string_view str) {
    write_varint(out, str.size());
    out.append(str);
}

bool read_string(std::string_view& data, std::string& value) {
    std::uint64_t len = 0;
    if(!read_varint(data, len)) {
        return false;
    }
    if(data.size() < len) {
        return false;
    }
    value.assign(data.data(), len);
    data.remove_prefix(len);
    return true;
}

bool read_blob(std::string_view& data, std::string& value) {
    return read_string(data, value);
}

RPCError parse_error(protocol::ErrorCode code, std::string message) {
    return RPCError(code, std::move(message));
}

}  // namespace

IncomingMessage BincodeCodec::parse_message(std::string_view payload) {
    IncomingMessage msg;
    auto data = payload;

    std::uint8_t type_byte = 0;
    if(!read_u8(data, type_byte)) {
        msg.parse_error = parse_error(protocol::ErrorCode::ParseError, "empty message");
        return msg;
    }

    auto type = static_cast<MessageType>(type_byte);

    switch(type) {
        case MessageType::request: {
            std::int64_t id_value = 0;
            if(!read_i64(data, id_value)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated request id");
                return msg;
            }
            msg.id = protocol::RequestID{id_value};

            std::string method;
            if(!read_string(data, method)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated request method");
                return msg;
            }
            msg.method = std::move(method);

            if(!read_blob(data, msg.params)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated request params");
                return msg;
            }
            break;
        }

        case MessageType::notification: {
            std::string method;
            if(!read_string(data, method)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated notification method");
                return msg;
            }
            msg.method = std::move(method);

            if(!read_blob(data, msg.params)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated notification params");
                return msg;
            }
            break;
        }

        case MessageType::success_response: {
            std::int64_t id_value = 0;
            if(!read_i64(data, id_value)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated response id");
                return msg;
            }
            msg.id = protocol::RequestID{id_value};

            if(!read_blob(data, msg.result)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated response result");
                return msg;
            }
            break;
        }

        case MessageType::error_response: {
            std::uint8_t id_present = 0;
            if(!read_u8(data, id_present)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated error response");
                return msg;
            }

            if(id_present != 0) {
                std::int64_t id_value = 0;
                if(!read_i64(data, id_value)) {
                    msg.parse_error =
                        parse_error(protocol::ErrorCode::ParseError, "truncated error response id");
                    return msg;
                }
                msg.id = protocol::RequestID{id_value};
            }

            std::int32_t error_code = 0;
            if(!read_i32(data, error_code)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated error code");
                return msg;
            }

            std::string error_message;
            if(!read_string(data, error_message)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated error message");
                return msg;
            }

            // Error data is a bincode blob (may be empty)
            std::string error_data;
            if(!read_blob(data, error_data)) {
                msg.parse_error =
                    parse_error(protocol::ErrorCode::ParseError, "truncated error data");
                return msg;
            }

            msg.error =
                RPCError(static_cast<protocol::integer>(error_code), std::move(error_message));
            break;
        }

        default:
            msg.parse_error = parse_error(protocol::ErrorCode::ParseError,
                                          "unknown message type: " + std::to_string(type_byte));
            return msg;
    }

    if(!data.empty()) {
        msg.parse_error =
            parse_error(protocol::ErrorCode::InvalidRequest, "trailing bytes in message");
        return msg;
    }

    return msg;
}

Result<std::string> BincodeCodec::encode_request(const protocol::RequestID& id,
                                                 std::string_view method,
                                                 std::string_view params) {
    std::string out;
    out.reserve(1 + 8 + 10 + method.size() + 10 + params.size());
    write_u8(out, static_cast<std::uint8_t>(MessageType::request));
    write_i64(out, id.value);
    write_string(out, method);
    write_string(out, params);
    return out;
}

Result<std::string> BincodeCodec::encode_notification(std::string_view method,
                                                      std::string_view params) {
    std::string out;
    out.reserve(1 + 10 + method.size() + 10 + params.size());
    write_u8(out, static_cast<std::uint8_t>(MessageType::notification));
    write_string(out, method);
    write_string(out, params);
    return out;
}

Result<std::string> BincodeCodec::encode_success_response(const protocol::RequestID& id,
                                                          std::string_view result) {
    std::string out;
    out.reserve(1 + 8 + 10 + result.size());
    write_u8(out, static_cast<std::uint8_t>(MessageType::success_response));
    write_i64(out, id.value);
    write_string(out, result);
    return out;
}

Result<std::string> BincodeCodec::encode_error_response(const protocol::ResponseID& id,
                                                        const RPCError& error) {
    std::string out;
    out.reserve(1 + 1 + 8 + 4 + 10 + error.message.size() + 10);
    write_u8(out, static_cast<std::uint8_t>(MessageType::error_response));

    if(id.has_value()) {
        write_u8(out, 1);
        write_i64(out, id->value);
    } else {
        write_u8(out, 0);
    }

    write_i32(out, static_cast<std::int32_t>(error.code));
    write_string(out, error.message);

    // Error data — empty blob (bincode Value serialization not supported here)
    write_varint(out, 0);

    return out;
}

std::optional<protocol::RequestID> BincodeCodec::parse_cancel_id(std::string_view params) {
    // cancel_request_params is just a RequestID (int64) serialized as 8 little-endian bytes.
    std::int64_t id_value = 0;
    auto data = params;
    if(!read_i64(data, id_value)) {
        return std::nullopt;
    }
    return protocol::RequestID{id_value};
}

}  // namespace eventide::ipc
