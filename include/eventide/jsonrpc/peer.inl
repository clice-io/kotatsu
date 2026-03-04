#pragma once

#ifndef EVENTIDE_JSONRPC_PEER_INL_FROM_HEADER
#include "eventide/jsonrpc/peer.h"
#endif

#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "eventide/common/function_traits.h"
#include "eventide/serde/json/json.h"

namespace eventide::jsonrpc {

namespace detail {

template <typename Params>
constexpr bool has_request_traits_v = requires {
    typename protocol::RequestTraits<Params>::Result;
    protocol::RequestTraits<Params>::method;
};

template <typename Params>
constexpr bool has_notification_traits_v = requires {
    protocol::NotificationTraits<Params>::method;
};

template <typename Callback>
using request_callback_args_t = callable_args_t<std::remove_cvref_t<Callback>>;

template <typename Callback>
using request_callback_params_t =
    std::remove_cvref_t<std::tuple_element_t<1, request_callback_args_t<Callback>>>;

template <typename Callback>
using request_callback_return_t = callable_return_t<std::remove_cvref_t<Callback>>;

template <typename Callback>
using notification_callback_args_t = callable_args_t<std::remove_cvref_t<Callback>>;

template <typename Callback>
using notification_callback_params_t =
    std::remove_cvref_t<std::tuple_element_t<0, notification_callback_args_t<Callback>>>;

template <typename Callback>
using notification_callback_return_t = callable_return_t<std::remove_cvref_t<Callback>>;

template <typename Callback>
consteval void validate_request_callback_signature() {
    using Args = request_callback_args_t<Callback>;
    static_assert(std::tuple_size_v<Args> == 2, "request callback should have two parameters");

    using Context = std::remove_cvref_t<std::tuple_element_t<0, Args>>;
    static_assert(std::is_same_v<Context, RequestContext>,
                  "request callback first parameter should be RequestContext");
}

template <typename Callback>
consteval void validate_notification_callback_signature() {
    using Args = notification_callback_args_t<Callback>;
    static_assert(std::tuple_size_v<Args> == 1,
                  "notification callback should have one parameter");

    using Ret = notification_callback_return_t<Callback>;
    static_assert(std::is_same_v<Ret, void>, "notification callback should return void");
}

template <typename Params>
constexpr std::string_view normalize_params_json(std::string_view params_json) {
    if(!params_json.empty()) {
        return params_json;
    }
    if constexpr(std::is_same_v<Params, protocol::null> ||
                 std::is_same_v<Params, protocol::Value>) {
        return "null";
    }
    return "{}";
}

template <typename T>
Result<T> deserialize_json(std::string_view json,
                           protocol::ErrorCode code = protocol::ErrorCode::RequestFailed) {
    auto parsed = serde::json::parse<T>(json);
    if(!parsed) {
        return std::unexpected(
            RPCError(code, std::string(serde::json::error_message(parsed.error()))));
    }
    return std::move(*parsed);
}

template <typename T>
Result<std::string> serialize_json(const T& value,
                                   protocol::ErrorCode code = protocol::ErrorCode::InternalError) {
    auto serialized = serde::json::to_string(value);
    if(!serialized) {
        return std::unexpected(
            RPCError(code, std::string(serde::json::error_message(serialized.error()))));
    }
    return std::move(*serialized);
}

}  // namespace detail

template <typename Params>
RequestResult<Params> Peer::send_request(const Params& params) {
    co_return co_await send_request(params, cancellation_token{});
}

template <typename Params>
RequestResult<Params> Peer::send_request(const Params& params, cancellation_token token) {
    static_assert(detail::has_request_traits_v<Params>,
                  "send_request(params) requires RequestTraits<Params>");
    using Traits = protocol::RequestTraits<Params>;

    auto serialized_params = detail::serialize_json(params);
    if(!serialized_params) {
        co_return std::unexpected(serialized_params.error());
    }

    auto raw_result =
        co_await send_request_json(Traits::method, std::move(*serialized_params), std::move(token));
    if(!raw_result) {
        co_return std::unexpected(raw_result.error());
    }

    auto parsed_result = detail::deserialize_json<typename Traits::Result>(*raw_result);
    if(!parsed_result) {
        co_return std::unexpected(parsed_result.error());
    }

    co_return std::move(*parsed_result);
}

template <typename Params>
RequestResult<Params> Peer::send_request(const Params& params, std::chrono::milliseconds timeout) {
    static_assert(detail::has_request_traits_v<Params>,
                  "send_request(params) requires RequestTraits<Params>");
    using Traits = protocol::RequestTraits<Params>;

    co_return co_await send_request<typename Traits::Result>(Traits::method, params, timeout);
}

template <typename ResultT, typename Params>
task<Result<ResultT>> Peer::send_request(std::string_view method, const Params& params) {
    co_return co_await send_request<ResultT>(method, params, cancellation_token{});
}

template <typename ResultT, typename Params>
task<Result<ResultT>>
    Peer::send_request(std::string_view method, const Params& params, cancellation_token token) {
    auto serialized_params = detail::serialize_json(params);
    if(!serialized_params) {
        co_return std::unexpected(serialized_params.error());
    }

    auto raw_result = co_await send_request_json(method, std::move(*serialized_params), std::move(token));
    if(!raw_result) {
        co_return std::unexpected(raw_result.error());
    }

    auto parsed_result = detail::deserialize_json<ResultT>(*raw_result);
    if(!parsed_result) {
        co_return std::unexpected(parsed_result.error());
    }

    co_return std::move(*parsed_result);
}

template <typename ResultT, typename Params>
task<Result<ResultT>>
    Peer::send_request(std::string_view method,
                       const Params& params,
                       std::chrono::milliseconds timeout) {
    auto serialized_params = detail::serialize_json(params);
    if(!serialized_params) {
        co_return std::unexpected(serialized_params.error());
    }

    auto raw_result = co_await send_request_json(method, std::move(*serialized_params), timeout);
    if(!raw_result) {
        co_return std::unexpected(raw_result.error());
    }

    auto parsed_result = detail::deserialize_json<ResultT>(*raw_result);
    if(!parsed_result) {
        co_return std::unexpected(parsed_result.error());
    }

    co_return std::move(*parsed_result);
}

template <typename Params>
Result<void> Peer::send_notification(const Params& params) {
    static_assert(detail::has_notification_traits_v<Params>,
                  "send_notification(params) requires NotificationTraits<Params>");
    using Traits = protocol::NotificationTraits<Params>;

    auto serialized_params = detail::serialize_json(params);
    if(!serialized_params) {
        return std::unexpected(serialized_params.error());
    }
    return send_notification_json(Traits::method, std::move(*serialized_params));
}

template <typename Params>
Result<void> Peer::send_notification(std::string_view method, const Params& params) {
    auto serialized_params = detail::serialize_json(params);
    if(!serialized_params) {
        return std::unexpected(serialized_params.error());
    }
    return send_notification_json(method, std::move(*serialized_params));
}

template <typename Callback>
void Peer::on_request(Callback&& callback) {
    detail::validate_request_callback_signature<Callback>();

    using Params = detail::request_callback_params_t<Callback>;
    static_assert(detail::has_request_traits_v<Params>,
                  "on_request(callback) requires RequestTraits<Params>");

    using Ret = detail::request_callback_return_t<Callback>;
    static_assert(std::is_same_v<Ret, RequestResult<Params>>,
                  "request callback return type should be RequestResult<Params>");

    bind_request_callback<Params>(protocol::RequestTraits<Params>::method,
                                  std::forward<Callback>(callback));
}

template <typename Callback>
void Peer::on_request(std::string_view method, Callback&& callback) {
    detail::validate_request_callback_signature<Callback>();

    using Params = detail::request_callback_params_t<Callback>;
    bind_request_callback<Params>(method, std::forward<Callback>(callback));
}

template <typename Callback>
void Peer::on_notification(Callback&& callback) {
    detail::validate_notification_callback_signature<Callback>();

    using Params = detail::notification_callback_params_t<Callback>;
    static_assert(detail::has_notification_traits_v<Params>,
                  "on_notification(callback) requires NotificationTraits<Params>");

    bind_notification_callback<Params>(protocol::NotificationTraits<Params>::method,
                                       std::forward<Callback>(callback));
}

template <typename Callback>
void Peer::on_notification(std::string_view method, Callback&& callback) {
    detail::validate_notification_callback_signature<Callback>();

    using Params = detail::notification_callback_params_t<Callback>;
    bind_notification_callback<Params>(method, std::forward<Callback>(callback));
}

template <typename Params, typename Callback>
void Peer::bind_request_callback(std::string_view method, Callback&& callback) {
    auto wrapped = [cb = std::forward<Callback>(callback),
                    method_name = std::string(method),
                    peer = this](const protocol::RequestID& request_id,
                                 std::string_view params_json,
                                 cancellation_token token)
        -> task<Result<std::string>> {
        auto parsed_params = detail::deserialize_json<Params>(
            detail::normalize_params_json<Params>(params_json),
            protocol::ErrorCode::InvalidParams);
        if(!parsed_params) {
            co_return std::unexpected(parsed_params.error());
        }

        RequestContext context(*peer, request_id, std::move(token));
        context.method = method_name;

        auto result = co_await std::invoke(cb, context, *parsed_params);
        if(!result) {
            co_return std::unexpected(result.error());
        }

        auto serialized = detail::serialize_json(*result, protocol::ErrorCode::InternalError);
        if(!serialized) {
            co_return std::unexpected(serialized.error());
        }

        co_return std::move(*serialized);
    };

    register_request_callback(method, std::move(wrapped));
}

template <typename Params, typename Callback>
void Peer::bind_notification_callback(std::string_view method, Callback&& callback) {
    auto wrapped = [cb = std::forward<Callback>(callback)](std::string_view params_json) {
        auto parsed_params = detail::deserialize_json<Params>(
            detail::normalize_params_json<Params>(params_json));
        if(!parsed_params) {
            return;
        }
        std::invoke(cb, *parsed_params);
    };

    register_notification_callback(method, std::move(wrapped));
}

}  // namespace eventide::jsonrpc
