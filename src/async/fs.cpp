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
        co_return outcome_error(error::invalid_argument);
    }

    if(self->has_pending()) {
        co_return self->take_pending();
    }

    if(self->has_waiter()) {
        co_return outcome_error(error::connection_already_in_progress);
    }

    co_return co_await fs_event_await{self.get()};
}

// ============================================================================
// Filesystem operations
// ============================================================================

namespace {

template <typename Result>
struct fs_op : uv::await_op<fs_op<Result>> {
    using promise_t = task<Result, error>::promise_type;

    uv_fs_t req = {};
    std::function<result<Result>(uv_fs_t&)> populate;
    result<Result> out = outcome_error(error());

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
        return outcome_error(error::function_not_implemented);
    }
#endif
#ifdef UV_FS_COPYFILE_FICLONE
    if(options.clone) {
        out |= UV_FS_COPYFILE_FICLONE;
    }
#else
    if(options.clone) {
        return outcome_error(error::function_not_implemented);
    }
#endif
#ifdef UV_FS_COPYFILE_FICLONE_FORCE
    if(options.clone_force) {
        out |= UV_FS_COPYFILE_FICLONE_FORCE;
    }
#else
    if(options.clone_force) {
        return outcome_error(error::function_not_implemented);
    }
#endif
    return static_cast<int>(out);
}

template <typename Result, typename Submit, typename Populate>
static task<Result, error> run_fs(Submit submit,
                                  Populate populate,
                                  event_loop& loop = event_loop::current()) {
    fs_op<Result> op;
    op.populate = populate;

    auto after_cb = [](uv_fs_t* req) {
        auto* h = static_cast<fs_op<Result>*>(req->data);
        assert(h != nullptr && "fs after_cb requires operation in req->data");

        h->mark_cancelled_if(req->result);

        if(req->result < 0) {
            h->out = outcome_error(uv::status_to_error(req->result));
        } else {
            h->out = h->populate(*req);
        }

        uv::fs_req_cleanup(*req);

        h->complete();
    };

    op.req.data = &op;

    if(auto err = submit(op.req, after_cb)) {
        co_return outcome_error(err);
    }

    co_return co_await op;
}

template <typename Submit>
static task<void, error> run_void_fs(Submit submit, event_loop& loop) {
    if(auto res = co_await run_fs<int>(std::move(submit), [](uv_fs_t&) { return 0; }, loop); !res) {
        co_return outcome_error(res.error());
    }
    co_return outcome_value();
}

static fs::file_time to_file_time(const uv_timespec_t& ts) {
    return fs::file_time{std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec}};
}

static fs::file_stats to_file_stats(const uv_stat_t& s) {
    return {
        .dev = s.st_dev,
        .mode = s.st_mode,
        .nlink = s.st_nlink,
        .uid = s.st_uid,
        .gid = s.st_gid,
        .rdev = s.st_rdev,
        .ino = s.st_ino,
        .size = s.st_size,
        .blksize = s.st_blksize,
        .blocks = s.st_blocks,
        .flags = s.st_flags,
        .gen = s.st_gen,
        .atime = to_file_time(s.st_atim),
        .mtime = to_file_time(s.st_mtim),
        .ctime = to_file_time(s.st_ctim),
        .birthtime = to_file_time(s.st_birthtim),
    };
}

}  // namespace

// ============================================================================
// dir_handle
// ============================================================================

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

// ============================================================================
// Success/failure operations
// ============================================================================

task<void, error> fs::unlink(std::string_view path, event_loop& loop) {
    std::string p(path);
    co_return co_await run_void_fs(
        [&, p](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_unlink(loop, req, p.c_str(), cb); },
        loop);
}

task<void, error> fs::mkdir(std::string_view path, int mode, event_loop& loop) {
    std::string p(path);
    co_return co_await run_void_fs(
        [&, p](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_mkdir(loop, req, p.c_str(), mode, cb); },
        loop);
}

task<void, error> fs::rmdir(std::string_view path, event_loop& loop) {
    std::string p(path);
    co_return co_await run_void_fs(
        [&, p](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_rmdir(loop, req, p.c_str(), cb); },
        loop);
}

task<void, error> fs::fsync(int fd, event_loop& loop) {
    co_return co_await run_void_fs(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_fsync(loop, req, fd, cb); },
        loop);
}

task<void, error> fs::fdatasync(int fd, event_loop& loop) {
    co_return co_await run_void_fs(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_fdatasync(loop, req, fd, cb); },
        loop);
}

task<void, error> fs::ftruncate(int fd, std::int64_t offset, event_loop& loop) {
    co_return co_await run_void_fs(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_ftruncate(loop, req, fd, offset, cb); },
        loop);
}

task<void, error> fs::access(std::string_view path, int mode, event_loop& loop) {
    std::string p(path);
    co_return co_await run_void_fs(
        [&, p](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_access(loop, req, p.c_str(), mode, cb); },
        loop);
}

task<void, error> fs::chmod(std::string_view path, int mode, event_loop& loop) {
    std::string p(path);
    co_return co_await run_void_fs(
        [&, p](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_chmod(loop, req, p.c_str(), mode, cb); },
        loop);
}

task<void, error> fs::utime(std::string_view path, double atime, double mtime, event_loop& loop) {
    std::string p(path);
    co_return co_await run_void_fs(
        [&, p](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_utime(loop, req, p.c_str(), atime, mtime, cb);
        },
        loop);
}

task<void, error> fs::futime(int fd, double atime, double mtime, event_loop& loop) {
    co_return co_await run_void_fs(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_futime(loop, req, fd, atime, mtime, cb); },
        loop);
}

task<void, error> fs::lutime(std::string_view path, double atime, double mtime, event_loop& loop) {
    std::string p(path);
    co_return co_await run_void_fs(
        [&, p](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_lutime(loop, req, p.c_str(), atime, mtime, cb);
        },
        loop);
}

task<void, error> fs::copyfile(std::string_view path,
                               std::string_view new_path,
                               fs::copyfile_options options,
                               event_loop& loop) {
    auto uv_flags = to_uv_copyfile_flags(options);
    if(!uv_flags) {
        co_return outcome_error(uv_flags.error());
    }

    std::string p(path);
    std::string np(new_path);
    co_return co_await run_void_fs(
        [&, p, np](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_copyfile(loop, req, p.c_str(), np.c_str(), uv_flags.value(), cb);
        },
        loop);
}

task<void, error> fs::rename(std::string_view path, std::string_view new_path, event_loop& loop) {
    std::string p(path);
    std::string np(new_path);
    co_return co_await run_void_fs(
        [&, p, np](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_rename(loop, req, p.c_str(), np.c_str(), cb);
        },
        loop);
}

task<void, error> fs::link(std::string_view path, std::string_view new_path, event_loop& loop) {
    std::string p(path);
    std::string np(new_path);
    co_return co_await run_void_fs(
        [&, p, np](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_link(loop, req, p.c_str(), np.c_str(), cb);
        },
        loop);
}

task<void, error> fs::closedir(fs::dir_handle& dir, event_loop& loop) {
    if(!dir.valid()) {
        co_return outcome_error(error::invalid_argument);
    }

    if(auto res = co_await run_void_fs(
           [&](uv_fs_t& req, uv_fs_cb cb) {
               return uv::fs_closedir(loop, req, *static_cast<uv_dir_t*>(dir.native_handle()), cb);
           },
           loop);
       !res) {
        co_return outcome_error(res.error());
    }
    dir.reset();
    co_return outcome_value();
}

// ============================================================================
// Stat operations
// ============================================================================

task<fs::file_stats, error> fs::stat(std::string_view path, event_loop& loop) {
    std::string p(path);
    co_return co_await run_fs<fs::file_stats>(
        [&, p](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_stat(loop, req, p.c_str(), cb); },
        [](uv_fs_t& req) { return to_file_stats(req.statbuf); },
        loop);
}

task<fs::file_stats, error> fs::fstat(int fd, event_loop& loop) {
    co_return co_await run_fs<fs::file_stats>(
        [&](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_fstat(loop, req, fd, cb); },
        [](uv_fs_t& req) { return to_file_stats(req.statbuf); },
        loop);
}

task<fs::file_stats, error> fs::lstat(std::string_view path, event_loop& loop) {
    std::string p(path);
    co_return co_await run_fs<fs::file_stats>(
        [&, p](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_lstat(loop, req, p.c_str(), cb); },
        [](uv_fs_t& req) { return to_file_stats(req.statbuf); },
        loop);
}

// ============================================================================
// Operations with typed results
// ============================================================================

task<std::string, error> fs::mkdtemp(std::string_view tpl, event_loop& loop) {
    std::string t(tpl);
    co_return co_await run_fs<std::string>(
        [&, t](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_mkdtemp(loop, req, t.c_str(), cb); },
        [](uv_fs_t& req) -> std::string { return req.path ? req.path : ""; },
        loop);
}

task<fs::mkstemp_result, error> fs::mkstemp(std::string_view tpl, event_loop& loop) {
    std::string t(tpl);
    co_return co_await run_fs<fs::mkstemp_result>(
        [&, t](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_mkstemp(loop, req, t.c_str(), cb); },
        [](uv_fs_t& req) -> fs::mkstemp_result {
            return {static_cast<int>(req.result), req.path ? req.path : ""};
        },
        loop);
}

task<std::int64_t, error> fs::sendfile(int out_fd,
                                       int in_fd,
                                       std::int64_t in_offset,
                                       std::size_t length,
                                       event_loop& loop) {
    co_return co_await run_fs<std::int64_t>(
        [&](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_sendfile(loop, req, out_fd, in_fd, in_offset, length, cb);
        },
        [](uv_fs_t& req) -> std::int64_t { return req.result; },
        loop);
}

// ============================================================================
// Directory enumeration
// ============================================================================

task<std::vector<fs::dirent>, error> fs::scandir(std::string_view path, event_loop& loop) {
    std::string p(path);
    co_return co_await run_fs<std::vector<fs::dirent>>(
        [&, p](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_scandir(loop, req, p.c_str(), 0, cb); },
        [](uv_fs_t& req) -> result<std::vector<fs::dirent>> {
            std::vector<fs::dirent> out;
            uv_dirent_t ent;
            while(true) {
                auto err = uv::fs_scandir_next(req, ent);
                if(err == error::end_of_file) {
                    break;
                }
                if(err) {
                    return result<std::vector<fs::dirent>>(outcome_error(err));
                }

                fs::dirent d;
                if(ent.name) {
                    d.name = ent.name;
                }
                d.kind = map_dirent(ent.type);
                out.push_back(std::move(d));
            }
            return out;
        },
        loop);
}

task<fs::dir_handle, error> fs::opendir(std::string_view path, event_loop& loop) {
    std::string p(path);
    co_return co_await run_fs<fs::dir_handle>(
        [&, p](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_opendir(loop, req, p.c_str(), cb); },
        [](uv_fs_t& req) { return fs::dir_handle::from_native(req.ptr); },
        loop);
}

task<std::vector<fs::dirent>, error> fs::readdir(fs::dir_handle& dir, event_loop& loop) {
    if(!dir.valid()) {
        co_return outcome_error(error::invalid_argument);
    }

    auto dir_ptr = static_cast<uv_dir_t*>(dir.native_handle());
    if(dir_ptr == nullptr) {
        co_return outcome_error(error::invalid_argument);
    }

    constexpr std::size_t entry_count = 64;
    auto entries_storage = std::make_shared<std::vector<uv_dirent_t>>(entry_count);
    dir_ptr->dirents = entries_storage->data();
    dir_ptr->nentries = entries_storage->size();

    co_return co_await run_fs<std::vector<fs::dirent>>(
        [&](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_readdir(loop, req, *static_cast<uv_dir_t*>(dir.native_handle()), cb);
        },
        [entries_storage](uv_fs_t& req) {
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
        },
        loop);
}

}  // namespace eventide
