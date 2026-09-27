#include "kota/codec/json/json.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "kota/ipc/codec/json.h"
#include "kota/codec/macro.h"

namespace kota::ipc {

namespace {

/// Messages whose arrays and objects nest deeper than this are not read:
/// reading a value recurses once per level, so a deep enough message would
/// overflow the stack.
constexpr std::size_t max_nesting = 128;

struct outgoing_request_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    std::string method;
    codec::RawValue params;
};

struct outgoing_notification_message {
    std::string jsonrpc = "2.0";
    std::string method;
    codec::RawValue params;
};

struct outgoing_success_response_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    codec::RawValue result;
};

struct outgoing_error_response_message {
    std::string jsonrpc = "2.0";
    /// Written as null when the error answers a message whose id is unknown.
    std::optional<protocol::RequestID> id;
    Error error;
};

struct json_rpc_incoming {
    // RawValue, not optional<RequestID>, so that a null id stays apart from
    // a missing one: absent → empty(), null → "null" text.
    KOTATSU_ANNOTATE(defaulted = true)
    <codec::RawValue> id;
    std::optional<std::string> method;
    std::optional<codec::RawValue> params;
    // Not optional<RawValue> because "result": null is a valid success
    // response — optional would lose it as nullopt.
    KOTATSU_ANNOTATE(defaulted = true)
    <codec::RawValue> result;
    std::optional<Error> error;
};

/// The request id `raw`, an id member as written, holds: nothing for a null,
/// or for a value that is neither an integer nor a string.
std::optional<protocol::RequestID> read_id(std::string_view raw) {
    auto id = codec::json::from_string<protocol::RequestID>(raw);
    if(!id) {
        return std::nullopt;
    }
    return std::move(*id);
}

/// Reads JSON from the front of text that may stop anywhere.
struct PrefixReader {
    std::string_view text;
    std::size_t at = 0;

    bool ended() const {
        return at >= text.size();
    }

    void skip_space() {
        while(!ended() &&
              (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) {
            ++at;
        }
    }

    bool take(char c) {
        skip_space();
        if(ended() || text[at] != c) {
            return false;
        }
        ++at;
        return true;
    }

    /// The string at the front, quotes included, if it ends before the text.
    std::optional<std::string_view> string() {
        skip_space();
        if(ended() || text[at] != '"') {
            return std::nullopt;
        }
        const auto start = at++;
        while(!ended()) {
            const char c = text[at++];
            if(c == '\\') {
                ++at;
            } else if(c == '"') {
                return text.substr(start, at - start);
            }
        }
        return std::nullopt;
    }

    /// The value at the front, if it ends before the text: a scalar ends at
    /// the delimiter after it.
    std::optional<std::string_view> value() {
        skip_space();
        const auto start = at;
        if(ended()) {
            return std::nullopt;
        }
        if(text[at] == '"') {
            return string();
        }
        if(text[at] == '{' || text[at] == '[') {
            int depth = 0;
            while(!ended()) {
                const char c = text[at];
                if(c == '"') {
                    if(!string()) {
                        return std::nullopt;
                    }
                    continue;
                }
                ++at;
                if(c == '{' || c == '[') {
                    ++depth;
                } else if((c == '}' || c == ']') && --depth == 0) {
                    return text.substr(start, at - start);
                }
            }
            return std::nullopt;
        }
        while(!ended() && std::string_view(",}] \t\r\n").find(text[at]) == std::string_view::npos) {
            ++at;
        }
        if(ended()) {
            return std::nullopt;
        }
        return text.substr(start, at - start);
    }
};

/// Whether `payload`'s arrays and objects nest deeper than max_nesting,
/// counted without regard to whether it is valid JSON.
bool nests_too_deeply(std::string_view payload) {
    std::size_t depth = 0;
    bool in_string = false;
    for(std::size_t at = 0; at < payload.size(); ++at) {
        const char c = payload[at];
        if(in_string) {
            if(c == '\\') {
                ++at;
            } else if(c == '"') {
                in_string = false;
            }
        } else if(c == '"') {
            in_string = true;
        } else if(c == '[' || c == '{') {
            if(++depth > max_nesting) {
                return true;
            }
        } else if((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return false;
}

/// Checks JSON's grammar (RFC 8259) without reading values: a number of any
/// size is JSON, where simdjson refuses integers past 64 bits. It keeps its
/// own stack rather than recursing, however deep the value.
struct JsonChecker {
    std::string_view text;
    std::size_t at = 0;

    bool ended() const {
        return at >= text.size();
    }

    void skip_space() {
        while(!ended() &&
              (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) {
            ++at;
        }
    }

    bool take(char c) {
        if(ended() || text[at] != c) {
            return false;
        }
        ++at;
        return true;
    }

    bool take_word(std::string_view word) {
        if(!text.substr(at).starts_with(word)) {
            return false;
        }
        at += word.size();
        return true;
    }

    static bool is_digit(char c) {
        return c >= '0' && c <= '9';
    }

    static bool is_hex(char c) {
        return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    bool digits() {
        const auto start = at;
        while(!ended() && is_digit(text[at])) {
            ++at;
        }
        return at > start;
    }

    /// A string, its opening quote next.
    bool string() {
        if(!take('"')) {
            return false;
        }
        while(!ended()) {
            const auto c = static_cast<unsigned char>(text[at++]);
            if(c == '"') {
                return true;
            }
            if(c < 0x20) {
                return false;
            }
            if(c != '\\') {
                continue;
            }
            if(ended()) {
                return false;
            }
            const char escaped = text[at++];
            if(escaped == 'u') {
                for(int i = 0; i < 4; ++i) {
                    if(ended() || !is_hex(text[at++])) {
                        return false;
                    }
                }
            } else if(std::string_view(R"("\/bfnrt)").find(escaped) == std::string_view::npos) {
                return false;
            }
        }
        return false;
    }

    bool number() {
        take('-');
        if(!take('0') && !digits()) {
            return false;
        }
        if(take('.') && !digits()) {
            return false;
        }
        if(take('e') || take('E')) {
            if(!take('+')) {
                take('-');
            }
            return digits();
        }
        return true;
    }

    /// A key and its colon, in an object.
    bool key() {
        skip_space();
        if(!string()) {
            return false;
        }
        skip_space();
        return take(':');
    }

    bool check() {
        if(!simdjson::validate_utf8(text.data(), text.size())) {
            return false;
        }
        // The arrays and objects around the next value.
        std::vector<char> open;
        while(true) {
            skip_space();
            if(ended()) {
                return false;
            }
            const char c = text[at];
            if(c == '[' || c == '{') {
                ++at;
                skip_space();
                if(!take(c == '[' ? ']' : '}')) {
                    open.push_back(c);
                    if(c == '{' && !key()) {
                        return false;
                    }
                    continue;
                }
            } else if(c == '"') {
                if(!string()) {
                    return false;
                }
            } else if(c == '-' || is_digit(c)) {
                if(!number()) {
                    return false;
                }
            } else if(!take_word("true") && !take_word("false") && !take_word("null")) {
                return false;
            }
            // A value ended: close what it ends, up to the next value.
            while(true) {
                skip_space();
                if(open.empty()) {
                    return ended();
                }
                if(take(',')) {
                    if(open.back() == '{' && !key()) {
                        return false;
                    }
                    break;
                }
                if(!take(open.back() == '[' ? ']' : '}')) {
                    return false;
                }
                open.pop_back();
            }
        }
    }
};

/// Whether `payload` is one JSON value.
bool is_json(std::string_view payload) {
    return JsonChecker{.text = payload}.check();
}

/// The members of the object at the front of `text` that tell what kind of
/// message it is, read in order without decoding their values: until `text`
/// ends, or until a member cannot be read.
MessageHead read_head(std::string_view text) {
    PrefixReader reader{text};
    std::optional<protocol::RequestID> id;
    bool has_method = false;
    bool answers = false;
    if(reader.take('{')) {
        while(auto key = reader.string()) {
            if(!reader.take(':')) {
                break;
            }
            has_method = has_method || *key == R"("method")";
            answers = answers || *key == R"("result")" || *key == R"("error")";
            auto value = reader.value();
            if(!value) {
                break;
            }
            if(*key == R"("id")") {
                id = read_id(*value);
            }
            if(!reader.take(',')) {
                break;
            }
        }
    }

    if(has_method) {
        return {.kind = id ? MessageHead::Kind::Request : MessageHead::Kind::Notification,
                .id = id};
    }
    if(answers) {
        return {.kind = MessageHead::Kind::Response, .id = id};
    }
    return {.kind = MessageHead::Kind::Unknown, .id = id};
}

/// What to make of a message that is not read as a whole: its envelope did
/// not decode, or it nests too deeply. Text that is no JSON is a parse error;
/// JSON that is no message object is an invalid request (batches are not
/// supported). An object's members are read for the id it names, without
/// decoding their values: a request (it has a method) is answered as invalid
/// under that id, and a response fails the request it answers, or is only
/// logged when its id cannot be read.
IncomingMessage read_malformed(std::string_view payload, std::string reason) {
    if(!is_json(payload)) {
        return IncomingParseError{
            .id = std::nullopt,
            .error = Error(protocol::ErrorCode::ParseError, std::move(reason)),
        };
    }
    PrefixReader reader{payload};
    if(!reader.take('{')) {
        return IncomingParseError{
            .id = std::nullopt,
            .error = Error(protocol::ErrorCode::InvalidRequest,
                           reader.take('[') ? "batch messages are not supported"
                                            : "message must be an object"),
        };
    }

    auto head = read_head(payload);
    using Kind = MessageHead::Kind;
    if(head.kind == Kind::Request || head.kind == Kind::Notification) {
        return IncomingParseError{
            .id = std::move(head.id),
            .error = Error(protocol::ErrorCode::InvalidRequest, std::move(reason)),
        };
    }
    return IncomingErrorResponse{
        .id = std::move(head.id),
        .error = Error(protocol::ErrorCode::InvalidRequest, "malformed response: " + reason),
    };
}

}  // namespace

IncomingMessage JsonCodec::parse_message(std::string_view payload) {
    if(nests_too_deeply(payload)) {
        return read_malformed(payload,
                              std::format("message nests deeper than {} levels", max_nesting));
    }
    auto envelope = codec::json::from_string<json_rpc_incoming>(payload);
    if(!envelope) {
        return read_malformed(payload, envelope.error().to_string());
    }

    const bool has_id = !envelope->id.empty();
    auto id = has_id ? read_id(envelope->id.data) : std::nullopt;

    if(envelope->method.has_value()) {
        auto params =
            envelope->params.has_value() ? std::move(envelope->params->data) : std::string{};
        if(!has_id) {
            return IncomingNotification{
                .method = std::move(*envelope->method),
                .params = std::move(params),
            };
        }
        if(!id) {
            return IncomingParseError{
                .id = std::nullopt,
                .error = Error(protocol::ErrorCode::InvalidRequest,
                               "request id must be an integer or a string"),
            };
        }
        return IncomingRequest{
            .id = std::move(*id),
            .method = std::move(*envelope->method),
            .params = std::move(params),
        };
    }

    const bool has_result = !envelope->result.empty();
    const bool has_error = envelope->error.has_value();
    if(!has_id && !has_result && !has_error) {
        return IncomingParseError{
            .id = std::nullopt,
            .error =
                Error(protocol::ErrorCode::InvalidRequest, "message must contain method or id"),
        };
    }
    if(has_result == has_error) {
        return IncomingErrorResponse{
            .id = std::move(id),
            .error = Error(protocol::ErrorCode::InvalidRequest,
                           "response must contain exactly one of result or error"),
        };
    }
    if(has_error) {
        return IncomingErrorResponse{.id = std::move(id), .error = std::move(*envelope->error)};
    }
    if(!id) {
        return IncomingErrorResponse{
            .id = std::nullopt,
            .error = Error(protocol::ErrorCode::InvalidRequest,
                           "response id must be an integer or a string"),
        };
    }
    return IncomingResponse{.id = std::move(*id), .result = std::move(envelope->result.data)};
}

/// Members are read in order until the prefix ends, so a writer that puts a
/// request's id after its method, past the prefix, reads as a notification.
/// kotatsu, like vscode-jsonrpc, writes the id first.
MessageHead JsonCodec::peek(std::string_view prefix) {
    return read_head(prefix);
}

Result<std::string> JsonCodec::encode_request(const protocol::RequestID& id,
                                              std::string_view method,
                                              std::string_view params) {
    return serialize_value(outgoing_request_message{
        .id = id,
        .method = std::string(method),
        .params = codec::RawValue{std::string(params)},
    });
}

Result<std::string> JsonCodec::encode_notification(std::string_view method,
                                                   std::string_view params) {
    return serialize_value(outgoing_notification_message{
        .method = std::string(method),
        .params = codec::RawValue{std::string(params)},
    });
}

Result<std::string> JsonCodec::encode_success_response(const protocol::RequestID& id,
                                                       std::string_view result) {
    return serialize_value(outgoing_success_response_message{
        .id = id,
        .result = codec::RawValue{std::string(result)},
    });
}

Result<std::string> JsonCodec::encode_error_response(const std::optional<protocol::RequestID>& id,
                                                     const Error& error) {
    return serialize_value(outgoing_error_response_message{
        .id = id,
        .error = error,
    });
}

template class Peer<JsonCodec>;

}  // namespace kota::ipc
