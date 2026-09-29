#pragma once

#include <cstddef>
#include <memory>
#include <optional>

#include "../async/io/awaiter.h"
#include "kota/http/detail/curl.h"
#include "kota/http/detail/manager.h"
#include "kota/http/detail/request.h"
#include "kota/http/detail/response.h"
#include "kota/async/runtime/task.h"

namespace kota::http::detail {

/// A request on its way: the curl easy handle set up from it, what curl
/// receives for it, and the task awaiting it. It lives in that task's
/// frame, whose address curl's callbacks keep.
///
/// Its manager tracks it from start() until it resumes or is cancelled.
/// What curl finishes, and what a manager aborts when it goes while its loop
/// lives, completes on a later turn of the loop, so that no task resumes
/// inside the manager. A manager that goes while its loop is being
/// destroyed leaves its transfers to their tasks' cancellation, since that
/// loop runs nothing queued any more.
struct transfer : uv::uv_op<transfer> {
    transfer(http::request request, manager& target) noexcept;

    /// Sends `request` and waits for its response.
    static task<response, error> send(http::request request);

    bool start() noexcept;

    void cancel() noexcept;

    outcome<response, error> await_resume() noexcept;

    http::request request;
    /// The curl share of the request's client on the loop.
    std::shared_ptr<curl::share_handle> share;
    curl::slist header_lines;
    /// Declared after what curl points into, so that it goes first.
    curl::easy_handle easy;
    response out;
    std::optional<error> failure;
    /// The manager it goes to, and that tracks it from start() on; null
    /// once it has left the manager, or the manager has gone.
    manager* owner;
    transfer* prev = nullptr;
    transfer* next = nullptr;
    /// Its completion is queued; curl drives it no more.
    bool queued = false;

private:
    /// What makes `request` impossible to send, found before curl sees it.
    static std::optional<error> check(const http::request& request);

    /// Sets the easy handle up from the request; the error that stops it.
    std::optional<error> setup();

    static std::size_t on_write(char* data, std::size_t size, std::size_t count, void* self);
    static std::size_t on_header(char* data, std::size_t size, std::size_t count, void* self);
    static std::size_t on_read(char* data, std::size_t size, std::size_t count, void* file);
};

}  // namespace kota::http::detail
