#pragma once

#ifndef KOTA_IPC_PEER_INL_FROM_HEADER
#include "kota/ipc/peer.h"
#endif

#include <cassert>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "kota/support/function_traits.h"

namespace kota::ipc {

namespace detail {

template <typename Params>
constexpr bool has_request_traits_v = requires {
    typename protocol::RequestTraits<Params>::Result;
    protocol::RequestTraits<Params>::method;
};

template <typename Params>
constexpr bool has_notification_traits_v =
    requires { protocol::NotificationTraits<Params>::method; };

template <typename Callback>
using callback_args_t = callable_args_t<std::remove_cvref_t<Callback>>;

template <typename Callback>
using callback_return_t = callable_return_t<std::remove_cvref_t<Callback>>;

/// The type a callback's parameter `I` names, as a value.
template <typename Callback, std::size_t I>
using callback_param_t = std::remove_cvref_t<std::tuple_element_t<I, callback_args_t<Callback>>>;

template <typename Callback, typename PeerT>
consteval void validate_request_callback_signature() {
    using Args = callback_args_t<Callback>;
    static_assert(std::tuple_size_v<Args> == 2, "request callback should have two parameters");

    using Context = std::remove_cvref_t<std::tuple_element_t<0, Args>>;
    static_assert(std::is_same_v<Context, basic_request_context<PeerT>>,
                  "request callback first parameter should be RequestContext");
}

template <typename Callback>
consteval void validate_notification_callback_signature() {
    static_assert(std::tuple_size_v<callback_args_t<Callback>> == 1,
                  "notification callback should have one parameter");
    static_assert(std::is_same_v<callback_return_t<Callback>, void>,
                  "notification callback should return void");
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Peer<CodecT>::Self
// ---------------------------------------------------------------------------

template <typename CodecT>
struct Peer<CodecT>::Self {
    using RequestCallback = std::function<
        task<std::string, Error>(const protocol::RequestID&, std::string_view, cancellation_token)>;
    using NotificationCallback = std::function<void(std::string_view)>;

    struct PendingRequest {
        event ready;
        std::optional<Result<std::string>> response;
    };

    /// How the wait for an answer ended.
    enum class Ending : std::uint8_t {
        Answered,
        Cancelled,
        TimedOut,
    };

    event_loop& loop;
    std::unique_ptr<Transport> transport;
    CodecT codec;

    std::deque<std::string> outgoing_queue;
    std::int64_t next_request_id = 1;

    std::unordered_map<std::string, RequestCallback> request_callbacks;
    std::unordered_map<std::string, NotificationCallback> notification_callbacks;

    std::unordered_map<protocol::RequestID, std::shared_ptr<PendingRequest>> pending_requests;
    std::unordered_map<protocol::RequestID, std::shared_ptr<cancellation_source>> incoming_requests;

    /// Answers can still arrive: the read loop has not ended. Once it has,
    /// a new request fails at once, since nothing could answer it.
    bool input_open = true;
    /// Messages can still be written. Once not, every send fails at once.
    bool output_open = true;
    /// close_output() was called: what is queued is written, then the
    /// transport's output closes.
    bool closing_output = false;
    /// close() or a failed write closed the transport.
    bool closed = false;
    /// The read loop has ended and every handler has finished, so no more
    /// answers are queued.
    bool answers_done = false;
    /// run() was called; it is called once.
    bool started = false;
    event write_event;

    LogCallback logger;
    LogLevel min_level = LogLevel::info;

    Self(event_loop& loop, std::unique_ptr<Transport> transport, CodecT codec) :
        loop(loop), transport(std::move(transport)), codec(std::move(codec)) {}

    /// Formats and hands `format` to the logger when `level` passes its
    /// threshold; nothing is formatted otherwise.
    template <typename... Args>
    void log(LogLevel level, std::format_string<Args...> format, Args&&... args) {
        if(logger && level >= min_level) {
            logger(level, std::format(format, std::forward<Args>(args)...));
        }
    }

    /// Why a message cannot be sent now, if it cannot; a request also needs
    /// the input open for its answer.
    std::optional<Error> unsendable(bool expects_answer) const {
        if(closed) {
            return Error("peer closed");
        }
        if(!output_open || closing_output) {
            return Error("peer output closed");
        }
        if(expects_answer && !input_open) {
            return Error("peer input closed");
        }
        return std::nullopt;
    }

    /// Queues `payload`; an answer the output can no longer take is dropped.
    void enqueue_outgoing(std::string payload) {
        if(!output_open || closing_output) {
            log(LogLevel::debug, "dropped, the output is closed: {}", payload);
            return;
        }
        log(LogLevel::trace, "send: {}", payload);
        outgoing_queue.push_back(std::move(payload));
        write_event.set();
    }

    /// Writes what is queued until the output closes, or until the answers
    /// are done and the queue is empty.
    task<> write_loop() {
        while(true) {
            if(outgoing_queue.empty()) {
                if(closing_output) {
                    finish_output();
                }
                if(!output_open || answers_done) {
                    break;
                }
                write_event.reset();
                co_await write_event.wait();
                continue;
            }

            auto payload = std::move(outgoing_queue.front());
            outgoing_queue.pop_front();
            auto written = co_await transport->write_message(payload);
            if(!written) {
                fail_output(written.error().message);
                break;
            }
        }
        output_open = false;
    }

    /// Half-closes the transport once close_output()'s queue is written.
    void finish_output() {
        closing_output = false;
        output_open = false;
        if(auto closed_output = transport->close_output(); !closed_output) {
            log(LogLevel::error, "closing the output failed: {}", closed_output.error().message);
        }
    }

    /// A write failed: nothing more can be written or answered, so every
    /// pending request fails and the transport closes, which ends the read
    /// loop too.
    void fail_output(const std::string& message) {
        log(LogLevel::error, "transport write failed: {}", message);
        output_open = false;
        closing_output = false;
        outgoing_queue.clear();
        fail_pending_requests(Error(message));
        // A close() that caused the failure has closed the transport already.
        if(closed) {
            return;
        }
        closed = true;
        // Their answers could not be written.
        cancel_handlers();
        if(auto result = transport->close(); !result) {
            log(LogLevel::error, "closing the transport failed: {}", result.error().message);
        }
    }

    /// Cancels every running request handler.
    void cancel_handlers() {
        // Copy the sources first: cancel() may resume handlers, which erase
        // their entries.
        auto values = incoming_requests | std::views::values;
        std::vector<std::shared_ptr<cancellation_source>> sources(values.begin(), values.end());
        for(auto& source: sources) {
            source->cancel();
        }
        incoming_requests.clear();
    }

    /// The read loop ended: nothing can answer a pending request any more.
    void end_input(const Error& why) {
        input_open = false;
        fail_pending_requests(why);
    }

    /// Reads and dispatches until the input ends, and returns why it ended:
    /// Closed or Malformed.
    task<ReadError> read_loop(task_group<>& handlers) {
        log(LogLevel::info, "read loop started");
        while(true) {
            auto message = co_await transport->read_message();
            if(message) {
                dispatch_incoming_message(*message, handlers);
            } else if(message.error().kind == ReadError::Kind::Oversized) {
                skip_oversized(message.error());
            } else {
                log(LogLevel::info, "read loop ended: {}", message.error().message);
                co_return std::move(message).error();
            }
        }
    }

    /// A message too large to read fails what it concerns, as far as its
    /// first bytes tell: a request is answered, a response fails the request
    /// it answers, a notification is dropped. Unknown, it could have been
    /// any pending request's answer, so every pending request fails.
    void skip_oversized(const ReadError& skipped) {
        log(LogLevel::warn, "skipped: {}", skipped.message);
        Error too_large(protocol::ErrorCode::MessageTooLarge, skipped.message);
        auto head = codec.peek(skipped.prefix);
        using Kind = MessageHead::Kind;
        if(head.kind == Kind::Request) {
            send_error(head.id, too_large);
        } else if(head.kind == Kind::Response && head.id) {
            complete_pending_request(*head.id, outcome_error(std::move(too_large)));
        } else if(head.kind != Kind::Notification) {
            fail_pending_requests(too_large);
        }
    }

    /// A result as it came in, read as T. A RawValue takes it as it is, the
    /// way a handler's RawValue result is sent.
    template <typename T>
    Result<T> read_result(std::string raw) {
        if constexpr(std::is_same_v<T, codec::RawValue>) {
            return codec::RawValue{std::move(raw)};
        } else {
            return codec.template deserialize_value<T>(raw);
        }
    }

    void send_error(const std::optional<protocol::RequestID>& id, const Error& error) {
        log(LogLevel::error, "error response: {}", error.message);
        auto response = codec.encode_error_response(id, error);
        if(response) {
            enqueue_outgoing(std::move(*response));
        }
    }

    /// Tells the remote that the request `id` is no longer awaited.
    void send_cancel_request(const protocol::RequestID& id) {
        auto params = codec.serialize_value(protocol::CancelRequestParams{id});
        assert(params && "a request id always encodes");
        auto notification = codec.encode_notification("$/cancelRequest", *params);
        assert(notification && "$/cancelRequest always encodes");
        enqueue_outgoing(std::move(*notification));
    }

    static task<Ending> answered(std::shared_ptr<PendingRequest> pending) {
        co_await pending->ready.wait();
        co_return Ending::Answered;
    }

    static task<Ending> cancelled(cancellation_token token) {
        co_await token.wait().catch_cancel();
        co_return Ending::Cancelled;
    }

    static task<Ending> expired(std::chrono::milliseconds timeout, event_loop& loop) {
        co_await sleep(timeout, loop);
        co_return Ending::TimedOut;
    }

    void complete_pending_request(const protocol::RequestID& id, Result<std::string>&& response) {
        auto it = pending_requests.find(id);
        if(it == pending_requests.end()) {
            log(LogLevel::warn, "orphan response for id={}", id);
            return;
        }

        log(LogLevel::debug, "response received for id={}", id);

        auto pending = std::move(it->second);
        pending_requests.erase(it);
        pending->response = std::move(response);
        pending->ready.set();
    }

    void fail_pending_requests(const Error& error) {
        if(pending_requests.empty()) {
            return;
        }

        log(LogLevel::error,
            "failing {} pending request(s): {}",
            pending_requests.size(),
            error.message);

        auto values = pending_requests | std::views::values;
        std::vector<std::shared_ptr<PendingRequest>> pending(values.begin(), values.end());
        pending_requests.clear();

        for(auto& state: pending) {
            state->response = outcome_error(error);
            state->ready.set();
        }
    }

    void dispatch_notification(const std::string& method, std::string_view params) {
        log(LogLevel::debug, "notification: {}", method);

        if(method == "$/cancelRequest") {
            auto parsed = codec.template deserialize_value<protocol::CancelRequestParams>(params);
            if(parsed) {
                if(auto it = incoming_requests.find(parsed->id); it != incoming_requests.end()) {
                    auto source = it->second;
                    source->cancel();
                }
            }
            return;
        }

        auto it = notification_callbacks.find(method);
        if(it == notification_callbacks.end()) {
            log(LogLevel::warn, "unhandled notification: {}", method);
            return;
        }
        // A notification has no answer to carry the failure, so it is
        // logged; the peer goes on reading.
        KOTA_TRY {
            it->second(params);
        }
        KOTA_CATCH_ALL() {
            log(LogLevel::error, "notification handler for {} threw", method);
        }
    }

    void dispatch_request(const std::string& method,
                          const protocol::RequestID& id,
                          std::string_view params,
                          task_group<>& handlers) {
        log(LogLevel::debug, "request: {} id={}", method, id);

        if(incoming_requests.contains(id)) {
            send_error(id, Error(protocol::ErrorCode::InvalidRequest, "duplicate request id"));
            return;
        }

        auto it = request_callbacks.find(method);
        if(it == request_callbacks.end()) {
            send_error(id,
                       Error(protocol::ErrorCode::MethodNotFound, "method not found: " + method));
            return;
        }

        auto callback = it->second;
        auto cancel_source = std::make_shared<cancellation_source>();
        incoming_requests.insert_or_assign(id, cancel_source);
        if(!handlers.spawn(run_request(id,
                                       std::move(callback),
                                       std::string(params),
                                       cancel_source->token()))) {
            // The handlers are being cancelled: run() is ending.
            incoming_requests.erase(id);
            send_error(id, Error(protocol::ErrorCode::RequestCancelled, "request cancelled"));
        }
    }

    task<> run_request(protocol::RequestID id,
                       RequestCallback callback,
                       std::string params,
                       cancellation_token token) {
        outcome<std::string, Error, cancellation> guarded_result = outcome_error(Error());
        // A handler that throws is answered InternalError; the exception
        // does not reach the other handlers, nor run().
        KOTA_TRY {
            guarded_result = co_await with_token(callback(id, params, token), token);
        }
        KOTA_CATCH_ALL() {
            guarded_result =
                outcome_error(Error(protocol::ErrorCode::InternalError, "request handler threw"));
        }
        incoming_requests.erase(id);

        if(guarded_result.is_cancelled()) {
            send_error(id, Error(protocol::ErrorCode::RequestCancelled, "request cancelled"));
            co_return;
        }

        if(guarded_result.has_error()) {
            send_error(id, guarded_result.error());
            co_return;
        }

        auto response = codec.encode_success_response(id, *guarded_result);
        if(!response) {
            send_error(id, Error(protocol::ErrorCode::InternalError, response.error().message));
            co_return;
        }

        enqueue_outgoing(std::move(*response));
    }

    void dispatch_incoming_message(std::string_view payload, task_group<>& handlers) {
        log(LogLevel::trace, "recv: {}", payload);
        auto msg = codec.parse_message(payload);
        std::visit(
            [&](auto& m) {
                using T = std::remove_cvref_t<decltype(m)>;
                if constexpr(std::is_same_v<T, IncomingRequest>) {
                    dispatch_request(m.method, m.id, m.params, handlers);
                } else if constexpr(std::is_same_v<T, IncomingNotification>) {
                    dispatch_notification(m.method, m.params);
                } else if constexpr(std::is_same_v<T, IncomingResponse>) {
                    complete_pending_request(m.id, Result<std::string>(std::move(m.result)));
                } else if constexpr(std::is_same_v<T, IncomingErrorResponse>) {
                    // An error that answers no request is not answered in
                    // turn, or two peers would trade such errors for good.
                    if(m.id) {
                        complete_pending_request(*m.id, outcome_error(std::move(m.error)));
                    } else {
                        log(LogLevel::warn, "error response without an id: {}", m.error.message);
                    }
                } else if constexpr(std::is_same_v<T, IncomingParseError>) {
                    send_error(m.id, m.error);
                }
            },
            msg);
    }
};

// ---------------------------------------------------------------------------
// Peer<CodecT> non-template methods
// ---------------------------------------------------------------------------

template <typename CodecT>
Peer<CodecT>::Peer(event_loop& loop, std::unique_ptr<Transport> transport, CodecT codec) :
    self(std::make_unique<Self>(loop, std::move(transport), std::move(codec))) {
    assert(self->transport && "Peer requires a transport");
}

template <typename CodecT>
Peer<CodecT>::~Peer() = default;

template <typename CodecT>
task<> Peer<CodecT>::run() {
    assert(!self->started && "Peer::run() is called once");
    self->started = true;

    task_group<> handlers(self->loop);

    // Pending requests fail as soon as the input ends, before the handlers
    // still running finish and before run() returns. A connection that went
    // away and a frame that cannot be read both fail them with
    // RequestFailed.
    auto read_loop = [&]() -> task<> {
        auto ended = co_await self->read_loop(handlers).catch_cancel();
        const bool malformed = ended.has_value() && ended->kind == ReadError::Kind::Malformed;
        self->end_input(Error(malformed ? ended->message : "transport closed"));
        if(ended.is_cancelled()) {
            handlers.cancel();
        }
        co_await handlers.join();
        self->answers_done = true;
        self->write_event.set();
    };

    co_await when_all(read_loop(), self->write_loop());
}

template <typename CodecT>
Result<void> Peer<CodecT>::close() {
    if(self->closed) {
        return {};
    }

    self->closed = true;
    self->input_open = false;
    self->output_open = false;
    self->closing_output = false;
    self->log(LogLevel::info, "peer closing");
    self->cancel_handlers();

    self->fail_pending_requests(Error("peer closed"));
    self->outgoing_queue.clear();
    self->write_event.set();

    // Ends a pending read, and with it the read loop.
    return self->transport->close();
}

template <typename CodecT>
void Peer<CodecT>::close_output() {
    if(self->output_open && !self->closing_output) {
        self->closing_output = true;
        self->write_event.set();
    }
}

template <typename CodecT>
void Peer<CodecT>::set_logger(LogCallback callback, LogLevel min_level) {
    self->logger = std::move(callback);
    self->min_level = min_level;
}

template <typename CodecT>
task<std::string, Error> Peer<CodecT>::send_request_impl(std::string_view method,
                                                         std::string params,
                                                         request_options opts) {
    using Ending = typename Self::Ending;

    if(opts.timeout && *opts.timeout <= std::chrono::milliseconds::zero()) {
        co_await fail(protocol::ErrorCode::RequestCancelled, "request timed out");
    }
    if(auto unsendable = self->unsendable(true)) {
        co_await fail(std::move(*unsendable));
    }
    if(opts.token && opts.token->cancelled()) {
        co_await fail(protocol::ErrorCode::RequestCancelled, "request cancelled");
    }

    protocol::RequestID id{self->next_request_id++};
    auto encoded = self->codec.encode_request(id, method, params);
    if(!encoded) {
        co_await fail(encoded.error());
    }

    auto pending = std::make_shared<typename Self::PendingRequest>();
    self->pending_requests.emplace(id, pending);
    self->enqueue_outgoing(std::move(*encoded));

    // The answer, the caller's cancellation or the deadline, whichever comes
    // first; the others are cancelled with the wait.
    std::vector<task<Ending>> waits;
    waits.push_back(Self::answered(pending));
    if(opts.token) {
        waits.push_back(Self::cancelled(*opts.token));
    }
    if(opts.timeout) {
        waits.push_back(Self::expired(*opts.timeout, self->loop));
    }
    auto ending = (co_await when_any(std::move(waits))).second;

    // An answer that came in the same turn as the cancellation still counts.
    if(!pending->response) {
        self->pending_requests.erase(id);
        self->send_cancel_request(id);
        co_await fail(protocol::ErrorCode::RequestCancelled,
                      ending == Ending::TimedOut ? "request timed out" : "request cancelled");
    }
    co_return co_await or_fail(std::move(*pending->response));
}

template <typename CodecT>
Result<void> Peer<CodecT>::send_notification_impl(std::string_view method, std::string params) {
    if(auto unsendable = self->unsendable(false)) {
        return outcome_error(std::move(*unsendable));
    }

    auto notification_encoded = self->codec.encode_notification(method, params);
    if(!notification_encoded) {
        return outcome_error(notification_encoded.error());
    }

    self->enqueue_outgoing(std::move(*notification_encoded));
    return {};
}

// ---------------------------------------------------------------------------
// Peer<CodecT> template methods
// ---------------------------------------------------------------------------

template <typename CodecT>
template <typename Params>
RequestResult<Params> Peer<CodecT>::send_request(const Params& params, request_options opts) {
    static_assert(detail::has_request_traits_v<Params>,
                  "send_request(params) requires RequestTraits<Params>");
    using Traits = protocol::RequestTraits<Params>;
    return send_request<typename Traits::Result>(Traits::method, params, std::move(opts));
}

template <typename CodecT>
template <typename ResultT, typename Params>
task<ResultT, Error> Peer<CodecT>::send_request(std::string_view method,
                                                const Params& params,
                                                request_options opts) {
    auto serialized_params = co_await or_fail(self->codec.serialize_value(params));
    auto raw_result =
        co_await send_request_impl(method, std::move(serialized_params), std::move(opts)).or_fail();
    co_return co_await or_fail(self->template read_result<ResultT>(std::move(raw_result)));
}

template <typename CodecT>
template <typename Params>
Result<void> Peer<CodecT>::send_notification(const Params& params) {
    static_assert(detail::has_notification_traits_v<Params>,
                  "send_notification(params) requires NotificationTraits<Params>");
    return send_notification(protocol::NotificationTraits<Params>::method, params);
}

template <typename CodecT>
template <typename Params>
Result<void> Peer<CodecT>::send_notification(std::string_view method, const Params& params) {
    auto serialized_params = self->codec.serialize_value(params);
    if(!serialized_params) {
        return outcome_error(serialized_params.error());
    }
    return send_notification_impl(method, std::move(*serialized_params));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_request(Callback&& callback) {
    detail::validate_request_callback_signature<Callback, Peer>();

    using Params = detail::callback_param_t<Callback, 1>;
    static_assert(detail::has_request_traits_v<Params>,
                  "on_request(callback) requires RequestTraits<Params>");

    using Ret = detail::callback_return_t<Callback>;
    static_assert(
        std::is_same_v<Ret, RequestResult<Params>> ||
            std::is_same_v<Ret, task<codec::RawValue, Error>>,
        "request callback return type should be RequestResult<Params> " "or task<codec::RawValue, Error>");

    on_request(protocol::RequestTraits<Params>::method, std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_request(std::string_view method, Callback&& callback) {
    detail::validate_request_callback_signature<Callback, Peer>();

    using Params = detail::callback_param_t<Callback, 1>;
    auto wrapped = [cb = std::forward<Callback>(callback),
                    method_name = std::string(method),
                    peer = this](const protocol::RequestID& request_id,
                                 std::string_view params_raw,
                                 cancellation_token token) -> task<std::string, Error> {
        auto& state = *peer->self;
        auto parsed_params =
            state.codec.template deserialize_value<Params>(params_raw,
                                                          protocol::ErrorCode::InvalidParams);
        if(!parsed_params) {
            state.log(LogLevel::warn,
                     "request '{}' params deserialization failed: {}",
                     method_name,
                     parsed_params.error().message);
            co_await fail(parsed_params.error());
        }

        RequestContext context(*peer, method_name, request_id, std::move(token));
        auto result = co_await std::invoke(cb, context, *parsed_params).or_fail();
        // A RawValue result is already in the codec's encoding; read_result
        // takes it back as it is.
        if constexpr(std::is_same_v<decltype(result), codec::RawValue>) {
            co_return std::move(result.data);
        } else {
            auto serialized = state.codec.serialize_value(result);
            if(!serialized) {
                co_await fail(Error(protocol::ErrorCode::InternalError, serialized.error().message));
            }
            co_return std::move(*serialized);
        }
    };

    self->request_callbacks.insert_or_assign(std::string(method), std::move(wrapped));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_notification(Callback&& callback) {
    detail::validate_notification_callback_signature<Callback>();

    using Params = detail::callback_param_t<Callback, 0>;
    static_assert(detail::has_notification_traits_v<Params>,
                  "on_notification(callback) requires NotificationTraits<Params>");

    on_notification(protocol::NotificationTraits<Params>::method, std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_notification(std::string_view method, Callback&& callback) {
    detail::validate_notification_callback_signature<Callback>();

    using Params = detail::callback_param_t<Callback, 0>;
    auto wrapped = [cb = std::forward<Callback>(callback), peer = this](std::string_view params_raw) {
        auto& state = *peer->self;
        auto parsed_params = state.codec.template deserialize_value<Params>(params_raw);
        if(!parsed_params) {
            state.log(LogLevel::warn,
                     "notification params deserialization failed: {}",
                     parsed_params.error().message);
            return;
        }
        std::invoke(cb, *parsed_params);
    };

    self->notification_callbacks.insert_or_assign(std::string(method), std::move(wrapped));
}

}  // namespace kota::ipc
