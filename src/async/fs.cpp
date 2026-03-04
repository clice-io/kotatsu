#include "eventide/async/fs.h"

#include <cassert>
#include <functional>

#include "awaiter.h"
#include "eventide/async/error.h"
#include "eventide/async/loop.h"

namespace eventide {

struct fs_event::Self :
    uv_handle<fs_event::Self, uv_fs_event_t>,
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
        return std::unexpected(error::function_not_implemented);
    }
#endif
#ifdef UV_FS_EVENT_STAT
    if(options.stat) {
        out |= UV_FS_EVENT_STAT;
    }
#else
    if(options.stat) {
        return std::unexpected(error::function_not_implemented);
    }
#endif
#ifdef UV_FS_EVENT_RECURSIVE
    if(options.recursive) {
        out |= UV_FS_EVENT_RECURSIVE;
    }
#else
    if(options.recursive) {
        return std::unexpected(error::function_not_implemented);
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
    using promise_t = task<result<fs_event::change>>::promise_type;

    // Watched fs_event self used to register and clear waiter pointers.
    fs_event::Self* self;
    // Result slot written by on_change() and returned from await_resume().
    result<fs_event::change> outcome = std::unexpected(error());

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

        auto err = uv::status_to_error(status);
        if(err) {
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
        return std::unexpected(err);
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

task<result<fs_event::change>> fs_event::wait() {
    if(!self) {
        co_return std::unexpected(error::invalid_argument);
    }

    if(self->has_pending()) {
        co_return self->take_pending();
    }

    if(self->has_waiter()) {
        co_return std::unexpected(error::connection_already_in_progress);
    }

    co_return co_await fs_event_await{self.get()};
}

namespace {

template <typename Result>
struct fs_op : uv::await_op<fs_op<Result>> {
    using promise_t = task<result<Result>>::promise_type;

    // Underlying libuv fs request; req.data points back to this awaiter.
    uv_fs_t req = {};
    // Extracts a typed result from a successful uv_fs_t completion.
    std::function<Result(uv_fs_t&)> populate;
    // Final result observed by await_resume().
    result<Result> out = std::unexpected(error());

    fs_op() = default;

    static void on_cancel(system_op* op) {
        auto* self = static_cast<fs_op*>(op);
        uv::cancel(self->req);
    }

    bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
        await_suspend(std::coroutine_handle<promise_t> waiting,
                      std::source_location location = std::source_location::current()) noexcept {
        return this->link_continuation(&waiting.promise(), location);
    }

    result<Result> await_resume() noexcept {
        return std::move(out);
    }
};

static fs::dirent::type map_dirent(uv_dirent_type_t t) {
    switch(t) {
        case UV_DIRENT_FILE: return fs::dirent::type::file;
        case UV_DIRENT_DIR: return fs::dirent::type::dir;
        case UV_DIRENT_LINK: return fs::dirent::type::link;
        case UV_DIRENT_FIFO: return fs::dirent::type::fifo;
        case UV_DIRENT_SOCKET: return fs::dirent::type::socket;
        case UV_DIRENT_CHAR: return fs::dirent::type::char_device;
        case UV_DIRENT_BLOCK: return fs::dirent::type::block_device;
        default: return fs::dirent::type::unknown;
    }
}

static result<int> to_uv_copyfile_flags(const fs::copyfile_options& options) {
    unsigned int out = 0;
#ifdef UV_FS_COPYFILE_EXCL
    if(options.excl) {
        out |= UV_FS_COPYFILE_EXCL;
    }
#else
    if(options.excl) {
        return std::unexpected(error::function_not_implemented);
    }
#endif
#ifdef UV_FS_COPYFILE_FICLONE
    if(options.clone) {
        out |= UV_FS_COPYFILE_FICLONE;
    }
#else
    if(options.clone) {
        return std::unexpected(error::function_not_implemented);
    }
#endif
#ifdef UV_FS_COPYFILE_FICLONE_FORCE
    if(options.clone_force) {
        out |= UV_FS_COPYFILE_FICLONE_FORCE;
    }
#else
    if(options.clone_force) {
        return std::unexpected(error::function_not_implemented);
    }
#endif
    return static_cast<int>(out);
}

template <typename Result, typename Submit, typename Populate>
static task<result<Result>> run_fs(Submit submit,
                                   Populate populate,
                                   event_loop& loop = event_loop::current()) {
    fs_op<Result> op;
    op.populate = populate;

    auto after_cb = [](uv_fs_t* req) {
        auto* h = static_cast<fs_op<Result>*>(req->data);
        assert(h != nullptr && "fs after_cb requires operation in req->data");

        h->mark_cancelled_if(req->result);

        if(req->result < 0) {
            h->out = std::unexpected(uv::status_to_error(req->result));
        } else {
            h->out = h->populate(*req);
        }

        uv::fs_req_cleanup(*req);

        h->complete();
    };

    op.req.data = &op;

    if(auto err = submit(op.req, after_cb)) {
        co_return std::unexpected(err);
    }

    co_return co_await op;
}

static fs::result basic_populate(uv_fs_t& req) {
    fs::result r{};
    r.value = req.result;
    if(req.path) {
        r.path = req.path;
    }
    return r;
}

}  // namespace

fs::dir_handle::dir_handle(dir_handle&& other) noexcept : dir(other.dir) {
    other.dir = nullptr;
}

fs::dir_handle& fs::dir_handle::operator=(dir_handle&& other) noexcept {
    if(this != &other) {
        dir = other.dir;
        other.dir = nullptr;
    }
    return *this;
}

fs::dir_handle::dir_handle(void* ptr) : dir(ptr) {}

bool fs::dir_handle::valid() const noexcept {
    return dir != nullptr;
}

void* fs::dir_handle::native_handle() const noexcept {
    return dir;
}

void fs::dir_handle::reset() noexcept {
    dir = nullptr;
}

fs::dir_handle fs::dir_handle::from_native(void* ptr) {
    return dir_handle(ptr);
}

task<result<fs::result>> fs::unlink(std::string_view path, event_loop& loop) {
    std::string path_storage(path);
    co_return co_await run_fs<fs::result>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_unlink(loop, req, path_storage.c_str(), cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::mkdir(std::string_view path, int mode, event_loop& loop) {
    std::string path_storage(path);
    co_return co_await run_fs<fs::result>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_mkdir(loop, req, path_storage.c_str(), mode, cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::stat(std::string_view path, event_loop& loop) {
    std::string path_storage(path);
    co_return co_await run_fs<fs::result>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_stat(loop, req, path_storage.c_str(), cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::copyfile(std::string_view path,
                                      std::string_view new_path,
                                      fs::copyfile_options options,
                                      event_loop& loop) {
    auto uv_flags = to_uv_copyfile_flags(options);
    if(!uv_flags) {
        co_return std::unexpected(uv_flags.error());
    }

    auto populate = [&](uv_fs_t& req) {
        fs::result r = basic_populate(req);
        r.path = path;
        r.aux_path = std::string(new_path);
        return r;
    };
    std::string path_storage(path);
    std::string new_path_storage(new_path);

    co_return co_await run_fs<fs::result>(
        [&, path_storage, new_path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_copyfile(loop,
                                   req,
                                   path_storage.c_str(),
                                   new_path_storage.c_str(),
                                   uv_flags.value(),
                                   cb);
        },
        populate,
        loop);
}

task<result<fs::result>> fs::mkdtemp(std::string_view tpl, event_loop& loop) {
    std::string tpl_storage(tpl);
    co_return co_await run_fs<fs::result>(
        [&, tpl_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_mkdtemp(loop, req, tpl_storage.c_str(), cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::mkstemp(std::string_view tpl, event_loop& loop) {
    std::string tpl_storage(tpl);
    co_return co_await run_fs<fs::result>(
        [&, tpl_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_mkstemp(loop, req, tpl_storage.c_str(), cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::rmdir(std::string_view path, event_loop& loop) {
    std::string path_storage(path);
    co_return co_await run_fs<fs::result>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_rmdir(loop, req, path_storage.c_str(), cb);
        },
        basic_populate,
        loop);
}

task<result<std::vector<fs::dirent>>> fs::scandir(std::string_view path, event_loop& loop) {
    auto populate = [](uv_fs_t& req) {
        std::vector<fs::dirent> out;
        uv_dirent_t ent;
        while(uv::fs_scandir_next(req, ent).value() != error::end_of_file.value()) {
            fs::dirent d;
            if(ent.name) {
                d.name = ent.name;
            }
            d.kind = map_dirent(ent.type);
            out.push_back(std::move(d));
        }
        return out;
    };

    std::string path_storage(path);
    co_return co_await run_fs<std::vector<fs::dirent>>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_scandir(loop, req, path_storage.c_str(), 0, cb);
        },
        populate,
        loop);
}

task<result<fs::dir_handle>> fs::opendir(std::string_view path, event_loop& loop) {
    auto populate = [](uv_fs_t& req) {
        return fs::dir_handle::from_native(req.ptr);
    };

    std::string path_storage(path);
    co_return co_await run_fs<fs::dir_handle>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_opendir(loop, req, path_storage.c_str(), cb);
        },
        populate,
        loop);
}

task<result<std::vector<fs::dirent>>> fs::readdir(fs::dir_handle& dir, event_loop& loop) {
    if(!dir.valid()) {
        co_return std::unexpected(error::invalid_argument);
    }

    auto dir_ptr = static_cast<uv_dir_t*>(dir.native_handle());
    if(dir_ptr == nullptr) {
        co_return std::unexpected(error::invalid_argument);
    }

    constexpr std::size_t entry_count = 64;
    auto entries_storage = std::make_shared<std::vector<uv_dirent_t>>(entry_count);
    dir_ptr->dirents = entries_storage->data();
    dir_ptr->nentries = entries_storage->size();

    auto populate = [entries_storage](uv_fs_t& req) {
        std::vector<fs::dirent> out;
        auto* d = static_cast<uv_dir_t*>(req.ptr);
        if(d == nullptr) {
            return out;
        }

        for(unsigned i = 0; i < req.result; ++i) {
            auto& ent = d->dirents[i];
            fs::dirent de;
            if(ent.name) {
                de.name = ent.name;
            }
            de.kind = map_dirent(ent.type);
            out.push_back(std::move(de));
        }
        return out;
    };

    co_return co_await run_fs<std::vector<fs::dirent>>(
        [&](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_readdir(loop, req, *static_cast<uv_dir_t*>(dir.native_handle()), cb);
        },
        populate,
        loop);
}

task<error> fs::closedir(fs::dir_handle& dir, event_loop& loop) {
    if(!dir.valid()) {
        co_return error::invalid_argument;
    }

    auto res = co_await run_fs<fs::result>(
        [&](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_closedir(loop, req, *static_cast<uv_dir_t*>(dir.native_handle()), cb);
        },
        basic_populate,
        loop);

    if(!res) {
        co_return res.error();
    }

    dir.reset();
    co_return error{};
}

task<result<fs::result>> fs::fstat(int fd, event_loop& loop) {
    co_return co_await run_fs<fs::result>(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_fstat(loop, req, fd, cb); },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::lstat(std::string_view path, event_loop& loop) {
    std::string path_storage(path);
    co_return co_await run_fs<fs::result>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_lstat(loop, req, path_storage.c_str(), cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::rename(std::string_view path,
                                    std::string_view new_path,
                                    event_loop& loop) {
    auto populate = [&](uv_fs_t& req) {
        fs::result r = basic_populate(req);
        r.path = path;
        r.aux_path = std::string(new_path);
        return r;
    };
    std::string path_storage(path);
    std::string new_path_storage(new_path);

    co_return co_await run_fs<fs::result>(
        [&, path_storage, new_path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_rename(loop, req, path_storage.c_str(), new_path_storage.c_str(), cb);
        },
        populate,
        loop);
}

task<result<fs::result>> fs::fsync(int fd, event_loop& loop) {
    co_return co_await run_fs<fs::result>(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_fsync(loop, req, fd, cb); },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::fdatasync(int fd, event_loop& loop) {
    co_return co_await run_fs<fs::result>(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_fdatasync(loop, req, fd, cb); },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::ftruncate(int fd, std::int64_t offset, event_loop& loop) {
    co_return co_await run_fs<fs::result>(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_ftruncate(loop, req, fd, offset, cb); },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::sendfile(int out_fd,
                                      int in_fd,
                                      std::int64_t in_offset,
                                      std::size_t length,
                                      event_loop& loop) {
    co_return co_await run_fs<fs::result>(
        [&](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_sendfile(loop, req, out_fd, in_fd, in_offset, length, cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::access(std::string_view path, int mode, event_loop& loop) {
    std::string path_storage(path);
    co_return co_await run_fs<fs::result>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_access(loop, req, path_storage.c_str(), mode, cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::chmod(std::string_view path, int mode, event_loop& loop) {
    std::string path_storage(path);
    co_return co_await run_fs<fs::result>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_chmod(loop, req, path_storage.c_str(), mode, cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>>
    fs::utime(std::string_view path, double atime, double mtime, event_loop& loop) {
    std::string path_storage(path);
    co_return co_await run_fs<fs::result>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_utime(loop, req, path_storage.c_str(), atime, mtime, cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::futime(int fd, double atime, double mtime, event_loop& loop) {
    co_return co_await run_fs<fs::result>(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_futime(loop, req, fd, atime, mtime, cb); },
        basic_populate,
        loop);
}

task<result<fs::result>>
    fs::lutime(std::string_view path, double atime, double mtime, event_loop& loop) {
    std::string path_storage(path);
    co_return co_await run_fs<fs::result>(
        [&, path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_lutime(loop, req, path_storage.c_str(), atime, mtime, cb);
        },
        basic_populate,
        loop);
}

task<result<fs::result>> fs::link(std::string_view path,
                                  std::string_view new_path,
                                  event_loop& loop) {
    auto populate = [&](uv_fs_t& req) {
        fs::result r = basic_populate(req);
        r.path = path;
        r.aux_path = std::string(new_path);
        return r;
    };
    std::string path_storage(path);
    std::string new_path_storage(new_path);

    co_return co_await run_fs<fs::result>(
        [&, path_storage, new_path_storage](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_link(loop, req, path_storage.c_str(), new_path_storage.c_str(), cb);
        },
        populate,
        loop);
}

}  // namespace eventide
