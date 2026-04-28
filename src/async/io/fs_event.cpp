#include "kota/async/io/fs_event.h"

#include <algorithm>
#include <atomic>
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
#include <dispatch/dispatch.h>
#include <fcntl.h>
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
    (IN_ATTRIB | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF |               \
     IN_MOVED_FROM | IN_MOVED_TO | IN_DONT_FOLLOW | IN_ONLYDIR | IN_EXCL_UNLINK)

// All buffer/map mutations happen on the event loop thread (on_poll callback on Linux,
// loop->post() lambdas on macOS/Windows), so no mutex is needed. Only `closed` is
// accessed cross-thread and uses std::atomic with acquire/release ordering.
struct fs_event::Self : std::enable_shared_from_this<Self> {
    event_loop* loop;
    timer debounce_timer;
    kota::event has_events{false};
    std::vector<change> buffer;
    std::chrono::milliseconds debounce_ms;
    std::atomic<bool> closed{false};
    bool recursive = false;
    std::string root_path;
    std::string file_filter;

    int inotify_fd = -1;
    uv_poll_t poll_handle{};
    bool poll_initialized = false;

    std::unordered_map<int, std::string> wd_to_path;
    std::unordered_map<std::string, int> path_to_wd;

    ~Self() {
        if(inotify_fd >= 0) {
            ::close(inotify_fd);
            inotify_fd = -1;
        }
        wd_to_path.clear();
        path_to_wd.clear();
    }

    void stop_inotify() {
        if(poll_initialized) {
            uv_poll_stop(&poll_handle);
            auto prevent_destroy = new std::shared_ptr<Self>(shared_from_this());
            poll_handle.data = prevent_destroy;
            uv_close(reinterpret_cast<uv_handle_t*>(&poll_handle), [](uv_handle_t* h) {
                delete static_cast<std::shared_ptr<fs_event::Self>*>(h->data);
            });
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
        if(closed.load(std::memory_order_acquire))
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

    void update_renamed_paths(const std::string& old_path, const std::string& new_path) {
        std::string old_prefix = old_path + "/";
        std::vector<std::pair<int, std::string>> updates;
        for(auto& [wd, p]: wd_to_path) {
            if(p == old_path) {
                updates.push_back({wd, new_path});
            } else if(p.size() > old_prefix.size() &&
                      p.compare(0, old_prefix.size(), old_prefix) == 0) {
                updates.push_back({wd, new_path + p.substr(old_path.size())});
            }
        }
        for(auto& [wd, new_p]: updates) {
            path_to_wd.erase(wd_to_path[wd]);
            wd_to_path[wd] = new_p;
            path_to_wd[new_p] = wd;
        }
    }

    void process_inotify_events() {
        alignas(struct inotify_event) char buf[8192];
        for(;;) {
            ssize_t n = ::read(inotify_fd, buf, sizeof(buf));
            if(n <= 0)
                break;

            struct raw_event {
                std::string path;
                uint32_t mask;
                uint32_t cookie;
                bool is_dir;
                int wd;
                bool consumed = false;
            };

            std::vector<raw_event> events;
            std::unordered_map<uint32_t, size_t> move_from_cookies;

            for(char* p = buf; p < buf + n;) {
                auto* ev = reinterpret_cast<struct inotify_event*>(p);
                p += sizeof(struct inotify_event) + ev->len;

                if(ev->mask & IN_Q_OVERFLOW) {
                    push_event(change{{}, effect::overflow, {}});
                    continue;
                }

                std::string path = build_path(ev->wd, ev->name, ev->len);
                if(path.empty())
                    continue;

                bool is_dir = (ev->mask & IN_ISDIR) != 0;
                events.push_back({std::move(path), ev->mask, ev->cookie, is_dir, ev->wd});

                if((ev->mask & IN_MOVED_FROM) && ev->cookie != 0) {
                    move_from_cookies[ev->cookie] = events.size() - 1;
                }
            }

            for(size_t i = 0; i < events.size(); i++) {
                auto& e = events[i];
                if(e.consumed)
                    continue;

                if(e.mask & IN_MOVED_TO) {
                    auto it = move_from_cookies.find(e.cookie);
                    if(it != move_from_cookies.end()) {
                        auto& from = events[it->second];
                        from.consumed = true;
                        push_event(change{e.path, effect::rename, from.path});
                        if(e.is_dir && recursive) {
                            update_renamed_paths(from.path, e.path);
                            scan_directory(e.path);
                        }
                    } else {
                        push_event(change{e.path, effect::create, {}});
                        if(e.is_dir && recursive) {
                            scan_directory(e.path);
                        }
                    }
                } else if(e.mask & IN_CREATE) {
                    push_event(change{e.path, effect::create, {}});
                    if(e.is_dir && recursive) {
                        scan_directory(e.path);
                    }
                } else if(e.mask & (IN_MODIFY | IN_ATTRIB)) {
                    push_event(change{e.path, effect::modify, {}});
                } else if(e.mask & IN_MOVED_FROM) {
                    // Unmatched MOVED_FROM — file moved out of watched tree
                    push_event(change{e.path, effect::destroy, {}});
                    if(e.is_dir) {
                        auto pit = path_to_wd.find(e.path);
                        if(pit != path_to_wd.end()) {
                            wd_to_path.erase(pit->second);
                            path_to_wd.erase(pit);
                        }
                    }
                } else if(e.mask & (IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF)) {
                    bool is_self = (e.mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0;
                    if(!is_self || e.path == root_path) {
                        push_event(change{e.path, effect::destroy, {}});
                    }
                    if(!is_self && e.is_dir) {
                        auto pit = path_to_wd.find(e.path);
                        if(pit != path_to_wd.end()) {
                            wd_to_path.erase(pit->second);
                            path_to_wd.erase(pit);
                        }
                    }
                }

                if(e.mask & IN_IGNORED) {
                    auto wit = wd_to_path.find(e.wd);
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
        if(status < 0 || self->closed.load(std::memory_order_acquire))
            return;
        if(events & UV_READABLE) {
            self->process_inotify_events();
        }
    }
};

// ── macOS: FSEvents + CFRunLoop thread ──────────────────────────────

#elif defined(__APPLE__)

namespace {

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

constexpr FSEventStreamEventFlags IGNORED_FLAGS =
    kFSEventStreamEventFlagItemIsHardlink | kFSEventStreamEventFlagItemIsLastHardlink |
    kFSEventStreamEventFlagItemIsSymlink | kFSEventStreamEventFlagItemIsDir |
    kFSEventStreamEventFlagItemIsFile
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
    | kFSEventStreamEventFlagItemCloned
#endif
    ;

}  // namespace

struct fs_event::Self : std::enable_shared_from_this<Self> {
    event_loop* loop;
    timer debounce_timer;
    kota::event has_events{false};
    std::vector<change> buffer;
    std::chrono::milliseconds debounce_ms;
    std::atomic<bool> closed{false};
    bool recursive = false;
    std::string root_path;
    std::string file_filter;

    FSEventStreamRef stream = nullptr;
    dispatch_queue_t dispatch_queue = nullptr;

    ~Self() {
        stop_fsevents();
    }

    void stop_fsevents() {
        if(stream) {
            FSEventStreamStop(stream);
            FSEventStreamInvalidate(stream);
            FSEventStreamRelease(stream);
            stream = nullptr;
        }

        if(dispatch_queue) {
            dispatch_release(dispatch_queue);
            dispatch_queue = nullptr;
        }
    }

    void push_event(change c) {
        if(closed.load(std::memory_order_acquire))
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
        auto* raw = static_cast<Self*>(info);
        auto shared = raw->shared_from_this();

        if(shared->closed.load(std::memory_order_acquire))
            return;

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
            if(flags[i] & kFSEventStreamEventFlagRootChanged) {
                struct stat st;
                if(stat(shared->root_path.c_str(), &st) != 0) {
                    changes.push_back(change{shared->root_path, effect::destroy, {}});
                }
                continue;
            }

            if((flags[i] & ~IGNORED_FLAGS) == 0) {
                continue;
            }

            std::string path = paths[i];
            if(!path.empty() && path.back() == '/') {
                path.pop_back();
            }

            if(!shared->recursive) {
                size_t prefix_len = shared->root_path.size();
                if(shared->root_path.back() != '/')
                    prefix_len += 1;
                if(path.size() <= prefix_len)
                    continue;
                auto relative = std::string_view(path).substr(prefix_len);
                if(relative.find('/') != std::string_view::npos) {
                    continue;
                }
            }

            if(shared->closed.load(std::memory_order_acquire))
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
                } else if(is_modified) {
                    changes.push_back(change{std::move(path), effect::modify, {}});
                } else {
                    changes.push_back(change{std::move(path), effect::create, {}});
                }
            }
        }

        if(changes.empty())
            return;

        if(shared->closed.load(std::memory_order_acquire))
            return;

        std::weak_ptr<Self> weak = shared;
        shared->loop->post([weak, changes = std::move(changes)]() mutable {
            if(auto s = weak.lock()) {
                for(auto& c: changes) {
                    s->push_event(std::move(c));
                }
            }
        });
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
    std::atomic<bool> closed{false};
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
        stop_win();
    }

    void stop_win() {
        if(!worker_thread.joinable())
            return;

        QueueUserAPC(
            [](ULONG_PTR param) {
                auto* s = reinterpret_cast<Self*>(param);
                s->running = false;
                if(s->dir_handle != INVALID_HANDLE_VALUE) {
                    CancelIoEx(s->dir_handle, nullptr);
                    CloseHandle(s->dir_handle);
                    s->dir_handle = INVALID_HANDLE_VALUE;
                }
            },
            worker_thread.native_handle(),
            reinterpret_cast<ULONG_PTR>(this));
        worker_thread.join();
    }

    void push_event(change c) {
        if(closed.load(std::memory_order_acquire))
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
                std::weak_ptr<Self> weak = shared_from_this();
                loop->post([weak]() {
                    if(auto s = weak.lock()) {
                        s->push_event(change{{}, effect::overflow, {}});
                    }
                });
                poll();
                return;
            }
            case ERROR_ACCESS_DENIED: {
                DWORD attrs = GetFileAttributesW(root_wpath.c_str());
                bool is_dir =
                    attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
                if(!is_dir) {
                    std::weak_ptr<Self> weak = shared_from_this();
                    std::string path = root_path;
                    loop->post([weak, path = std::move(path)]() {
                        if(auto s = weak.lock()) {
                            s->push_event(change{std::string(path), effect::destroy, {}});
                        }
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

        std::swap(read_buffer, write_buffer);
        poll();

        if(num_bytes == 0)
            return;

        std::vector<change> changes;
        std::string pending_old_name;
        BYTE* ptr = read_buffer.data();
        for(;;) {
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(ptr);
            size_t name_chars = info->FileNameLength / sizeof(wchar_t);
            std::string name = wide_to_utf8(info->FileName, name_chars);
            std::replace(name.begin(), name.end(), '\\', '/');
            std::string full = root_path + "/" + name;

            switch(info->Action) {
                case FILE_ACTION_ADDED:
                    changes.push_back(change{std::move(full), effect::create, {}});
                    break;
                case FILE_ACTION_REMOVED:
                    changes.push_back(change{std::move(full), effect::destroy, {}});
                    break;
                case FILE_ACTION_MODIFIED: {
                    std::wstring wfull =
                        root_wpath + L"/" + std::wstring(info->FileName, name_chars);
                    DWORD attrs = GetFileAttributesW(wfull.c_str());
                    if(attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
                        break;
                    changes.push_back(change{std::move(full), effect::modify, {}});
                    break;
                }
                case FILE_ACTION_RENAMED_OLD_NAME:
                    if(!pending_old_name.empty()) {
                        changes.push_back(change{std::move(pending_old_name), effect::destroy, {}});
                    }
                    pending_old_name = std::move(full);
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                    if(!pending_old_name.empty()) {
                        changes.push_back(
                            change{std::move(full), effect::rename, std::move(pending_old_name)});
                        pending_old_name.clear();
                    } else {
                        changes.push_back(change{std::move(full), effect::create, {}});
                    }
                    break;
            }

            if(info->NextEntryOffset == 0)
                break;
            ptr += info->NextEntryOffset;
        }

        if(!pending_old_name.empty()) {
            changes.push_back(change{std::move(pending_old_name), effect::destroy, {}});
        }

        if(!changes.empty()) {
            std::weak_ptr<Self> weak = shared_from_this();
            loop->post([weak, changes = std::move(changes)]() mutable {
                if(auto s = weak.lock()) {
                    for(auto& c: changes) {
                        s->push_event(std::move(c));
                    }
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
    stop();
}

fs_event::fs_event(fs_event&&) noexcept = default;

fs_event& fs_event::operator=(fs_event&& other) noexcept {
    if(this != &other) {
        stop();
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

#if defined(_WIN32)
    std::replace(s->root_path.begin(), s->root_path.end(), '\\', '/');
#endif

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
        s->stop_inotify();
        return outcome_error(error(rc));
    }

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

    s->dispatch_queue = dispatch_queue_create("kota.fs_event", DISPATCH_QUEUE_SERIAL);
    if(!s->dispatch_queue) {
        FSEventStreamRelease(s->stream);
        s->stream = nullptr;
        return outcome_error(error::unknown_error);
    }

    FSEventStreamSetDispatchQueue(s->stream, s->dispatch_queue);
    FSEventStreamStart(s->stream);

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

    BY_HANDLE_FILE_INFORMATION file_info;
    if(!GetFileInformationByHandle(s->dir_handle, &file_info) ||
       !(file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(s->dir_handle);
        s->dir_handle = INVALID_HANDLE_VALUE;
        return outcome_error(error::invalid_argument);
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

result<fs_event> fs_event::create(std::string_view path, event_loop& loop) {
    return create(path, options{}, loop);
}

task<std::vector<fs_event::change>, error> fs_event::next() {
    if(!self || self->closed.load(std::memory_order_acquire)) {
        co_await fail(error::invalid_argument);
    }

    while(true) {
        while(self->buffer.empty()) {
            self->has_events.reset();
            co_await self->has_events.wait();

            if(self->closed.load(std::memory_order_acquire)) {
                co_await fail(error::operation_aborted);
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

void fs_event::stop() {
    if(!self) {
        return;
    }

    self->closed.store(true, std::memory_order_release);
    self->has_events.set();
    self->debounce_timer.stop();

#if defined(__linux__)
    self->stop_inotify();
#elif defined(__APPLE__)
    self->stop_fsevents();
#elif defined(_WIN32)
    self->stop_win();
#endif
}

}  // namespace kota
