#include "kota/async/io/fs_event.h"

#include <cassert>

#include "awaiter.h"
#include "kota/async/io/loop.h"
#include "kota/async/vocab/error.h"

namespace kota {

struct fs_event::Self :
    uv::handle<fs_event::Self, uv_fs_event_t>,
    uv::latest_value_delivery<fs_event::change> {
    uv_fs_event_t handle{};
};

namespace {

static result<unsigned int> to_uv_fs_event_flags(const fs_event::watch_options& options) {
    unsigned int out = 0;
#ifdef UV_FS_EVENT_WATCH_ENTRY
    if(options.watch_entry) {
        out |= UV_FS_EVENT_WATCH_ENTRY;
    }
#else
    if(options.watch_entry) {
        return outcome_error(error::function_not_implemented);
    }
#endif
#ifdef UV_FS_EVENT_STAT
    if(options.stat) {
        out |= UV_FS_EVENT_STAT;
    }
#else
    if(options.stat) {
        return outcome_error(error::function_not_implemented);
    }
#endif
#ifdef UV_FS_EVENT_RECURSIVE
    if(options.recursive) {
        out |= UV_FS_EVENT_RECURSIVE;
    }
#else
    if(options.recursive) {
        return outcome_error(error::function_not_implemented);
    }
#endif
    return out;
}

static fs_event::change_flags to_fs_change_flags(int events) {
    fs_event::change_flags out{};
#ifdef UV_RENAME
    if((events & UV_RENAME) != 0) {
        out.rename = true;
    }
#endif
#ifdef UV_CHANGE
    if((events & UV_CHANGE) != 0) {
        out.change = true;
    }
#endif
    return out;
}

struct fs_event_await : uv::await_op<fs_event_await> {
    using await_base = uv::await_op<fs_event_await>;
    using promise_t = task<fs_event::change, error>::promise_type;

    fs_event::Self* self;
    result<fs_event::change> outcome = outcome_error(error());

    explicit fs_event_await(fs_event::Self* watcher) : self(watcher) {}

    static void on_cancel(system_op* op) {
        await_base::complete_cancel(op, [](auto& aw) {
            if(aw.self) {
                aw.self->disarm();
            }
        });
    }

    static void on_change(uv_fs_event_t* handle, const char* filename, int events, int status) {
        auto* watcher = static_cast<fs_event::Self*>(handle->data);
        assert(watcher != nullptr && "on_change requires watcher state in handle->data");

        if(auto err = uv::status_to_error(status)) {
            watcher->deliver(err);
            return;
        }

        fs_event::change c{};
        if(filename) {
            c.path = filename;
        }
        c.flags = to_fs_change_flags(events);

        watcher->deliver(std::move(c));
    }

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_t> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        if(!self) {
            return waiting;
        }
        self->arm(*this, outcome);
        return this->link_continuation(&waiting.promise(), location);
    }

    result<fs_event::change> await_resume() noexcept {
        if(self) {
            self->disarm();
        }
        return std::move(outcome);
    }
};

}  // namespace

fs_event::fs_event() noexcept = default;

fs_event::fs_event(unique_handle<Self> self) noexcept : self(std::move(self)) {}

fs_event::~fs_event() = default;

fs_event::fs_event(fs_event&& other) noexcept = default;

fs_event& fs_event::operator=(fs_event&& other) noexcept = default;

fs_event::Self* fs_event::operator->() noexcept {
    return self.get();
}

result<fs_event> fs_event::create(event_loop& loop) {
    auto self = Self::make();
    if(auto err = uv::fs_event_init(loop, self->handle)) {
        return outcome_error(err);
    }

    return fs_event(std::move(self));
}

error fs_event::start(const char* path, watch_options options) {
    if(!self) {
        return error::invalid_argument;
    }

    auto uv_flags = to_uv_fs_event_flags(options);
    if(!uv_flags) {
        return uv_flags.error();
    }

    auto& handle = self->handle;
    if(auto err = uv::fs_event_start(handle, fs_event_await::on_change, path, uv_flags.value())) {
        return err;
    }

    return {};
}

error fs_event::stop() {
    if(!self) {
        return error::invalid_argument;
    }

    auto& handle = self->handle;
    if(auto err = uv::fs_event_stop(handle)) {
        return err;
    }

    return {};
}

task<fs_event::change, error> fs_event::wait() {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(self->has_pending()) {
        co_return self->take_pending();
    }

    if(self->has_waiter()) {
        co_await fail(error::connection_already_in_progress);
    }

    co_return co_await fs_event_await{self.get()};
}

}  // namespace kota
