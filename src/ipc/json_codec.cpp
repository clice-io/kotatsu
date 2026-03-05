#include "eventide/ipc/json_codec.h"

#include <string>
#include <string_view>

#include "eventide/serde/json/json.h"

namespace eventide::ipc {

namespace {

template <typename T>
Result<T> parse_json_value(std::string_view json,
                           protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
    auto parsed = serde::json::parse<T>(json);
    if(!parsed) {
        return std::unexpected(
            RPCError(code, std::string(serde::json::error_message(parsed.error()))));
    }
    return std::move(*parsed);
}

template <typename T>
Result<std::string>
    serialize_json_value(const T& value,
                         protocol::ErrorCode code = protocol::ErrorCode::InternalError) {
    auto serialized = serde::json::to_string(value);
    if(!serialized) {
        return std::unexpected(
            RPCError(code, std::string(serde::json::error_message(serialized.error()))));
    }
    return std::move(*serialized);
}

struct cancel_request_params {
    protocol::RequestID id;
};

struct outgoing_request_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    std::string method;
    protocol::Value params;
};

struct outgoing_notification_message {
    std::string jsonrpc = "2.0";
    std::string method;
    protocol::Value params;
};

struct outgoing_success_response_message {
    std::string jsonrpc = "2.0";
    protocol::RequestID id;
    protocol::Value result;
};

struct outgoing_error_response_message {
    std::string jsonrpc = "2.0";
    protocol::ResponseID id;
    protocol::ResponseError error;
};

struct parsed_incoming_id {
    bool present = false;
    bool is_null = false;
    protocol::RequestID request_id{};
};

}  // namespace

IncomingMessage JsonCodec::parse_message(std::string_view payload) {
    IncomingMessage msg;
    parsed_incoming_id id;

    auto set_parse_error = [&](protocol::ErrorCode code, std::string message) {
        msg.id_is_null = id.is_null;
        if(id.present && !id.is_null) {
            msg.id = id.request_id;
        }
        msg.parse_error = RPCError(code, std::move(message));
    };

    simdjson::ondemand::parser parser;
    simdjson::padded_string json(payload);

    simdjson::ondemand::document document{};
    auto document_result = parser.iterate(json);
    auto document_error = std::move(document_result).get(document);
    if(document_error != simdjson::SUCCESS) {
        set_parse_error(protocol::ErrorCode::ParseError,
                        std::string(simdjson::error_message(document_error)));
        return msg;
    }

    simdjson::ondemand::object object{};
    auto object_result = document.get_object();
    auto object_error = std::move(object_result).get(object);
    if(object_error != simdjson::SUCCESS) {
        auto code = object_error == simdjson::INCORRECT_TYPE ? protocol::ErrorCode::InvalidRequest
                                                             : protocol::ErrorCode::ParseError;
        set_parse_error(code, std::string(simdjson::error_message(object_error)));
        return msg;
    }

    std::optional<std::string_view> params_json;
    std::optional<std::string_view> result_json;
    std::optional<std::string_view> error_json;

    for(auto field_result: object) {
        simdjson::ondemand::field field{};
        auto field_error = std::move(field_result).get(field);
        if(field_error != simdjson::SUCCESS) {
            set_parse_error(protocol::ErrorCode::InvalidRequest,
                            std::string(simdjson::error_message(field_error)));
            return msg;
        }

        std::string_view key;
        auto key_error = field.unescaped_key().get(key);
        if(key_error != simdjson::SUCCESS) {
            set_parse_error(protocol::ErrorCode::InvalidRequest,
                            std::string(simdjson::error_message(key_error)));
            return msg;
        }

        auto value = field.value();
        if(key == "method") {
            std::string_view method_text;
            auto method_error = value.get_string().get(method_text);
            if(method_error != simdjson::SUCCESS) {
                set_parse_error(protocol::ErrorCode::InvalidRequest,
                                std::string(simdjson::error_message(method_error)));
                return msg;
            }
            msg.method = std::string(method_text);
            continue;
        }

        if(key == "id") {
            simdjson::ondemand::json_type id_type{};
            auto id_type_error = value.type().get(id_type);
            if(id_type_error != simdjson::SUCCESS) {
                set_parse_error(protocol::ErrorCode::InvalidRequest,
                                std::string(simdjson::error_message(id_type_error)));
                return msg;
            }

            if(id_type == simdjson::ondemand::json_type::number) {
                std::int64_t integer_id = 0;
                auto integer_error = value.get_int64().get(integer_id);
                if(integer_error != simdjson::SUCCESS) {
                    set_parse_error(protocol::ErrorCode::InvalidRequest,
                                    "request id must be integer");
                    return msg;
                }

                id.present = true;
                id.is_null = false;
                id.request_id = protocol::RequestID{integer_id};
                continue;
            }

            if(id_type == simdjson::ondemand::json_type::null) {
                id.present = true;
                id.is_null = true;
                continue;
            }

            set_parse_error(protocol::ErrorCode::InvalidRequest,
                            "request id must be integer or null");
            return msg;
        }

        std::string_view raw_json;
        auto raw_error = value.raw_json().get(raw_json);
        if(raw_error != simdjson::SUCCESS) {
            set_parse_error(protocol::ErrorCode::InvalidRequest,
                            std::string(simdjson::error_message(raw_error)));
            return msg;
        }

        if(key == "params") {
            params_json = raw_json;
            continue;
        }

        if(key == "result") {
            result_json = raw_json;
            continue;
        }

        if(key == "error") {
            error_json = raw_json;
            continue;
        }
    }

    if(!document.at_end()) {
        set_parse_error(protocol::ErrorCode::InvalidRequest,
                        "trailing content after JSON-RPC message");
        return msg;
    }

    msg.id_is_null = id.is_null;
    if(id.present && !id.is_null) {
        msg.id = id.request_id;
    }

    if(params_json.has_value()) {
        msg.params = std::string(*params_json);
    }

    if(result_json.has_value()) {
        msg.result = std::string(*result_json);
    }

    if(error_json.has_value()) {
        auto parsed_error =
            parse_json_value<protocol::ResponseError>(*error_json,
                                                      protocol::ErrorCode::InvalidRequest);
        if(!parsed_error) {
            msg.error = parsed_error.error();
        } else {
            msg.error = RPCError(parsed_error->code,
                                 std::move(parsed_error->message),
                                 std::move(parsed_error->data));
        }
    }

    return msg;
}

Result<std::string> JsonCodec::encode_request(const protocol::RequestID& id,
                                              std::string_view method,
                                              std::string_view params) {
    auto parsed_params =
        parse_json_value<protocol::Value>(params, protocol::ErrorCode::InternalError);
    if(!parsed_params) {
        return std::unexpected(parsed_params.error());
    }

    return serialize_json_value(outgoing_request_message{
        .id = id,
        .method = std::string(method),
        .params = std::move(*parsed_params),
    });
}

Result<std::string> JsonCodec::encode_notification(std::string_view method,
                                                   std::string_view params) {
    auto parsed_params =
        parse_json_value<protocol::Value>(params, protocol::ErrorCode::InternalError);
    if(!parsed_params) {
        return std::unexpected(parsed_params.error());
    }

    return serialize_json_value(outgoing_notification_message{
        .method = std::string(method),
        .params = std::move(*parsed_params),
    });
}

Result<std::string> JsonCodec::encode_success_response(const protocol::RequestID& id,
                                                       std::string_view result) {
    auto parsed_result =
        parse_json_value<protocol::Value>(result, protocol::ErrorCode::InternalError);
    if(!parsed_result) {
        return std::unexpected(parsed_result.error());
    }

    return serialize_json_value(outgoing_success_response_message{
        .id = id,
        .result = std::move(*parsed_result),
    });
}

Result<std::string> JsonCodec::encode_error_response(const protocol::ResponseID& id,
                                                     const RPCError& error) {
    return serialize_json_value(outgoing_error_response_message{
        .id = id,
        .error = protocol::ResponseError{error.code, error.message, error.data},
    });
}

std::optional<protocol::RequestID> JsonCodec::parse_cancel_id(std::string_view params) {
    auto parsed =
        parse_json_value<cancel_request_params>(params, protocol::ErrorCode::InvalidParams);
    if(!parsed) {
        return std::nullopt;
    }
    return parsed->id;
}

}  // namespace eventide::ipc
