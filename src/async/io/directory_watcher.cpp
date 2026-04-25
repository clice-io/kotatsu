#include "kota/async/io/directory_watcher.h"

#include <efsw/efsw.h>

#include "kota/async/io/loop.h"
#include "kota/async/io/watcher.h"
#include "kota/async/runtime/sync.h"

namespace kota {

struct directory_watcher::Self : std::enable_shared_from_this<Self> {
    event_loop* loop;
    efsw_watcher watcher = nullptr;
    timer debounce_timer;
    kota::event has_events{false};
    std::vector<change> buffer;
    std::chrono::milliseconds debounce_ms;
    bool closed = false;

    void on_change(change c) {
        if(closed) {
            return;
        }

        buffer.push_back(std::move(c));
        has_events.set();
    }

    static void efsw_callback(efsw_watcher /*watcher*/,
                              efsw_watchid /*watchid*/,
                              const char* dir,
                              const char* filename,
                              enum efsw_action action,
                              const char* old_filename,
                              void* param) {
        auto* raw = static_cast<Self*>(param);

        change c;

        std::string path = dir;
        if(!path.empty() && path.back() != '/' && path.back() != '\\') {
            path += '/';
        }
        path += filename;
        c.path = std::move(path);

        switch(action) {
            case EFSW_ADD: c.type = effect::create; break;
            case EFSW_DELETE: c.type = effect::destroy; break;
            case EFSW_MODIFIED: c.type = effect::modify; break;
            case EFSW_MOVED: c.type = effect::rename; break;
            default: c.type = effect::other; break;
        }

        if(action == EFSW_MOVED && old_filename) {
            std::string old_path = dir;
            if(!old_path.empty() && old_path.back() != '/' && old_path.back() != '\\') {
                old_path += '/';
            }
            old_path += old_filename;
            c.associated = std::move(old_path);
        }

        c.is_directory = false;

        auto shared = raw->shared_from_this();
        raw->loop->post([shared, c = std::move(c)]() mutable { shared->on_change(std::move(c)); });
    }
};

directory_watcher::directory_watcher() noexcept = default;

directory_watcher::directory_watcher(std::shared_ptr<Self> self) noexcept : self(std::move(self)) {}

directory_watcher::~directory_watcher() {
    close();
}

directory_watcher::directory_watcher(directory_watcher&&) noexcept = default;

directory_watcher& directory_watcher::operator=(directory_watcher&&) noexcept = default;

result<directory_watcher> directory_watcher::create(const char* path,
                                                    options opts,
                                                    event_loop& loop) {
    auto s = std::make_shared<Self>();
    s->loop = &loop;
    s->debounce_ms = opts.debounce;
    s->debounce_timer = timer::create(loop);

    s->watcher = efsw_create(0);
    if(!s->watcher) {
        return outcome_error(error::unknown_error);
    }

    efsw_watchid id = efsw_addwatch(s->watcher, path, Self::efsw_callback, 1, s.get());
    if(id < 0) {
        efsw_release(s->watcher);
        return outcome_error(error::unknown_error);
    }

    efsw_watch(s->watcher);

    return directory_watcher(std::move(s));
}

task<std::vector<directory_watcher::change>, error> directory_watcher::next() {
    if(!self || self->closed) {
        co_await fail(error::invalid_argument);
    }

    while(self->buffer.empty()) {
        self->has_events.reset();
        co_await self->has_events.wait();

        if(self->closed) {
            co_await fail(error::software_caused_connection_abort);
        }
    }

    self->debounce_timer.start(self->debounce_ms);
    co_await self->debounce_timer.wait();

    auto batch = std::move(self->buffer);
    self->buffer.clear();
    co_return batch;
}

void directory_watcher::close() {
    if(!self) {
        return;
    }

    if(self->watcher) {
        efsw_release(self->watcher);
        self->watcher = nullptr;
    }

    self->closed = true;
    self->has_events.set();
    self->debounce_timer.stop();
}

}  // namespace kota
