#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "ipc/harness/codec_kit.h"
#include "kota/ipc/codec/json.h"
#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"
#include "kota/codec/json/json.h"

namespace kota::test {

/// JSONCodec for the ipc kits. The remote's messages are JSON-RPC text
/// written by hand; what the codec writes is read through a dyn::Value, so a
/// null id and a missing one stay apart.
struct JSONAdapter {
    using Codec = ipc::JSONCodec;
    constexpr static std::string_view name = "json";
    constexpr static Caps caps{.string_ids = true};
    /// A request cut off before its closing brace.
    constexpr static std::string_view garbage = R"({"jsonrpc":"2.0","id":1,"method":"test/add")";
    /// A string, which no struct decodes from.
    constexpr static std::string_view not_a_value = R"("invalid")";

    template <typename T>
    static std::string encode(const T& value) {
        auto text = codec::json::to_string<ipc::lsp_config>(value);
        ZEST_CONTEXT("JSONAdapter::encode");
        ZASSERT(text.has_value());
        return std::move(*text);
    }

    template <typename T>
    static std::optional<T> decode(std::string_view body) {
        auto value = codec::json::from_string<T, ipc::lsp_config>(body);
        if(!value) {
            return std::nullopt;
        }
        return std::move(*value);
    }

    static std::string request_raw(const RequestID& id,
                                   std::string_view method,
                                   std::string_view params) {
        return std::format(R"({{"jsonrpc":"2.0","id":{},"method":{}{}}})",
                           encode(id),
                           encode(method),
                           params_member(params));
    }

    static std::string notification_raw(std::string_view method, std::string_view params) {
        return std::format(R"({{"jsonrpc":"2.0","method":{}{}}})",
                           encode(method),
                           params_member(params));
    }

    static std::string response_raw(const RequestID& id, std::string_view result) {
        return std::format(R"({{"jsonrpc":"2.0","id":{},"result":{}}})", encode(id), result);
    }

    /// Error data, where there is some, is written as it is.
    static std::string error_response(const std::optional<RequestID>& id, const ipc::Error& error) {
        std::string data;
        if(error.data) {
            data = std::format(R"(,"data":{})", encode(*error.data));
        }
        return std::format(R"({{"jsonrpc":"2.0","id":{},"error":{{"code":{},"message":{}{}}}}})",
                           id ? encode(*id) : "null",
                           error.code,
                           encode(error.message),
                           data);
    }

    static std::expected<Message, std::string> read(std::string_view payload) {
        auto document = codec::json::from_string<codec::dyn::Value>(payload);
        if(!document) {
            return std::unexpected("not JSON: " + document.error().to_string());
        }
        const auto* object = document->get_object();
        if(object == nullptr) {
            return std::unexpected("not an object");
        }
        const auto* version = object->find("jsonrpc");
        if(version == nullptr || version->get_string() != "2.0") {
            return std::unexpected(R"(no "jsonrpc":"2.0")");
        }

        Message message;
        const auto* id = object->find("id");
        if(id != nullptr && !id->is_null()) {
            if(auto number = id->get_int()) {
                message.id = RequestID(*number);
            } else if(auto text = id->get_string()) {
                message.id = RequestID(std::string(*text));
            } else {
                return std::unexpected("an id that is neither an integer nor a string");
            }
        }

        if(const auto* method = object->find("method")) {
            if(!method->is_string()) {
                return std::unexpected("a method that is not a string");
            }
            if(id != nullptr && id->is_null()) {
                return std::unexpected("a request with a null id");
            }
            message.kind = id != nullptr ? Message::Kind::Request : Message::Kind::Notification;
            message.method = std::string(*method->get_string());
            if(const auto* params = object->find("params")) {
                message.body = encode(*params);
            }
            return message;
        }

        if(id == nullptr) {
            return std::unexpected("a response without an id");
        }
        const auto* result = object->find("result");
        const auto* error = object->find("error");
        if((result == nullptr) == (error == nullptr)) {
            return std::unexpected("a response without exactly one of result and error");
        }
        if(result != nullptr) {
            message.kind = Message::Kind::Result;
            message.body = encode(*result);
            return message;
        }

        message.kind = Message::Kind::Error;
        const auto* fields = error->get_object();
        if(fields == nullptr) {
            return std::unexpected("an error that is not an object");
        }
        const auto* code = fields->find("code");
        const auto* text = fields->find("message");
        if(code == nullptr || !code->get_int() || text == nullptr || !text->is_string()) {
            return std::unexpected("an error without an integer code and a message");
        }
        message.error.code = static_cast<ipc::protocol::integer>(*code->get_int());
        message.error.message = std::string(*text->get_string());
        if(const auto* data = fields->find("data")) {
            message.error.data = *data;
        }
        return message;
    }

private:
    static std::string params_member(std::string_view params) {
        return params.empty() ? std::string() : std::format(R"(,"params":{})", params);
    }
};

}  // namespace kota::test
