#include "kota/async/io/fs.h"

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "awaiter.h"
#include "kota/support/functional.h"

namespace kota::fs {

namespace {

/// Runs a libuv fs call synchronously; returns what it returned, which is
/// an error below zero.
template <typename Submit, typename... Args>
result<std::size_t> run_sync(Submit submit, Args... args) {
    uv_fs_t req;
    auto status = submit(nullptr, &req, args..., nullptr);
    ::uv_fs_req_cleanup(&req);
    if(status < 0) {
        return outcome_error(error(status));
    }
    return static_cast<std::size_t>(status);
}

/// The value of a request that makes nothing.
void no_value(uv_fs_t&) {}

/// What to release of a request that makes nothing to release.
void keep(uv_fs_t&) {}

template <auto Project>
using value_of = std::invoke_result_t<decltype(Project), uv_fs_t&>;

/// A libuv fs request: `Project` makes the value of one that succeeded, and
/// `Release` frees what one made after its task was cancelled, when the
/// cancel came too late to stop it.
template <auto Project, auto Release>
struct fs_op : uv::uv_op<fs_op<Project, Release>> {
    using submit_fn = function_ref<int(uv_fs_t*, uv_fs_cb)>;

    uv_fs_t req = {};
    submit_fn submit;
    result<value_of<Project>> value = outcome_error(error());

    explicit fs_op(submit_fn submit) noexcept : submit(submit) {}

    bool start() noexcept {
        req.data = this;
        if(auto err = error(submit(&req, &on_done))) {
            ::uv_fs_req_cleanup(&req);
            value = outcome_error(err);
            return false;
        }
        return true;
    }

    /// Dequeues the request if no thread has taken it yet; otherwise it
    /// runs to its end, and on_done releases what it made.
    void cancel() noexcept {
        ::uv_cancel(reinterpret_cast<uv_req_t*>(&req));
    }

    static void on_done(uv_fs_t* req) {
        auto* op = static_cast<fs_op*>(req->data);
        if(req->result < 0) {
            op->value = outcome_error(uv::status_to_error(req->result));
        } else if(op->cancel_requested()) {
            Release(*req);
        } else if constexpr(std::is_void_v<value_of<Project>>) {
            op->value = {};
        } else {
            op->value = Project(*req);
        }
        ::uv_fs_req_cleanup(req);
        op->complete();
    }

    result<value_of<Project>> await_resume() noexcept {
        return std::move(value);
    }
};

/// How an argument fs_call keeps reaches libuv: a path NUL-terminated, a
/// buffer by address.
template <typename Arg>
decltype(auto) pass(const Arg& arg) {
    if constexpr(std::same_as<Arg, std::string>) {
        return arg.c_str();
    } else if constexpr(std::same_as<Arg, uv_buf_t>) {
        return &arg;
    } else {
        return (arg);
    }
}

/// Runs the libuv fs call `submit` with `args`. The call's frame keeps the
/// arguments, taken by value: paths as strings, since the returned task
/// submits only once awaited.
template <auto Project = no_value, auto Release = keep, typename Submit, typename... Args>
task<value_of<Project>, error> fs_call(event_loop& loop, Submit submit, Args... args) {
    auto request = [&](uv_fs_t* req, uv_fs_cb done) {
        return submit(loop.native_handle(), req, pass(args)..., done);
    };
    if constexpr(std::is_void_v<value_of<Project>>) {
        co_await or_fail(co_await fs_op<Project, Release>(request));
    } else {
        co_return co_await fs_op<Project, Release>(request);
    }
}

file_time to_file_time(const uv_timespec_t& ts) {
    return file_time{std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec}};
}

file_stats stats_of(uv_fs_t& req) {
    const auto& s = req.statbuf;
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

fs_stats fs_stats_of(uv_fs_t& req) {
    const auto& s = *static_cast<const uv_statfs_t*>(req.ptr);
    return {
        .type = s.f_type,
        .bsize = s.f_bsize,
        .blocks = s.f_blocks,
        .bfree = s.f_bfree,
        .bavail = s.f_bavail,
        .files = s.f_files,
        .ffree = s.f_ffree,
    // f_frsize was added in libuv 1.52. Fall back to f_bsize on older
    // versions (conda-forge's macOS toolchain still ships 1.51).
#if UV_VERSION_HEX >= ((1 << 16) | (52 << 8))
        .frsize = s.f_frsize,
#else
        .frsize = s.f_bsize,
#endif
    };
}

int descriptor_of(uv_fs_t& req) {
    return static_cast<int>(req.result);
}

std::size_t size_of(uv_fs_t& req) {
    return static_cast<std::size_t>(req.result);
}

std::int64_t count_of(uv_fs_t& req) {
    return req.result;
}

std::string path_of(uv_fs_t& req) {
    return req.path;
}

std::string text_of(uv_fs_t& req) {
    return static_cast<const char*>(req.ptr);
}

mkstemp_result temp_file_of(uv_fs_t& req) {
    return {.fd = descriptor_of(req), .path = req.path};
}

void close_descriptor(uv_fs_t& req) {
    run_sync(::uv_fs_close, descriptor_of(req));
}

dirent::type kind_of(uv_dirent_type_t type) {
    switch(type) {
        case UV_DIRENT_FILE: return dirent::type::file;
        case UV_DIRENT_DIR: return dirent::type::dir;
        case UV_DIRENT_LINK: return dirent::type::link;
        case UV_DIRENT_FIFO: return dirent::type::fifo;
        case UV_DIRENT_SOCKET: return dirent::type::socket;
        case UV_DIRENT_CHAR: return dirent::type::char_device;
        case UV_DIRENT_BLOCK: return dirent::type::block_device;
        default: return dirent::type::unknown;
    }
}

std::vector<dirent> scanned_of(uv_fs_t& req) {
    std::vector<dirent> out;
    uv_dirent_t entry;
    while(::uv_fs_scandir_next(&req, &entry) == 0) {
        out.push_back({.name = entry.name, .kind = kind_of(entry.type)});
    }
    return out;
}

std::vector<dirent> read_entries_of(uv_fs_t& req) {
    const auto* dir = static_cast<const uv_dir_t*>(req.ptr);
    std::vector<dirent> out;
    for(std::size_t i = 0; i < static_cast<std::size_t>(req.result); ++i) {
        out.push_back({.name = dir->dirents[i].name, .kind = kind_of(dir->dirents[i].type)});
    }
    return out;
}

void close_dir(uv_dir_t* dir) {
    run_sync(::uv_fs_closedir, dir);
}

void close_opened_dir(uv_fs_t& req) {
    close_dir(static_cast<uv_dir_t*>(req.ptr));
}

}  // namespace

struct dir_handle::Self {
    uv_dir_t* dir;
    /// Where readdir() reads a batch of entries.
    std::array<uv_dirent_t, 64> entries;

    explicit Self(uv_dir_t* dir) noexcept : dir(dir) {
        dir->dirents = entries.data();
        dir->nentries = entries.size();
    }

    Self(const Self&) = delete;
    Self& operator=(const Self&) = delete;

    ~Self() {
        close_dir(dir);
    }

    static dir_handle adopt(uv_fs_t& req) {
        dir_handle out;
        out.self = std::make_unique<Self>(static_cast<uv_dir_t*>(req.ptr));
        return out;
    }
};

dir_handle::dir_handle() noexcept = default;

dir_handle::dir_handle(dir_handle&& other) noexcept = default;

dir_handle& dir_handle::operator=(dir_handle&& other) noexcept = default;

dir_handle::~dir_handle() = default;

task<void, error> unlink(std::string_view path, event_loop& loop) {
    return fs_call(loop, ::uv_fs_unlink, std::string(path));
}

task<void, error> mkdir(std::string_view path, int mode, event_loop& loop) {
    return fs_call(loop, ::uv_fs_mkdir, std::string(path), mode);
}

task<file_stats, error> stat(std::string_view path, event_loop& loop) {
    return fs_call<stats_of>(loop, ::uv_fs_stat, std::string(path));
}

task<void, error> copyfile(std::string_view path,
                           std::string_view new_path,
                           copyfile_options options,
                           event_loop& loop) {
    int flags = 0;
    if(options.excl) {
        flags |= UV_FS_COPYFILE_EXCL;
    }
    if(options.clone) {
        flags |= UV_FS_COPYFILE_FICLONE;
    }
    if(options.clone_force) {
        flags |= UV_FS_COPYFILE_FICLONE_FORCE;
    }
    return fs_call(loop, ::uv_fs_copyfile, std::string(path), std::string(new_path), flags);
}

task<std::string, error> mkdtemp(std::string_view tpl, event_loop& loop) {
    return fs_call<path_of>(loop, ::uv_fs_mkdtemp, std::string(tpl));
}

task<mkstemp_result, error> mkstemp(std::string_view tpl, event_loop& loop) {
    return fs_call<temp_file_of, close_descriptor>(loop, ::uv_fs_mkstemp, std::string(tpl));
}

task<void, error> rmdir(std::string_view path, event_loop& loop) {
    return fs_call(loop, ::uv_fs_rmdir, std::string(path));
}

task<std::vector<dirent>, error> scandir(std::string_view path, event_loop& loop) {
    return fs_call<scanned_of>(loop, ::uv_fs_scandir, std::string(path), 0);
}

task<dir_handle, error> opendir(std::string_view path, event_loop& loop) {
    return fs_call<dir_handle::Self::adopt, close_opened_dir>(loop,
                                                              ::uv_fs_opendir,
                                                              std::string(path));
}

task<std::vector<dirent>, error> readdir(dir_handle& dir, event_loop& loop) {
    // libuv refuses the null directory of an inert handle.
    return fs_call<read_entries_of>(loop, ::uv_fs_readdir, dir.self ? dir.self->dir : nullptr);
}

task<file_stats, error> fstat(int fd, event_loop& loop) {
    return fs_call<stats_of>(loop, ::uv_fs_fstat, fd);
}

task<file_stats, error> lstat(std::string_view path, event_loop& loop) {
    return fs_call<stats_of>(loop, ::uv_fs_lstat, std::string(path));
}

task<void, error> rename(std::string_view path, std::string_view new_path, event_loop& loop) {
    return fs_call(loop, ::uv_fs_rename, std::string(path), std::string(new_path));
}

task<void, error> fsync(int fd, event_loop& loop) {
    return fs_call(loop, ::uv_fs_fsync, fd);
}

task<void, error> fdatasync(int fd, event_loop& loop) {
    return fs_call(loop, ::uv_fs_fdatasync, fd);
}

task<void, error> ftruncate(int fd, std::int64_t offset, event_loop& loop) {
    return fs_call(loop, ::uv_fs_ftruncate, fd, offset);
}

task<std::int64_t, error>
    sendfile(int out_fd, int in_fd, std::int64_t in_offset, std::size_t length, event_loop& loop) {
    return fs_call<count_of>(loop, ::uv_fs_sendfile, out_fd, in_fd, in_offset, length);
}

task<void, error> access(std::string_view path, int mode, event_loop& loop) {
    return fs_call(loop, ::uv_fs_access, std::string(path), mode);
}

task<void, error> chmod(std::string_view path, int mode, event_loop& loop) {
    return fs_call(loop, ::uv_fs_chmod, std::string(path), mode);
}

task<void, error> utime(std::string_view path, double atime, double mtime, event_loop& loop) {
    return fs_call(loop, ::uv_fs_utime, std::string(path), atime, mtime);
}

task<void, error> futime(int fd, double atime, double mtime, event_loop& loop) {
    return fs_call(loop, ::uv_fs_futime, fd, atime, mtime);
}

task<void, error> lutime(std::string_view path, double atime, double mtime, event_loop& loop) {
    return fs_call(loop, ::uv_fs_lutime, std::string(path), atime, mtime);
}

task<void, error> link(std::string_view path, std::string_view new_path, event_loop& loop) {
    return fs_call(loop, ::uv_fs_link, std::string(path), std::string(new_path));
}

task<void, error>
    symlink(std::string_view path, std::string_view new_path, int flags, event_loop& loop) {
    return fs_call(loop, ::uv_fs_symlink, std::string(path), std::string(new_path), flags);
}

task<std::string, error> readlink(std::string_view path, event_loop& loop) {
    return fs_call<text_of>(loop, ::uv_fs_readlink, std::string(path));
}

task<std::string, error> realpath(std::string_view path, event_loop& loop) {
    return fs_call<text_of>(loop, ::uv_fs_realpath, std::string(path));
}

task<void, error> fchmod(int fd, int mode, event_loop& loop) {
    return fs_call(loop, ::uv_fs_fchmod, fd, mode);
}

task<void, error>
    chown(std::string_view path, std::uint32_t uid, std::uint32_t gid, event_loop& loop) {
    return fs_call(loop,
                   ::uv_fs_chown,
                   std::string(path),
                   static_cast<uv_uid_t>(uid),
                   static_cast<uv_gid_t>(gid));
}

task<void, error> fchown(int fd, std::uint32_t uid, std::uint32_t gid, event_loop& loop) {
    return fs_call(loop,
                   ::uv_fs_fchown,
                   fd,
                   static_cast<uv_uid_t>(uid),
                   static_cast<uv_gid_t>(gid));
}

task<void, error>
    lchown(std::string_view path, std::uint32_t uid, std::uint32_t gid, event_loop& loop) {
    return fs_call(loop,
                   ::uv_fs_lchown,
                   std::string(path),
                   static_cast<uv_uid_t>(uid),
                   static_cast<uv_gid_t>(gid));
}

task<fs_stats, error> statfs(std::string_view path, event_loop& loop) {
    return fs_call<fs_stats_of>(loop, ::uv_fs_statfs, std::string(path));
}

task<int, error> open(std::string_view path, int flags, int mode, event_loop& loop) {
    return fs_call<descriptor_of, close_descriptor>(loop,
                                                    ::uv_fs_open,
                                                    std::string(path),
                                                    flags,
                                                    mode);
}

task<std::size_t, error> read(int fd, std::span<char> buf, std::int64_t offset, event_loop& loop) {
    return fs_call<size_of>(loop, ::uv_fs_read, fd, uv::buffer_of(buf), 1U, offset);
}

task<std::size_t, error>
    write(int fd, std::span<const char> buf, std::int64_t offset, event_loop& loop) {
    return fs_call<size_of>(loop, ::uv_fs_write, fd, uv::buffer_of(buf), 1U, offset);
}

task<void, error> close(int fd, event_loop& loop) {
    return fs_call(loop, ::uv_fs_close, fd);
}

result<int> sync::open(std::string_view path, int flags, int mode) {
    auto fd = run_sync(::uv_fs_open, std::string(path).c_str(), flags, mode);
    if(!fd) {
        return outcome_error(fd.error());
    }
    return static_cast<int>(*fd);
}

result<std::size_t> sync::read(int fd, std::span<char> buf, std::int64_t offset) {
    auto uv_buf = uv::buffer_of(buf);
    return run_sync(::uv_fs_read, fd, &uv_buf, 1U, offset);
}

result<std::size_t> sync::write(int fd, std::span<const char> buf, std::int64_t offset) {
    auto uv_buf = uv::buffer_of(buf);
    return run_sync(::uv_fs_write, fd, &uv_buf, 1U, offset);
}

error sync::close(int fd) {
    auto closed = run_sync(::uv_fs_close, fd);
    return closed ? error() : closed.error();
}

result<std::string> sync::read_to_string(std::string_view path) {
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

}  // namespace kota::fs
