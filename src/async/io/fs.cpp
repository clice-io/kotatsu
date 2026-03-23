#include "eventide/async/io/fs.h"

#include <cassert>
#include <functional>

#include "awaiter.h"
#include "eventide/async/io/loop.h"
#include "eventide/async/vocab/error.h"

namespace eventide {

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
        // Callback won't fire on submit failure; clean up manually.
        uv::fs_req_cleanup(op.req);
        co_await fail(err);
    }

    co_return co_await op;
}

template <typename Submit>
static task<void, error> run_void_fs(Submit submit, event_loop& loop) {
    if(auto res = co_await run_fs<int>(std::move(submit), [](uv_fs_t&) { return 0; }, loop); !res) {
        co_await fail(res.error());
    }
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
    return run_void_fs(
        [p = std::string(path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_unlink(loop, req, p.c_str(), cb);
        },
        loop);
}

task<void, error> fs::mkdir(std::string_view path, int mode, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), mode, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_mkdir(loop, req, p.c_str(), mode, cb);
        },
        loop);
}

task<void, error> fs::rmdir(std::string_view path, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_rmdir(loop, req, p.c_str(), cb);
        },
        loop);
}

task<void, error> fs::fsync(int fd, event_loop& loop) {
    return run_void_fs(
        [fd, &loop](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_fsync(loop, req, fd, cb); },
        loop);
}

task<void, error> fs::fdatasync(int fd, event_loop& loop) {
    return run_void_fs(
        [fd, &loop](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_fdatasync(loop, req, fd, cb); },
        loop);
}

task<void, error> fs::ftruncate(int fd, std::int64_t offset, event_loop& loop) {
    return run_void_fs(
        [fd, offset, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_ftruncate(loop, req, fd, offset, cb);
        },
        loop);
}

task<void, error> fs::access(std::string_view path, int mode, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), mode, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_access(loop, req, p.c_str(), mode, cb);
        },
        loop);
}

task<void, error> fs::chmod(std::string_view path, int mode, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), mode, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_chmod(loop, req, p.c_str(), mode, cb);
        },
        loop);
}

task<void, error> fs::utime(std::string_view path, double atime, double mtime, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), atime, mtime, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_utime(loop, req, p.c_str(), atime, mtime, cb);
        },
        loop);
}

task<void, error> fs::futime(int fd, double atime, double mtime, event_loop& loop) {
    return run_void_fs(
        [fd, atime, mtime, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_futime(loop, req, fd, atime, mtime, cb);
        },
        loop);
}

task<void, error> fs::lutime(std::string_view path, double atime, double mtime, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), atime, mtime, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_lutime(loop, req, p.c_str(), atime, mtime, cb);
        },
        loop);
}

task<void, error> fs::copyfile(std::string_view path,
                               std::string_view new_path,
                               fs::copyfile_options options,
                               event_loop& loop) {
    auto uv_flags = co_await or_fail(to_uv_copyfile_flags(options));

    co_await run_void_fs(
        [p = std::string(path), np = std::string(new_path), uv_flags, &loop](uv_fs_t& req,
                                                                             uv_fs_cb cb) {
            return uv::fs_copyfile(loop, req, p.c_str(), np.c_str(), uv_flags, cb);
        },
        loop)
        .or_fail();
}

task<void, error> fs::rename(std::string_view path, std::string_view new_path, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), np = std::string(new_path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_rename(loop, req, p.c_str(), np.c_str(), cb);
        },
        loop);
}

task<void, error> fs::link(std::string_view path, std::string_view new_path, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), np = std::string(new_path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_link(loop, req, p.c_str(), np.c_str(), cb);
        },
        loop);
}

task<void, error>
    fs::symlink(std::string_view path, std::string_view new_path, int flags, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), np = std::string(new_path), flags, &loop](uv_fs_t& req,
                                                                          uv_fs_cb cb) {
            return uv::fs_symlink(loop, req, p.c_str(), np.c_str(), flags, cb);
        },
        loop);
}

task<void, error> fs::fchmod(int fd, int mode, event_loop& loop) {
    return run_void_fs(
        [fd, mode, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_fchmod(loop, req, fd, mode, cb);
        },
        loop);
}

task<void, error>
    fs::chown(std::string_view path, std::uint32_t uid, std::uint32_t gid, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), uid, gid, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_chown(loop, req, p.c_str(), uid, gid, cb);
        },
        loop);
}

task<void, error> fs::fchown(int fd, std::uint32_t uid, std::uint32_t gid, event_loop& loop) {
    return run_void_fs(
        [fd, uid, gid, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_fchown(loop, req, fd, uid, gid, cb);
        },
        loop);
}

task<void, error>
    fs::lchown(std::string_view path, std::uint32_t uid, std::uint32_t gid, event_loop& loop) {
    return run_void_fs(
        [p = std::string(path), uid, gid, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_lchown(loop, req, p.c_str(), uid, gid, cb);
        },
        loop);
}

task<void, error> fs::close(int fd, event_loop& loop) {
    return run_void_fs(
        [fd, &loop](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_close(loop, req, fd, cb); },
        loop);
}

task<void, error> fs::closedir(fs::dir_handle& dir, event_loop& loop) {
    if(!dir.valid()) {
        co_await fail(error::invalid_argument);
    }

    co_await run_void_fs(
        [&](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_closedir(loop, req, *static_cast<uv_dir_t*>(dir.native_handle()), cb);
        },
        loop)
        .or_fail();
    dir.reset();
}

// ============================================================================
// Stat operations
// ============================================================================

task<fs::file_stats, error> fs::stat(std::string_view path, event_loop& loop) {
    return run_fs<fs::file_stats>(
        [p = std::string(path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_stat(loop, req, p.c_str(), cb);
        },
        [](uv_fs_t& req) { return to_file_stats(req.statbuf); },
        loop);
}

task<fs::file_stats, error> fs::fstat(int fd, event_loop& loop) {
    return run_fs<fs::file_stats>(
        [fd, &loop](uv_fs_t& req, uv_fs_cb cb) { return uv::fs_fstat(loop, req, fd, cb); },
        [](uv_fs_t& req) { return to_file_stats(req.statbuf); },
        loop);
}

task<fs::file_stats, error> fs::lstat(std::string_view path, event_loop& loop) {
    return run_fs<fs::file_stats>(
        [p = std::string(path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_lstat(loop, req, p.c_str(), cb);
        },
        [](uv_fs_t& req) { return to_file_stats(req.statbuf); },
        loop);
}

// ============================================================================
// Operations with typed results
// ============================================================================

task<std::string, error> fs::mkdtemp(std::string_view tpl, event_loop& loop) {
    return run_fs<std::string>(
        [t = std::string(tpl), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_mkdtemp(loop, req, t.c_str(), cb);
        },
        [](uv_fs_t& req) -> std::string { return req.path ? req.path : ""; },
        loop);
}

task<fs::mkstemp_result, error> fs::mkstemp(std::string_view tpl, event_loop& loop) {
    return run_fs<fs::mkstemp_result>(
        [t = std::string(tpl), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_mkstemp(loop, req, t.c_str(), cb);
        },
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
    return run_fs<std::int64_t>(
        [out_fd, in_fd, in_offset, length, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_sendfile(loop, req, out_fd, in_fd, in_offset, length, cb);
        },
        [](uv_fs_t& req) -> std::int64_t { return req.result; },
        loop);
}

task<std::string, error> fs::readlink(std::string_view path, event_loop& loop) {
    return run_fs<std::string>(
        [p = std::string(path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_readlink(loop, req, p.c_str(), cb);
        },
        [](uv_fs_t& req) -> result<std::string> {
            if(!req.ptr) {
                return outcome_error(error::io_error);
            }
            return std::string(static_cast<const char*>(req.ptr));
        },
        loop);
}

task<std::string, error> fs::realpath(std::string_view path, event_loop& loop) {
    return run_fs<std::string>(
        [p = std::string(path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_realpath(loop, req, p.c_str(), cb);
        },
        [](uv_fs_t& req) -> result<std::string> {
            if(!req.ptr) {
                return outcome_error(error::io_error);
            }
            return std::string(static_cast<const char*>(req.ptr));
        },
        loop);
}

task<fs::fs_stats, error> fs::statfs(std::string_view path, event_loop& loop) {
    return run_fs<fs::fs_stats>(
        [p = std::string(path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_statfs(loop, req, p.c_str(), cb);
        },
        [](uv_fs_t& req) -> result<fs::fs_stats> {
            auto* s = static_cast<uv_statfs_t*>(req.ptr);
            if(!s) {
                return outcome_error(error::io_error);
            }
            return fs::fs_stats{
                .type = s->f_type,
                .bsize = s->f_bsize,
                .blocks = s->f_blocks,
                .bfree = s->f_bfree,
                .bavail = s->f_bavail,
                .files = s->f_files,
                .ffree = s->f_ffree,
                .frsize = s->f_frsize,
            };
        },
        loop);
}

task<int, error> fs::open(std::string_view path, int flags, int mode, event_loop& loop) {
    return run_fs<int>(
        [p = std::string(path), flags, mode, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_open(loop, req, p.c_str(), flags, mode, cb);
        },
        [](uv_fs_t& req) -> int { return static_cast<int>(req.result); },
        loop);
}

task<std::size_t, error>
    fs::read(int fd, std::span<char> buf, std::int64_t offset, event_loop& loop) {
    auto storage =
        std::make_shared<uv_buf_t>(uv_buf_init(buf.data(), static_cast<unsigned int>(buf.size())));

    return run_fs<std::size_t>(
        [fd, storage, offset, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_read(loop, req, fd, storage.get(), 1, offset, cb);
        },
        [](uv_fs_t& req) -> std::size_t { return static_cast<std::size_t>(req.result); },
        loop);
}

task<std::size_t, error>
    fs::write(int fd, std::span<const char> buf, std::int64_t offset, event_loop& loop) {
    auto storage = std::make_shared<uv_buf_t>(
        uv_buf_init(const_cast<char*>(buf.data()), static_cast<unsigned int>(buf.size())));

    return run_fs<std::size_t>(
        [fd, storage, offset, &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_write(loop, req, fd, storage.get(), 1, offset, cb);
        },
        [](uv_fs_t& req) -> std::size_t { return static_cast<std::size_t>(req.result); },
        loop);
}

// ============================================================================
// Directory enumeration
// ============================================================================

task<std::vector<fs::dirent>, error> fs::scandir(std::string_view path, event_loop& loop) {
    return run_fs<std::vector<fs::dirent>>(
        [p = std::string(path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_scandir(loop, req, p.c_str(), 0, cb);
        },
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
    return run_fs<fs::dir_handle>(
        [p = std::string(path), &loop](uv_fs_t& req, uv_fs_cb cb) {
            return uv::fs_opendir(loop, req, p.c_str(), cb);
        },
        [](uv_fs_t& req) { return fs::dir_handle::from_native(req.ptr); },
        loop);
}

task<std::vector<fs::dirent>, error> fs::readdir(fs::dir_handle& dir, event_loop& loop) {
    if(!dir.valid()) {
        co_await fail(error::invalid_argument);
    }

    auto dir_ptr = static_cast<uv_dir_t*>(dir.native_handle());
    if(dir_ptr == nullptr) {
        co_await fail(error::invalid_argument);
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

// ============================================================================
// Synchronous file operations
// ============================================================================

namespace {

/// Returns the default loop handle for synchronous libuv fs calls.
/// libuv sync operations (cb = nullptr) don't actually interact with the
/// event loop, so uv_default_loop() is safe to use from any thread.
uv_loop_t* sync_loop() noexcept {
    return uv_default_loop();
}

}  // namespace

result<int> fs::sync::open(std::string_view path, int flags, int mode) {
    std::string p(path);
    uv_fs_t req{};
    int r = uv_fs_open(sync_loop(), &req, p.c_str(), flags, mode, nullptr);
    uv_fs_req_cleanup(&req);
    if(r < 0) {
        return outcome_error(uv::status_to_error(r));
    }
    return r;
}

result<std::size_t> fs::sync::read(int fd, std::span<char> buf, std::int64_t offset) {
    uv_buf_t uv_buf = uv_buf_init(buf.data(), static_cast<unsigned int>(buf.size()));
    uv_fs_t req{};
    int r = uv_fs_read(sync_loop(), &req, fd, &uv_buf, 1, offset, nullptr);
    uv_fs_req_cleanup(&req);
    if(r < 0) {
        return outcome_error(uv::status_to_error(r));
    }
    return static_cast<std::size_t>(r);
}

result<std::size_t> fs::sync::write(int fd, std::span<const char> buf, std::int64_t offset) {
    uv_buf_t uv_buf =
        uv_buf_init(const_cast<char*>(buf.data()), static_cast<unsigned int>(buf.size()));
    uv_fs_t req{};
    int r = uv_fs_write(sync_loop(), &req, fd, &uv_buf, 1, offset, nullptr);
    uv_fs_req_cleanup(&req);
    if(r < 0) {
        return outcome_error(uv::status_to_error(r));
    }
    return static_cast<std::size_t>(r);
}

error fs::sync::close(int fd) {
    uv_fs_t req{};
    int r = uv_fs_close(sync_loop(), &req, fd, nullptr);
    uv_fs_req_cleanup(&req);
    if(r < 0) {
        return uv::status_to_error(r);
    }
    return {};
}

result<std::string> fs::sync::read_to_string(std::string_view path) {
    auto fd = open(path, UV_FS_O_RDONLY);
    if(!fd) {
        return outcome_error(fd.error());
    }

    std::string content;
    char buf[4096];
    while(true) {
        auto n = read(*fd, std::span<char>(buf, sizeof(buf)));
        if(!n) {
            close(*fd);
            return outcome_error(n.error());
        }
        if(*n == 0) {
            break;
        }
        content.append(buf, *n);
    }

    close(*fd);
    return content;
}

}  // namespace eventide
