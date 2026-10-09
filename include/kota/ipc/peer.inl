#pragma once

#ifndef KOTA_IPC_PEER_INL_FROM_HEADER
#include "kota/ipc/peer.h"
#endif

#include <algorithm>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "kota/support/function_traits.h"

namespace kota::ipc::detail {

/// A request id as Peer's log writes it: a number as it is, a string quoted.
struct LoggedId {
    const protocol::RequestID& id;
};

}  // namespace kota::ipc::detail

template <>
struct std::formatter<kota::ipc::detail::LoggedId> {
    constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const kota::ipc::detail::LoggedId& logged, std::format_context& ctx) const {
        if(const auto* text = std::get_if<std::string>(&logged.id)) {
            return std::format_to(ctx.out(), "\"{}\"", *text);
        }
        return std::format_to(ctx.out(), "{}", std::get<std::int64_t>(logged.id));
    }
};

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

/// Traits of a method that takes no params, keyed by an empty structure.
template <typename Traits>
constexpr bool takes_no_params_v = requires { requires !Traits::takes_params; };

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

template <typename CodecT>
struct Peer<CodecT>::Self {
    /// The params come as the codec read them from the message, which it
    /// decodes them from in place. A request's callback returns its answer,
    /// encoded.
    using RequestCallback = std::function<
        task<std::string, Error>(const protocol::RequestID&, PayloadSlice&, cancellation_token)>;
    using NotificationCallback = std::function<void(PayloadSlice&)>;

    using Clock = std::chrono::steady_clock;

    struct PendingRequest;

    /// When each request with a timeout times out.
    using Deadlines = std::multimap<Clock::time_point, PendingRequest*>;

    /// A request sent and not settled yet, which its sender awaits. A cancel
    /// of the sender tells the remote and goes on waiting: the answer, a
    /// failure of the connection or the deadline settles it.
    struct PendingRequest : io_op {
        Self& peer;
        protocol::RequestID id;
        /// What settled it. Once set, nothing touches the Peer again, which
        /// may be gone by then.
        std::optional<Result<PayloadSlice>> response;
        /// Its entry in `deadlines`, while it has one.
        std::optional<typename Deadlines::iterator> deadline;
        /// The remote was told that the request is no longer awaited.
        bool cancel_sent = false;

        PendingRequest(Self& peer, protocol::RequestID id) : peer(peer), id(std::move(id)) {
            action = [](io_op* op) {
                static_cast<PendingRequest*>(op)->cancel_remote();
            };
        }

        PendingRequest(const PendingRequest&) = delete;
        PendingRequest& operator=(const PendingRequest&) = delete;

        /// One its sender left unsettled, as an exception unwound it before
        /// it waited, takes itself off the Peer.
        ~PendingRequest() {
            if(!response) {
                peer.forget(*this);
            }
        }

        bool await_ready() const noexcept {
            return false;
        }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<Promise> waiting,
            std::source_location location = std::source_location::current()) noexcept {
            return attach(waiting.promise(), location);
        }

        void await_resume() const noexcept {}

        /// Tells the remote, once, that the request is no longer awaited, and
        /// goes on waiting for its answer; when the remote cannot be told,
        /// it gives up at once instead.
        void cancel_remote() {
            if(response || cancel_sent) {
                return;
            }
            cancel_sent = true;
            if(!peer.send_cancel_request(id)) {
                peer.settle(*this, outcome_error(Error(protocol::ErrorCode::RequestCancelled,
                                                       "request cancelled")));
            }
        }
    };

    event_loop& loop;
    std::unique_ptr<Transport> transport;
    CodecT codec;

    std::vector<std::string> outgoing_queue;
    std::int64_t next_request_id = 1;

    std::unordered_map<std::string, RequestCallback> request_callbacks;
    std::unordered_map<std::string, NotificationCallback> notification_callbacks;

    std::unordered_map<protocol::RequestID, PendingRequest*> pending_requests;
    Deadlines deadlines;
    /// A deadline earlier than the others was filed, or the input ended.
    event deadline_changed;

    /// A request whose handler answers it, from its dispatch to its answer.
    struct RunningRequest {
        /// The key of its handler's entry in request_callbacks, which is
        /// never erased.
        std::string_view method;
        /// Shared, so that it outlives the entry: its cancel() may end the
        /// handler, which erases the entry.
        std::shared_ptr<cancellation_source> source;
    };

    std::unordered_map<protocol::RequestID, RunningRequest> incoming_requests;

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
    /// run() is running: the Peer must not go.
    bool running = false;
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

    /// The request `method` with `params`, as `id`, encoded; one for a
    /// method that takes no params carries none.
    template <typename Params>
    Result<std::string> encode_request(const protocol::RequestID& id,
                                       std::string_view method,
                                       const Params& params) {
        if constexpr(detail::takes_no_params_v<protocol::RequestTraits<Params>>) {
            return codec.encode_request(id, method);
        } else {
            return codec.encode_request(id, method, params);
        }
    }

    /// The notification `method` with `params`, encoded, as encode_request.
    template <typename Params>
    Result<std::string> encode_notification(std::string_view method, const Params& params) {
        if constexpr(detail::takes_no_params_v<protocol::NotificationTraits<Params>>) {
            return codec.encode_notification(method);
        } else {
            return codec.encode_notification(method, params);
        }
    }

    /// Why a request cannot be sent now, if it cannot.
    std::optional<Error> refuse_request(const request_options& opts) const {
        if(opts.timeout && *opts.timeout <= std::chrono::milliseconds::zero()) {
            return Error(protocol::ErrorCode::RequestCancelled, "request timed out");
        }
        if(auto unsendable = this->unsendable(true)) {
            return unsendable;
        }
        if(opts.token.cancelled()) {
            return Error(protocol::ErrorCode::RequestCancelled, "request cancelled");
        }
        return std::nullopt;
    }

    /// Why a message cannot be sent now, if it cannot; a request also needs
    /// the input open for its answer.
    std::optional<Error> unsendable(bool expects_answer) const {
        if(closed) {
            return Error(protocol::ErrorCode::ConnectionClosed, "peer closed");
        }
        if(!output_open || closing_output) {
            return Error(protocol::ErrorCode::ConnectionClosed, "peer output closed");
        }
        if(expects_answer && !input_open) {
            return Error(protocol::ErrorCode::ConnectionClosed, "peer input closed");
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

    /// Writes what is queued until the output closes. Once the queue is
    /// empty, it half-closes the transport when close_output() asked for it,
    /// or when nothing more will be written: the input has ended and every
    /// answer is out, and the remote reads the end of its input.
    task<> write_loop() {
        // run() was cancelled from outside: nothing will use the transport
        // again, so it closes, and the remote reads the end.
        bool cancelled = false;
        while(true) {
            if(outgoing_queue.empty()) {
                if(closing_output || (answers_done && output_open)) {
                    co_await finish_output();
                }
                if(!output_open) {
                    break;
                }
                write_event.reset();
                // Cancelled with run(): nothing more is written.
                auto woken = co_await wait_for(write_event).catch_cancel();
                if(woken.is_cancelled()) {
                    cancelled = true;
                    break;
                }
                continue;
            }

            // What is queued goes to the transport at once.
            auto batch = std::exchange(outgoing_queue, {});
            auto written = co_await transport->write_messages(batch).catch_cancel();
            if(written.is_cancelled()) {
                cancelled = true;
                break;
            }
            if(written.has_error()) {
                fail_output("transport write failed", written.error().message);
                break;
            }
        }
        output_open = false;
        if(cancelled && !closed) {
            closed = true;
            if(auto result = transport->close(); !result) {
                log(LogLevel::error, "closing the transport failed: {}", result.error().message);
            }
        }
    }

    /// Half-closes the transport once the queue is written. A half-close
    /// that fails leaves the remote waiting for the end of its input, so it
    /// fails the output as a write does.
    task<> finish_output() {
        closing_output = false;
        output_open = false;
        // Cancelled with run(): the half-close still happens.
        auto closed_output = co_await transport->close_output().catch_cancel();
        if(closed_output.has_error()) {
            fail_output("closing the output failed", closed_output.error().message);
        }
    }

    /// A write or a half-close failed: nothing more can be written or
    /// answered, so every pending request fails and the transport closes,
    /// which ends the read loop too. One that close() caused, by closing the
    /// transport under it, is no failure: close() has done all that.
    void fail_output(std::string_view what, const std::string& message) {
        if(closed) {
            return;
        }
        log(LogLevel::error, "{}: {}", what, message);
        output_open = false;
        closing_output = false;
        outgoing_queue.clear();
        fail_pending_requests(Error(protocol::ErrorCode::ConnectionClosed, message));
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
        auto values =
            incoming_requests | std::views::values | std::views::transform(&RunningRequest::source);
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
        deadline_changed.set();
    }

    /// Runs the read, write and deadline loops until all three have ended.
    task<> serve() {
        task_group<> handlers;

        // Pending requests fail as soon as the input ends, before the
        // handlers still running finish and before run() returns. A
        // connection that went away and a frame that cannot be read both
        // fail them with ConnectionClosed.
        auto reading = [&]() -> task<> {
            auto ended = co_await read_loop(handlers).catch_cancel();
            const bool malformed = ended.has_value() && ended->kind == ReadError::Kind::Malformed;
            end_input(Error(protocol::ErrorCode::ConnectionClosed,
                            malformed ? ended->message : "transport closed"));
            if(ended.is_cancelled()) {
                handlers.cancel();
            }
            co_await handlers.join();
            answers_done = true;
            write_event.set();
        };

        co_await when_all(reading(), write_loop(), deadline_loop());
    }

    /// Reads and dispatches until the input ends, and returns why it ended:
    /// Closed or Malformed.
    task<ReadError> read_loop(task_group<>& handlers) {
        log(LogLevel::info, "read loop started");
        while(true) {
            auto message = co_await transport->read_message();
            if(message) {
                dispatch_incoming_message(std::move(*message), handlers);
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
    /// it answers, and a notification, or a response whose id names no
    /// request, is dropped. Unknown, it could have been any pending request's
    /// answer, so every pending request fails.
    void skip_oversized(const ReadError& skipped) {
        log(LogLevel::warn, "skipped: {}", skipped.message);
        Error too_large(protocol::ErrorCode::MessageTooLarge, skipped.message);
        auto head = codec.peek(skipped.prefix);
        using Kind = MessageHead::Kind;
        if(head.kind == Kind::Request) {
            send_error(head.id, too_large);
        } else if(head.kind == Kind::Response && head.id) {
            complete_pending_request(*head.id, outcome_error(std::move(too_large)));
        } else if(head.kind == Kind::Unknown) {
            fail_pending_requests(too_large);
        }
    }

    void send_error(const std::optional<protocol::RequestID>& id, const Error& error) {
        log(LogLevel::error, "error response: {}", error.message);
        auto response = codec.encode_error_response(id, error);
        if(response) {
            send_answer(id, std::move(*response));
        }
    }

    /// Whether the remote reads `payload`.
    bool fits(std::string_view payload) const {
        return payload.size() <= transport->remote_max_payload();
    }

    /// The error of `what`, a message of `size` bytes over the remote's limit.
    Error too_large(std::string_view what, std::size_t size) const {
        return Error(protocol::ErrorCode::MessageTooLarge,
                     std::format("a {} of {} bytes exceeds the limit of {} bytes",
                                 what,
                                 size,
                                 transport->remote_max_payload()));
    }

    /// Queues `response`, which answers `id`. One over the remote's limit is
    /// replaced by a MessageTooLarge error, itself dropped when even that is
    /// over the limit.
    void send_answer(const std::optional<protocol::RequestID>& id, std::string response) {
        if(fits(response)) {
            enqueue_outgoing(std::move(response));
            return;
        }
        auto error = too_large("response", response.size());
        log(LogLevel::warn, "answered instead: {}", error.message);
        auto replacement = codec.encode_error_response(id, error);
        if(replacement && fits(*replacement)) {
            enqueue_outgoing(std::move(*replacement));
        } else {
            log(LogLevel::error, "dropped an answer: the limit leaves no room for its error");
        }
    }

    /// Sends the notification `method` with `params`; one the remote would
    /// not read fails unsent.
    template <typename Params>
    Result<void> send_notification(std::string_view method, const Params& params) {
        if(auto unsendable = this->unsendable(false)) {
            return outcome_error(std::move(*unsendable));
        }
        auto notification = encode_notification(method, params);
        if(!notification) {
            return outcome_error(notification.error());
        }
        if(!fits(*notification)) {
            return outcome_error(too_large("notification", notification->size()));
        }
        enqueue_outgoing(std::move(*notification));
        return {};
    }

    /// Tells the remote that the request `id` is no longer awaited; false,
    /// and logged, when that cannot be sent.
    bool send_cancel_request(const protocol::RequestID& id) {
        auto sent = send_notification("$/cancelRequest", protocol::CancelRequestParams{id});
        if(!sent) {
            log(LogLevel::error,
                "$/cancelRequest for id={} not sent: {}",
                detail::LoggedId{id},
                sent.error().message);
        }
        return sent.has_value();
    }

    /// Waits for `signal`: a task, whose cancellation its awaiter can catch.
    static task<> wait_for(event& signal) {
        co_await signal.wait();
    }

    /// Takes `pending` off the requests awaiting an answer, and its deadline
    /// with it.
    void forget(PendingRequest& pending) {
        pending_requests.erase(pending.id);
        if(pending.deadline) {
            deadlines.erase(*pending.deadline);
        }
    }

    /// Settles `pending` with `response`, and has its sender resume once
    /// whatever runs has suspended: never inside the read loop.
    void settle(PendingRequest& pending, Result<PayloadSlice> response) {
        forget(pending);
        pending.response = std::move(response);
        pending.complete_deferred(loop);
    }

    /// Files when `pending` times out, waking the deadline loop when that is
    /// before every other deadline.
    void file_deadline(PendingRequest& pending, std::chrono::milliseconds timeout) {
        // A timeout past what the clock counts never passes.
        const auto now = Clock::now();
        const auto room =
            std::chrono::floor<std::chrono::milliseconds>(Clock::time_point::max() - now);
        const auto at = timeout < room ? now + timeout : Clock::time_point::max();
        auto filed = deadlines.emplace(at, &pending);
        pending.deadline = filed;
        if(filed == deadlines.begin()) {
            deadline_changed.set();
        }
    }

    /// Ends the requests whose timeout has passed, telling the remote.
    void expire_due() {
        const auto now = Clock::now();
        while(!deadlines.empty() && deadlines.begin()->first <= now) {
            auto& pending = *deadlines.begin()->second;
            if(!pending.cancel_sent) {
                pending.cancel_sent = true;
                send_cancel_request(pending.id);
            }
            settle(pending,
                   outcome_error(
                       Error(protocol::ErrorCode::RequestCancelled, "request timed out")));
        }
    }

    /// Ends each request whose timeout has passed, until the input ends: no
    /// request is left to time out then.
    task<> deadline_loop() {
        while(input_open) {
            expire_due();
            deadline_changed.reset();
            if(deadlines.empty()) {
                co_await deadline_changed.wait();
                continue;
            }
            auto left = std::chrono::ceil<std::chrono::milliseconds>(deadlines.begin()->first -
                                                                     Clock::now());
            co_await when_any(sleep(std::max(left, std::chrono::milliseconds::zero()), loop),
                              wait_for(deadline_changed));
        }
    }

    void complete_pending_request(const protocol::RequestID& id, Result<PayloadSlice>&& response) {
        auto it = pending_requests.find(id);
        if(it == pending_requests.end()) {
            log(LogLevel::warn, "orphan response for id={}", detail::LoggedId{id});
            return;
        }
        log(LogLevel::debug, "response received for id={}", detail::LoggedId{id});
        settle(*it->second, std::move(response));
    }

    void fail_pending_requests(const Error& error) {
        if(pending_requests.empty()) {
            return;
        }

        log(LogLevel::error,
            "failing {} pending request(s): {}",
            pending_requests.size(),
            error.message);

        // settle() takes each off the map.
        auto values = pending_requests | std::views::values;
        std::vector<PendingRequest*> pending(values.begin(), values.end());
        for(auto* request: pending) {
            settle(*request, outcome_error(error));
        }
    }

    /// Logs that the params of the `kind` (request or notification) `method`
    /// did not decode.
    void log_params_failure(std::string_view kind, std::string_view method, const Error& error) {
        log(LogLevel::warn,
            "{} '{}' params deserialization failed: {}",
            kind,
            method,
            error.message);
    }

    void dispatch_notification(const std::string& method, PayloadSlice& params) {
        log(LogLevel::debug, "notification: {}", method);

        if(method == "$/cancelRequest") {
            auto parsed = codec.template deserialize_value<protocol::CancelRequestParams>(params);
            if(parsed) {
                if(auto it = incoming_requests.find(parsed->id); it != incoming_requests.end()) {
                    auto source = it->second.source;
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
                          PayloadSlice params,
                          task_group<>& handlers) {
        log(LogLevel::debug, "request: {} id={}", method, detail::LoggedId{id});

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
        auto source = std::make_shared<cancellation_source>();
        incoming_requests.emplace(id, RunningRequest{.method = it->first, .source = source});
        if(!handlers.spawn(run_request(id,
                                       std::move(callback),
                                       std::move(params),
                                       source->token()))) {
            // The handlers are being cancelled: run() is ending.
            send_error(id, Error(protocol::ErrorCode::RequestCancelled, "request cancelled"));
            incoming_requests.erase(id);
        }
    }

    task<> run_request(protocol::RequestID id,
                       RequestCallback callback,
                       PayloadSlice params,
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
        if(guarded_result.is_cancelled()) {
            send_error(id, Error(protocol::ErrorCode::RequestCancelled, "request cancelled"));
        } else if(guarded_result.has_error()) {
            send_error(id, guarded_result.error());
        } else {
            send_answer(id, std::move(*guarded_result));
        }
        // Off the list once its answer is queued. Its source goes with the
        // node, once the map is done with it: its end runs the callbacks
        // still registered on the token, which find the request answered.
        auto answered = incoming_requests.extract(id);
    }

    void dispatch_incoming_message(std::string payload, task_group<>& handlers) {
        log(LogLevel::trace, "recv: {}", payload);
        auto msg = codec.parse_message(std::move(payload));
        std::visit(
            [&](auto& m) {
                using T = std::remove_cvref_t<decltype(m)>;
                if constexpr(std::is_same_v<T, IncomingRequest>) {
                    dispatch_request(m.method, m.id, std::move(m.params), handlers);
                } else if constexpr(std::is_same_v<T, IncomingNotification>) {
                    dispatch_notification(m.method, m.params);
                } else if constexpr(std::is_same_v<T, IncomingResponse>) {
                    complete_pending_request(m.id, Result<PayloadSlice>(std::move(m.result)));
                } else if constexpr(std::is_same_v<T, IncomingErrorResponse>) {
                    // An error that answers no request is not answered in
                    // turn, or two peers would trade such errors for good.
                    if(m.id) {
                        complete_pending_request(*m.id, outcome_error(std::move(m.error)));
                    } else {
                        log(LogLevel::warn, "error response without an id: {}", m.error.message);
                    }
                } else if constexpr(std::is_same_v<T, IncomingParseError>) {
                    if(m.notification) {
                        log(LogLevel::warn, "dropped a notification: {}", m.error.message);
                    } else {
                        send_error(m.id, m.error);
                    }
                }
            },
            msg);
    }
};

template <typename CodecT>
Peer<CodecT>::Peer(event_loop& loop, std::unique_ptr<Transport> transport, CodecT codec) :
    self(std::make_unique<Self>(loop, std::move(transport), std::move(codec))) {
    assert(self->transport && "Peer requires a transport");
}

template <typename CodecT>
Peer<CodecT>::~Peer() {
    assert(!self->running && "a Peer destroyed while its run() runs");
    // A request sent before run(), and not answered, ends with the Peer.
    self->fail_pending_requests(Error(protocol::ErrorCode::ConnectionClosed, "peer destroyed"));
}

template <typename CodecT>
task<> Peer<CodecT>::run() {
    assert(!self->started && "Peer::run() is called once");
    self->started = true;
    self->running = true;
    // However serve() ends, run() is done with the Peer once it has.
    outcome<void, void, cancellation> served;
    KOTA_TRY {
        served = co_await self->serve().catch_cancel();
    }
    KOTA_CATCH_ALL() {
        self->running = false;
        KOTA_RETHROW();
    }
    self->running = false;
    if(served.is_cancelled()) {
        co_await cancel();
    }
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
    // The requests first: a handler cancelled below finds its own requests
    // settled, with nothing left to tell the remote.
    self->fail_pending_requests(Error(protocol::ErrorCode::ConnectionClosed, "peer closed"));
    self->cancel_handlers();
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
std::vector<UnansweredRequest> Peer<CodecT>::incoming_requests() const {
    auto requests = self->incoming_requests | std::views::transform([](const auto& entry) {
                        return UnansweredRequest{.method = entry.second.method, .id = entry.first};
                    });
    return {requests.begin(), requests.end()};
}

template <typename CodecT>
task<PayloadSlice, Error> Peer<CodecT>::send_request_impl(protocol::RequestID id,
                                                          std::string request,
                                                          request_options opts) {
    if(!self->fits(request)) {
        co_await fail(self->too_large("request", request.size()));
    }

    typename Self::PendingRequest pending(*self, std::move(id));
    self->pending_requests.emplace(pending.id, &pending);
    if(opts.timeout) {
        self->file_deadline(pending, *opts.timeout);
    }
    self->enqueue_outgoing(std::move(request));

    // The token's cancel tells the remote; its answer still counts.
    auto told = opts.token.on_cancel([&pending] { pending.cancel_remote(); });
    // A cancel of this task does the same, and ends it once the request is
    // settled.
    co_await pending;
    co_return co_await or_fail(std::move(*pending.response));
}

// The typed members: they encode params and decode results for the ones above.

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
    if(auto refused = self->refuse_request(opts)) {
        co_await fail(std::move(*refused));
    }
    protocol::RequestID id{self->next_request_id++};
    auto request = co_await or_fail(self->encode_request(id, method, params));
    auto raw_result =
        co_await send_request_impl(std::move(id), std::move(request), std::move(opts)).or_fail();
    co_return co_await or_fail(self->codec.template deserialize_value<ResultT>(raw_result));
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
    return self->send_notification(method, params);
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

    on_request_impl(protocol::RequestTraits<Params>::method, std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_request(std::string_view method, Callback&& callback) {
    detail::validate_request_callback_signature<Callback, Peer>();
    on_request_impl(method, std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_request_impl(std::string_view method, Callback&& callback) {
    using Params = detail::callback_param_t<Callback, 1>;
    auto wrapped = [cb = std::forward<Callback>(callback),
                    method_name = std::string(method),
                    peer = this](const protocol::RequestID& request_id,
                                 PayloadSlice& params_raw,
                                 cancellation_token token) -> task<std::string, Error> {
        auto& state = *peer->self;
        auto parsed_params =
            state.codec.template deserialize_value<Params>(params_raw,
                                                          protocol::ErrorCode::InvalidParams);
        if(!parsed_params) {
            state.log_params_failure("request", method_name, parsed_params.error());
            co_await fail(parsed_params.error());
        }

        RequestContext context(*peer, method_name, request_id, std::move(token));
        // The callback runs as the request is dispatched; the task it returns
        // starts once the loop has come round, after what was read with the
        // request. A cancel meanwhile, a $/cancelRequest read with the request
        // or close(), ends this coroutine at the yield, and the task never
        // starts.
        auto answering = std::invoke(cb, context, *parsed_params);
        co_await yield(state.loop);
        auto result = co_await std::move(answering).or_fail();
        // A RawValue result is already in the codec's encoding, and goes as
        // it is; deserialize_value takes it back so.
        co_return co_await or_fail(state.codec.encode_success_response(request_id, result));
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

    on_notification_impl(protocol::NotificationTraits<Params>::method,
                         std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_notification(std::string_view method, Callback&& callback) {
    detail::validate_notification_callback_signature<Callback>();
    on_notification_impl(method, std::forward<Callback>(callback));
}

template <typename CodecT>
template <typename Callback>
void Peer<CodecT>::on_notification_impl(std::string_view method, Callback&& callback) {
    using Params = detail::callback_param_t<Callback, 0>;
    auto wrapped = [cb = std::forward<Callback>(callback),
                    method_name = std::string(method),
                    peer = this](PayloadSlice& params_raw) {
        auto& state = *peer->self;
        auto parsed_params = state.codec.template deserialize_value<Params>(params_raw);
        if(!parsed_params) {
            state.log_params_failure("notification", method_name, parsed_params.error());
            return;
        }
        std::invoke(cb, *parsed_params);
    };

    self->notification_callbacks.insert_or_assign(std::string(method), std::move(wrapped));
}

}  // namespace kota::ipc
