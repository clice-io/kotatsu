#pragma once

#ifndef KOTA_IPC_PEER_INL_FROM_HEADER
#include "kota/ipc/peer.h"
#endif

#include <cassert>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
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
#include <variant>
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
    /// run() has started and not returned, cancelled or not: the Peer must
    /// not go.
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

    /// Why `payload` is not sent, if it is larger than the transport
    /// carries: the remote would skip it unread.
    std::optional<Error> oversized(std::string_view payload) const {
        const auto limit = transport->max_payload();
        if(payload.size() <= limit) {
            return std::nullopt;
        }
        return Error(protocol::ErrorCode::MessageTooLarge,
                     std::format("a message of {} bytes exceeds the limit of {} bytes",
                                 payload.size(),
                                 limit));
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

            auto payload = std::move(outgoing_queue.front());
            outgoing_queue.pop_front();
            auto written = co_await transport->write_message(payload).catch_cancel();
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
            enqueue_response(id, std::move(*response));
        }
    }

    /// Queues `response`, the answer to `id`. One larger than the transport
    /// carries is replaced by a MessageTooLarge error without data, which the
    /// remote can read; the handler that answered never learns of it.
    void enqueue_response(const std::optional<protocol::RequestID>& id, std::string response) {
        if(auto too_large = oversized(response)) {
            log(LogLevel::warn, "response replaced: {}", too_large->message);
            // An error without data always encodes.
            response = *codec.encode_error_response(id, *too_large);
        }
        enqueue_outgoing(std::move(response));
    }

    /// Tells the remote that the request `id` is no longer awaited. It is a
    /// courtesy: one the codec cannot encode is logged and not sent.
    void send_cancel_request(const protocol::RequestID& id) {
        auto params = codec.serialize_value(protocol::CancelRequestParams{id});
        if(!params) {
            log(LogLevel::error,
                "$/cancelRequest for id={} not sent: {}",
                protocol::to_string(id),
                params.error().message);
            return;
        }
        auto notification = codec.encode_notification("$/cancelRequest", *params);
        if(!notification) {
            log(LogLevel::error,
                "$/cancelRequest for id={} not sent: {}",
                protocol::to_string(id),
                notification.error().message);
            return;
        }
        enqueue_outgoing(std::move(*notification));
    }

    /// Waits for `signal`: a task, whose cancellation its awaiter can catch.
    static task<> wait_for(event& signal) {
        co_await signal.wait();
    }

    static task<Ending> answered(std::shared_ptr<PendingRequest> pending) {
        co_await pending->ready.wait();
        co_return Ending::Answered;
    }

    static task<Ending> expired(std::chrono::milliseconds timeout, event_loop& loop) {
        co_await sleep(timeout, loop);
        co_return Ending::TimedOut;
    }

    /// What a request awaits once it is sent: its answer. A cancel of the
    /// awaiting task does not end this wait at once, as it ends others: it
    /// sends the remote $/cancelRequest and the wait goes on, and the task
    /// ends cancelled once it is over, as queue() waits for its work. The
    /// token firing sends $/cancelRequest too, and the request then gives
    /// what the remote answers. The wait is over once the answer is in, once
    /// the pending requests fail, or once the timeout passes, which fails the
    /// request.
    ///
    /// A watcher ends the wait: a task in a group the wait owns and nobody
    /// joins, so that it starts at once and a cancel of the request never
    /// reaches it. An event or a timer wakes it, so the request resumes from
    /// there, never inside the call that answered or failed it.
    struct AnswerWait : io_op {
        Self& self;
        protocol::RequestID id;
        std::shared_ptr<PendingRequest> pending;
        request_options opts;
        /// $/cancelRequest is queued.
        bool cancel_sent = false;
        task_group<> watcher;

        AnswerWait(Self& self,
                   protocol::RequestID id,
                   std::shared_ptr<PendingRequest> pending,
                   request_options opts) :
            self(self), id(std::move(id)), pending(std::move(pending)), opts(std::move(opts)) {
            action = [](io_op* op) {
                static_cast<AnswerWait*>(op)->send_cancel();
            };
        }

        bool await_ready() const noexcept {
            return false;
        }

        template <typename Promise>
        std::coroutine_handle<>
            await_suspend(std::coroutine_handle<Promise> waiting,
                          std::source_location location = std::source_location::current()) noexcept {
            // The watcher suspends at once: nothing it waits for has come.
            watcher.spawn(watch(*this));
            return attach(waiting.promise(), location);
        }

        void await_resume() const noexcept {}

        /// Tells the remote, once, that the request is no longer awaited,
        /// unless its answer is in.
        void send_cancel() {
            if(cancel_sent || pending->response) {
                return;
            }
            cancel_sent = true;
            self.send_cancel_request(id);
        }

        /// Ends `wait`, which is the last thing it does: the request may end
        /// there, and `wait` and its group with it, which lets the watcher go
        /// to end on its own.
        static task<> watch(AnswerWait& wait) {
            std::vector<task<Ending>> waits;
            waits.push_back(answered(wait.pending));
            if(wait.opts.token) {
                waits.push_back(cancel_on(wait, *wait.opts.token));
            }
            if(wait.opts.timeout) {
                waits.push_back(expired(*wait.opts.timeout, wait.self.loop));
            }
            auto first = co_await when_any(std::move(waits));
            // An answer that came in the same turn still counts; one that
            // comes later is dropped.
            if(first.second == Ending::TimedOut && !wait.pending->response) {
                wait.send_cancel();
                wait.self.pending_requests.erase(wait.id);
                wait.pending->response =
                    outcome_error(Error(protocol::ErrorCode::RequestCancelled, "request timed out"));
            }
            wait.complete();
        }

        /// Sends $/cancelRequest once `token` fires, then waits for the
        /// answer as answered() does.
        static task<Ending> cancel_on(AnswerWait& wait, cancellation_token token) {
            co_await token.wait().catch_cancel();
            // Resumed as well when the wait ends before the token fires.
            if(token.cancelled()) {
                wait.send_cancel();
            }
            co_await wait.pending->ready.wait();
            co_return Ending::Answered;
        }
    };

    /// Whether `id` is one this peer gave a request of its own.
    bool issued(const protocol::RequestID& id) const {
        const auto* number = std::get_if<std::int64_t>(&id);
        return number != nullptr && *number >= 1 && *number < next_request_id;
    }

    void complete_pending_request(const protocol::RequestID& id, Result<std::string>&& response) {
        auto it = pending_requests.find(id);
        if(it == pending_requests.end()) {
            // The answer to a request that timed out, or that failed with a
            // message too large to read, may still come; nothing awaits it.
            if(issued(id)) {
                log(LogLevel::debug, "late response for id={}", protocol::to_string(id));
            } else {
                log(LogLevel::warn, "orphan response for id={}", protocol::to_string(id));
            }
            return;
        }

        log(LogLevel::debug, "response received for id={}", protocol::to_string(id));

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
        log(LogLevel::debug, "request: {} id={}", method, protocol::to_string(id));

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

    /// Runs `handler` once every message read with its request is
    /// dispatched: a notification that came with the request reaches its
    /// handler first, and a $/cancelRequest that came with it fires the
    /// token, which keeps the handler from starting.
    task<std::string, Error> after_read(task<std::string, Error> handler) {
        co_await yield(loop);
        co_return co_await std::move(handler).or_fail();
    }

    task<> run_request(protocol::RequestID id,
                       RequestCallback callback,
                       std::string params,
                       cancellation_token token) {
        outcome<std::string, Error, cancellation> guarded_result = outcome_error(Error());
        // A handler that throws is answered InternalError; the exception
        // does not reach the other handlers, nor run().
        KOTA_TRY {
            guarded_result = co_await with_token(after_read(callback(id, params, token)), token);
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

        enqueue_response(id, std::move(*response));
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
    assert(!self->running && "Peer destroyed while its run() runs: destroy it once run() returned");
}

template <typename CodecT>
task<> Peer<CodecT>::run() {
    assert(!self->started && "Peer::run() is called once");
    self->started = true;
    self->running = true;
    // Cleared as run() ends, however it ends, before the task awaiting it
    // resumes: its owner may destroy the peer then.
    struct Running {
        Self& self;

        ~Running() {
            self.running = false;
        }
    } running{*self};

    task_group<> handlers;

    // Pending requests fail as soon as the input ends, before the handlers
    // still running finish and before run() returns. A connection that went
    // away and a frame that cannot be read both fail them with
    // ConnectionClosed.
    auto read_loop = [&]() -> task<> {
        auto ended = co_await self->read_loop(handlers).catch_cancel();
        const bool malformed = ended.has_value() && ended->kind == ReadError::Kind::Malformed;
        self->end_input(Error(protocol::ErrorCode::ConnectionClosed,
                              malformed ? ended->message : "transport closed"));
        if(ended.is_cancelled()) {
            handlers.cancel();
        }
        co_await handlers.join();
        self->answers_done = true;
        self->write_event.set();
    };

    auto loops = [&]() -> task<> {
        co_await when_all(read_loop(), self->write_loop());
    };
    // A cancel caught here lets run() reach its end, where `running` is
    // cleared; it was cancelled first, so it still ends cancelled.
    co_await loops().catch_cancel();
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

    self->fail_pending_requests(Error(protocol::ErrorCode::ConnectionClosed, "peer closed"));
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
    if(auto too_large = self->oversized(*encoded)) {
        co_await fail(std::move(*too_large));
    }

    auto pending = std::make_shared<typename Self::PendingRequest>();
    self->pending_requests.emplace(id, pending);
    self->enqueue_outgoing(std::move(*encoded));

    // Over only once the response is in, a timeout's included; a cancel of
    // this task ends it cancelled then.
    co_await typename Self::AnswerWait(*self, id, pending, std::move(opts));
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
    if(auto too_large = self->oversized(*notification_encoded)) {
        return outcome_error(std::move(*too_large));
    }

    self->enqueue_outgoing(std::move(*notification_encoded));
    return {};
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
