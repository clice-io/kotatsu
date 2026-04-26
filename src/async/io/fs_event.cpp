#include "kota/async/io/fs_event.h"

#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <utility>

#include "kota/async/io/loop.h"
#include "kota/async/io/watcher.h"
#include "kota/async/runtime/sync.h"

#if defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>
#include <uv.h>
#elif defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uv.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <thread>
#include <uv.h>
#include <windows.h>
#endif

namespace kota {

// ── Linux: inotify + uv_poll_t ─────────────────────────────────────

#if defined(__linux__)

#define INOTIFY_MASK                                                                               \
    IN_ATTRIB | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF |                \
        IN_MOVED_FROM | IN_MOVED_TO | IN_DONT_FOLLOW | IN_ONLYDIR | IN_EXCL_UNLINK

struct fs_event::Self : std::enable_shared_from_this<Self> {
    event_loop* loop;
    timer debounce_timer;
    kota::event has_events{false};
    std::vector<change> buffer;
    std::chrono::milliseconds debounce_ms;
    bool closed = false;
    bool recursive = false;
    std::string root_path;
    std::string file_filter;

    int inotify_fd = -1;
    uv_poll_t poll_handle{};
    bool poll_initialized = false;

    std::unordered_map<int, std::string> wd_to_path;
    std::unordered_map<std::string, int> path_to_wd;

    ~Self() {
        close_inotify();
    }

    void close_inotify() {
        if(poll_initialized) {
            uv_poll_stop(&poll_handle);
            uv_close(reinterpret_cast<uv_handle_t*>(&poll_handle), nullptr);
            poll_initialized = false;
        }
        if(inotify_fd >= 0) {
            ::close(inotify_fd);
            inotify_fd = -1;
        }
        wd_to_path.clear();
        path_to_wd.clear();
    }

    bool add_watch(const std::string& path) {
        int wd = inotify_add_watch(inotify_fd, path.c_str(), INOTIFY_MASK);
        if(wd < 0) {
            return false;
        }
        wd_to_path[wd] = path;
        path_to_wd[path] = wd;
        return true;
    }

    void scan_directory(const std::string& path) {
        if(!add_watch(path)) {
            return;
        }
        if(!recursive) {
            return;
        }
        std::error_code ec;
        for(auto& entry: std::filesystem::directory_iterator(path, ec)) {
            if(ec)
                break;
            if(entry.is_directory(ec) && !ec) {
                scan_directory(entry.path().string());
            }
        }
    }

    void push_event(change c) {
        if(closed)
            return;
        buffer.push_back(std::move(c));
        has_events.set();
    }

    std::string build_path(int wd, const char* name, uint32_t len) {
        auto it = wd_to_path.find(wd);
        if(it == wd_to_path.end())
            return {};
        if(name && len > 0) {
            return it->second + "/" + name;
        }
        return it->second;
    }

    void process_inotify_events() {
        alignas(struct inotify_event) char buf[8192];
        for(;;) {
            ssize_t n = ::read(inotify_fd, buf, sizeof(buf));
            if(n <= 0)
                break;

            for(char* p = buf; p < buf + n;) {
                auto* ev = reinterpret_cast<struct inotify_event*>(p);
                p += sizeof(struct inotify_event) + ev->len;

                if(ev->mask & IN_Q_OVERFLOW) {
                    push_event(change{{}, effect::overflow, {}});
                    continue;
                }

                bool is_dir = (ev->mask & IN_ISDIR) != 0;
                std::string path = build_path(ev->wd, ev->name, ev->len);
                if(path.empty())
                    continue;

                if(ev->mask & (IN_CREATE | IN_MOVED_TO)) {
                    push_event(change{path, effect::create, {}});
                    if(is_dir && recursive) {
                        scan_directory(path);
                    }
                } else if(ev->mask & (IN_MODIFY | IN_ATTRIB)) {
                    push_event(change{path, effect::modify, {}});
                } else if(ev->mask & (IN_DELETE | IN_DELETE_SELF | IN_MOVED_FROM | IN_MOVE_SELF)) {
                    bool is_self = (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0;
                    // Self events on non-root directories: the parent's
                    // IN_DELETE / IN_MOVED_FROM already emitted the event.
                    // For MOVE_SELF the watch stays alive (inode unchanged),
                    // the MOVED_TO handler re-registers the new path.
                    if(!is_self || path == root_path) {
                        push_event(change{path, effect::destroy, {}});
                    }
                    if(!is_self && is_dir) {
                        auto pit = path_to_wd.find(path);
                        if(pit != path_to_wd.end()) {
                            wd_to_path.erase(pit->second);
                            path_to_wd.erase(pit);
                        }
                    }
                }

                if(ev->mask & IN_IGNORED) {
                    auto wit = wd_to_path.find(ev->wd);
                    if(wit != wd_to_path.end()) {
                        path_to_wd.erase(wit->second);
                        wd_to_path.erase(wit);
                    }
                }
            }
        }
    }

    static void on_poll(uv_poll_t* handle, int status, int events) {
        auto* self = static_cast<Self*>(handle->data);
        if(status < 0 || self->closed)
            return;
        if(events & UV_READABLE) {
            self->process_inotify_events();
        }
    }
};

// ── macOS: FSEvents + CFRunLoop thread ──────────────────────────────

#elif defined(__APPLE__)

namespace {

// macOS has a case insensitive file system by default. Use F_GETPATH
// to get the canonical path and compare it with the input path to
// detect case-only renames.
bool path_exists(const char* path) {
    int fd = open(path, O_RDONLY | O_SYMLINK);
    if(fd == -1)
        return false;
    char buf[PATH_MAX];
    if(fcntl(fd, F_GETPATH, buf) == -1) {
        ::close(fd);
        return false;
    }
    bool res = strncmp(path, buf, PATH_MAX) == 0;
    ::close(fd);
    return res;
}

constexpr auto IGNORED_FLAGS = kFSEventStreamEventFlagItemIsHardlink |
                               kFSEventStreamEventFlagItemIsLastHardlink |
                               kFSEventStreamEventFlagItemIsSymlink |
                               kFSEventStreamEventFlagItemIsDir | kFSEventStreamEventFlagItemIsFile;

}  // namespace

struct fs_event::Self : std::enable_shared_from_this<Self> {
    event_loop* loop;
    timer debounce_timer;
    kota::event has_events{false};
    std::vector<change> buffer;
    std::chrono::milliseconds debounce_ms;
    bool closed = false;
    bool recursive = false;
    std::string root_path;
    std::string file_filter;

    FSEventStreamRef stream = nullptr;
    CFRunLoopRef cf_run_loop = nullptr;
    pthread_t cf_thread{};
    bool thread_started = false;
    pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;

    ~Self() {
        close_fsevents();
        pthread_mutex_destroy(&start_mutex);
        pthread_cond_destroy(&start_cond);
    }

    void close_fsevents() {
        if(stream && cf_run_loop) {
            FSEventStreamStop(stream);
            FSEventStreamInvalidate(stream);
            FSEventStreamRelease(stream);
            stream = nullptr;

            CFRunLoopStop(cf_run_loop);
        }

        if(thread_started) {
            pthread_join(cf_thread, nullptr);
            thread_started = false;
        }

        cf_run_loop = nullptr;
    }

    void push_event(change c) {
        if(closed)
            return;
        buffer.push_back(std::move(c));
        has_events.set();
    }

    static void fsevents_callback(ConstFSEventStreamRef,
                                  void* info,
                                  size_t num_events,
                                  void* event_paths,
                                  const FSEventStreamEventFlags flags[],
                                  const FSEventStreamEventId[]) {
        auto* self = static_cast<Self*>(info);
        auto** paths = static_cast<char**>(event_paths);

        std::vector<change> changes;
        changes.reserve(num_events);

        for(size_t i = 0; i < num_events; ++i) {
            if(flags[i] & kFSEventStreamEventFlagMustScanSubDirs) {
                changes.push_back(change{{}, effect::overflow, {}});
                continue;
            }

            if(flags[i] & kFSEventStreamEventFlagHistoryDone)
                continue;
            if(flags[i] & (kFSEventStreamEventFlagMount | kFSEventStreamEventFlagUnmount))
                continue;
            if(flags[i] & kFSEventStreamEventFlagRootChanged)
                continue;

            if((flags[i] & ~IGNORED_FLAGS) == 0) {
                continue;
            }

            std::string path = paths[i];
            if(!path.empty() && path.back() == '/') {
                path.pop_back();
            }

            if(!self->recursive) {
                if(path.size() <= self->root_path.size() + 1)
                    continue;
                auto relative = std::string_view(path).substr(self->root_path.size() + 1);
                if(relative.find('/') != std::string_view::npos) {
                    continue;
                }
            }

            if(self->closed)
                continue;

            bool is_created = (flags[i] & kFSEventStreamEventFlagItemCreated) != 0;
            bool is_removed = (flags[i] & kFSEventStreamEventFlagItemRemoved) != 0;
            bool is_renamed = (flags[i] & kFSEventStreamEventFlagItemRenamed) != 0;
            bool is_modified = (flags[i] & (kFSEventStreamEventFlagItemModified |
                                            kFSEventStreamEventFlagItemInodeMetaMod |
                                            kFSEventStreamEventFlagItemFinderInfoMod |
                                            kFSEventStreamEventFlagItemChangeOwner |
                                            kFSEventStreamEventFlagItemXattrMod)) != 0;

            if(is_created && !(is_removed || is_modified || is_renamed)) {
                changes.push_back(change{std::move(path), effect::create, {}});
            } else if(is_removed && !(is_created || is_modified || is_renamed)) {
                changes.push_back(change{std::move(path), effect::destroy, {}});
            } else if(is_modified && !(is_created || is_removed || is_renamed)) {
                changes.push_back(change{std::move(path), effect::modify, {}});
            } else {
                struct stat st;
                if(stat(path.c_str(), &st) != 0 || !path_exists(paths[i])) {
                    changes.push_back(change{std::move(path), effect::destroy, {}});
                } else if(is_modified && !is_created) {
                    changes.push_back(change{std::move(path), effect::modify, {}});
                } else {
                    changes.push_back(change{std::move(path), effect::create, {}});
                }
            }
        }

        if(changes.empty())
            return;

        auto shared = self->shared_from_this();
        self->loop->post([shared, changes = std::move(changes)]() mutable {
            for(auto& c: changes) {
                shared->push_event(std::move(c));
            }
        });
    }

    static void* cf_thread_entry(void* arg) {
        auto* self = static_cast<Self*>(arg);

        pthread_mutex_lock(&self->start_mutex);
        self->cf_run_loop = CFRunLoopGetCurrent();

        FSEventStreamScheduleWithRunLoop(self->stream, self->cf_run_loop, kCFRunLoopDefaultMode);
        FSEventStreamStart(self->stream);

        pthread_cond_signal(&self->start_cond);
        pthread_mutex_unlock(&self->start_mutex);

        CFRunLoopRun();
        return nullptr;
    }
};

// ── Windows: ReadDirectoryChangesW + APC completion routine ────────

#elif defined(_WIN32)

struct fs_event::Self : std::enable_shared_from_this<Self> {
    event_loop* loop;
    timer debounce_timer;
    kota::event has_events{false};
    std::vector<change> buffer;
    std::chrono::milliseconds debounce_ms;
    bool closed = false;
    bool recursive = false;
    std::string root_path;
    std::string file_filter;
    std::wstring root_wpath;

    HANDLE dir_handle = INVALID_HANDLE_VALUE;
    std::thread worker_thread;
    bool running = false;

    constexpr static DWORD DEFAULT_BUF_SIZE = 1024 * 1024;
    constexpr static DWORD NETWORK_BUF_SIZE = 64 * 1024;

    std::vector<BYTE> read_buffer;
    std::vector<BYTE> write_buffer;
    OVERLAPPED overlapped{};

    ~Self() {
        close_win();
    }

    void close_win() {
        if(!worker_thread.joinable())
            return;

        QueueUserAPC(
            [](ULONG_PTR param) {
                auto* s = reinterpret_cast<Self*>(param);
                s->running = false;
                if(s->dir_handle != INVALID_HANDLE_VALUE) {
                    CancelIo(s->dir_handle);
                    CloseHandle(s->dir_handle);
                    s->dir_handle = INVALID_HANDLE_VALUE;
                }
            },
            worker_thread.native_handle(),
            reinterpret_cast<ULONG_PTR>(this));
        worker_thread.join();
    }

    void push_event(change c) {
        if(closed)
            return;
        buffer.push_back(std::move(c));
        has_events.set();
    }

    static std::string wide_to_utf8(const wchar_t* str, size_t len) {
        if(len == 0)
            return {};
        int size = WideCharToMultiByte(CP_UTF8,
                                       0,
                                       str,
                                       static_cast<int>(len),
                                       nullptr,
                                       0,
                                       nullptr,
                                       nullptr);
        if(size <= 0)
            return {};
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8,
                            0,
                            str,
                            static_cast<int>(len),
                            result.data(),
                            size,
                            nullptr,
                            nullptr);
        return result;
    }

    void poll() {
        if(!running)
            return;

        BOOL ok = ReadDirectoryChangesW(dir_handle,
                                        write_buffer.data(),
                                        static_cast<DWORD>(write_buffer.size()),
                                        recursive ? TRUE : FALSE,
                                        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                            FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                        nullptr,
                                        &overlapped,
                                        [](DWORD error_code, DWORD num_bytes, LPOVERLAPPED ov) {
                                            auto* self = reinterpret_cast<Self*>(ov->hEvent);
                                            self->on_completion(error_code, num_bytes);
                                        });

        if(!ok) {
            running = false;
        }
    }

    void on_completion(DWORD error_code, DWORD num_bytes) {
        if(!running)
            return;

        switch(error_code) {
            case ERROR_OPERATION_ABORTED: return;
            case ERROR_INVALID_PARAMETER:
                read_buffer.resize(NETWORK_BUF_SIZE);
                write_buffer.resize(NETWORK_BUF_SIZE);
                poll();
                return;
            case ERROR_NOTIFY_ENUM_DIR: {
                auto shared = shared_from_this();
                loop->post([shared]() { shared->push_event(change{{}, effect::overflow, {}}); });
                return;
            }
            case ERROR_ACCESS_DENIED: {
                DWORD attrs = GetFileAttributesW(root_wpath.c_str());
                bool is_dir =
                    attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
                if(!is_dir) {
                    auto shared = shared_from_this();
                    loop->post([shared]() {
                        shared->push_event(change{shared->root_path, effect::destroy, {}});
                    });
                    running = false;
                    return;
                }
                return;
            }
            default:
                if(error_code != ERROR_SUCCESS)
                    return;
        }

        // Swap buffers and re-poll
        std::swap(read_buffer, write_buffer);
        poll();

        if(num_bytes == 0)
            return;

        // Process events from read_buffer
        std::vector<change> changes;
        BYTE* ptr = read_buffer.data();
        for(;;) {
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(ptr);
            size_t name_chars = info->FileNameLength / sizeof(wchar_t);
            std::string name = wide_to_utf8(info->FileName, name_chars);
            std::replace(name.begin(), name.end(), '\\', '/');
            std::string full = root_path + "/" + name;

            switch(info->Action) {
                case FILE_ACTION_ADDED:
                case FILE_ACTION_RENAMED_NEW_NAME:
                    changes.push_back(change{std::move(full), effect::create, {}});
                    break;
                case FILE_ACTION_REMOVED:
                case FILE_ACTION_RENAMED_OLD_NAME:
                    changes.push_back(change{std::move(full), effect::destroy, {}});
                    break;
                case FILE_ACTION_MODIFIED:
                    changes.push_back(change{std::move(full), effect::modify, {}});
                    break;
            }

            if(info->NextEntryOffset == 0)
                break;
            ptr += info->NextEntryOffset;
        }

        if(!changes.empty()) {
            auto shared = shared_from_this();
            loop->post([shared, changes = std::move(changes)]() mutable {
                for(auto& c: changes) {
                    shared->push_event(std::move(c));
                }
            });
        }
    }

    void worker_entry() {
        poll();
        while(running) {
            SleepEx(INFINITE, TRUE);
        }
    }
};

#endif

// ── Common implementation ───────────────────────────────────────────

fs_event::fs_event() noexcept = default;

fs_event::fs_event(std::shared_ptr<Self> self) noexcept : self(std::move(self)) {}

fs_event::~fs_event() {
    close();
}

fs_event::fs_event(fs_event&&) noexcept = default;

fs_event& fs_event::operator=(fs_event&& other) noexcept {
    if(this != &other) {
        close();
        self = std::move(other.self);
    }
    return *this;
}

result<fs_event> fs_event::create(std::string_view path, options opts, event_loop& loop) {
    std::error_code ec;
    std::filesystem::path fs_path(path);
    auto status = std::filesystem::status(fs_path, ec);

    auto s = std::make_shared<Self>();
    s->loop = &loop;
    s->debounce_ms = opts.debounce;

    if(!ec && std::filesystem::is_directory(status)) {
        s->recursive = opts.recursive;
        auto canonical = std::filesystem::canonical(fs_path, ec);
        if(ec) {
            return outcome_error(error::no_such_file_or_directory);
        }
        s->root_path = canonical.string();
    } else {
        auto parent = fs_path.parent_path();
        auto filename = fs_path.filename().string();
        if(filename.empty()) {
            return outcome_error(error::invalid_argument);
        }
        auto parent_status = std::filesystem::status(parent, ec);
        if(ec || !std::filesystem::is_directory(parent_status)) {
            return outcome_error(error::no_such_file_or_directory);
        }
        s->recursive = false;
        auto canonical = std::filesystem::canonical(parent, ec);
        if(ec) {
            return outcome_error(error::no_such_file_or_directory);
        }
        s->root_path = canonical.string();
        s->file_filter = std::move(filename);
    }

    s->debounce_timer = timer::create(loop);

#if defined(__linux__)
    s->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if(s->inotify_fd < 0) {
        return outcome_error(error::unknown_error);
    }

    s->scan_directory(s->root_path);
    if(s->wd_to_path.empty()) {
        ::close(s->inotify_fd);
        s->inotify_fd = -1;
        return outcome_error(error::permission_denied);
    }

    uv_loop_t& uv = loop;
    int rc = uv_poll_init(&uv, &s->poll_handle, s->inotify_fd);
    if(rc < 0) {
        ::close(s->inotify_fd);
        s->inotify_fd = -1;
        return outcome_error(error(rc));
    }

    s->poll_handle.data = s.get();
    s->poll_initialized = true;

    rc = uv_poll_start(&s->poll_handle, UV_READABLE, Self::on_poll);
    if(rc < 0) {
        s->close_inotify();
        return outcome_error(error(rc));
    }

    // Keep the poll handle unreferenced so it doesn't keep the loop alive on its own
    uv_unref(reinterpret_cast<uv_handle_t*>(&s->poll_handle));

#elif defined(__APPLE__)
    CFStringRef cf_path =
        CFStringCreateWithCString(nullptr, s->root_path.c_str(), kCFStringEncodingUTF8);
    if(!cf_path) {
        return outcome_error(error::unknown_error);
    }

    CFArrayRef paths_to_watch =
        CFArrayCreate(nullptr, reinterpret_cast<const void**>(&cf_path), 1, &kCFTypeArrayCallBacks);
    CFRelease(cf_path);

    if(!paths_to_watch) {
        return outcome_error(error::unknown_error);
    }

    FSEventStreamContext ctx{};
    ctx.info = s.get();

    double latency_sec = 0.001;
    FSEventStreamCreateFlags fs_flags = kFSEventStreamCreateFlagFileEvents;

    s->stream = FSEventStreamCreate(nullptr,
                                    &Self::fsevents_callback,
                                    &ctx,
                                    paths_to_watch,
                                    kFSEventStreamEventIdSinceNow,
                                    latency_sec,
                                    fs_flags);

    CFRelease(paths_to_watch);

    if(!s->stream) {
        return outcome_error(error::unknown_error);
    }

    // Start the CF thread
    pthread_mutex_lock(&s->start_mutex);
    int pt_err = pthread_create(&s->cf_thread, nullptr, Self::cf_thread_entry, s.get());
    if(pt_err != 0) {
        pthread_mutex_unlock(&s->start_mutex);
        FSEventStreamRelease(s->stream);
        s->stream = nullptr;
        return outcome_error(error::unknown_error);
    }
    s->thread_started = true;
    pthread_cond_wait(&s->start_cond, &s->start_mutex);
    pthread_mutex_unlock(&s->start_mutex);

#elif defined(_WIN32)
    std::wstring wpath;
    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, s->root_path.c_str(), -1, nullptr, 0);
        if(wlen <= 0)
            return outcome_error(error::invalid_argument);
        wpath.resize(wlen - 1);
        MultiByteToWideChar(CP_UTF8, 0, s->root_path.c_str(), -1, wpath.data(), wlen);
    }

    s->dir_handle = CreateFileW(wpath.c_str(),
                                FILE_LIST_DIRECTORY,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                nullptr);

    if(s->dir_handle == INVALID_HANDLE_VALUE) {
        return outcome_error(error::no_such_file_or_directory);
    }

    s->root_wpath = std::move(wpath);
    s->read_buffer.resize(Self::DEFAULT_BUF_SIZE);
    s->write_buffer.resize(Self::DEFAULT_BUF_SIZE);
    ZeroMemory(&s->overlapped, sizeof(OVERLAPPED));
    s->overlapped.hEvent = reinterpret_cast<HANDLE>(s.get());
    s->running = true;

    auto shared_for_thread = s;
    s->worker_thread = std::thread([shared_for_thread]() { shared_for_thread->worker_entry(); });

#endif

    return fs_event(std::move(s));
}

task<std::vector<fs_event::change>, error> fs_event::next() {
    if(!self || self->closed) {
        co_await fail(error::invalid_argument);
    }

    while(true) {
        while(self->buffer.empty()) {
            self->has_events.reset();
            co_await self->has_events.wait();

            if(self->closed) {
                co_await fail(error::software_caused_connection_abort);
            }
        }

        self->debounce_timer.start(self->debounce_ms);
        co_await self->debounce_timer.wait();

        auto batch = std::exchange(self->buffer, {});

        if(self->file_filter.empty()) {
            co_return batch;
        }

        std::vector<change> filtered;
        bool has_create = false;
        for(auto& c: batch) {
            auto slash = c.path.rfind('/');
            std::string_view name = (slash != std::string::npos)
                                        ? std::string_view(c.path).substr(slash + 1)
                                        : std::string_view(c.path);
            if(name != self->file_filter)
                continue;

            if(c.type == effect::create)
                has_create = true;
            filtered.push_back(std::move(c));
        }

        if(filtered.empty())
            continue;

        // Coalesce: create takes priority over modify in the same batch
        if(has_create) {
            bool any_destroy = false;
            for(auto& c: filtered) {
                if(c.type == effect::destroy) {
                    any_destroy = true;
                    break;
                }
            }
            if(!any_destroy) {
                for(auto& c: filtered) {
                    if(c.type == effect::modify)
                        c.type = effect::create;
                }
            }
        }

        co_return filtered;
    }
}

void fs_event::close() {
    if(!self) {
        return;
    }

#if defined(__linux__)
    self->close_inotify();
#elif defined(__APPLE__)
    self->close_fsevents();
#elif defined(_WIN32)
    self->close_win();
#endif

    self->closed = true;
    self->has_events.set();
    self->debounce_timer.stop();
}

}  // namespace kota
