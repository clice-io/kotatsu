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
#include <cerrno>
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

// Common state shared by all three platform Self structs.
//
// Thread safety: `closed` is atomic and may be read/written from any thread.
// All other fields are only accessed from the event-loop thread except during
// platform callbacks (macOS dispatch queue, Windows worker thread), which
// only read `root_path`, `recursive`, and `loop` (all immutable after create)
// and call post_change()/post_changes() to marshal events to the loop thread.
struct fs_event_base {
    event_loop* loop;
    timer debounce_timer;
    event has_events{false};
    // Accessed only on the event-loop thread; guarded by debounce_timer.
    std::vector<fs_event::change> buffer;
    std::chrono::milliseconds debounce_ms;
    std::atomic<bool> closed{false};
    // Immutable after create().
    bool recursive = false;
    std::string root_path;
    std::string file_filter;

    void push_event(fs_event::change c) {
        if(closed.load(std::memory_order_acquire))
            return;
        buffer.push_back(std::move(c));
        has_events.set();
    }

    void post_change(std::weak_ptr<fs_event_base> weak, fs_event::change c) {
        loop->post([weak = std::move(weak), c = std::move(c)]() mutable {
            if(auto s = weak.lock()) {
                s->push_event(std::move(c));
            }
        });
    }

    void post_changes(std::weak_ptr<fs_event_base> weak, std::vector<fs_event::change> changes) {
        loop->post([weak = std::move(weak), changes = std::move(changes)]() mutable {
            if(auto s = weak.lock()) {
                for(auto& c: changes) {
                    s->push_event(std::move(c));
                }
            }
        });
    }
};

#if defined(__linux__)

namespace {

constexpr uint32_t inotify_mask = IN_ATTRIB | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY |
                                  IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO | IN_DONT_FOLLOW |
                                  IN_ONLYDIR | IN_EXCL_UNLINK;

}  // namespace

struct fs_event::Self : fs_event_base, std::enable_shared_from_this<Self> {
    int inotify_fd = -1;
    uv_poll_t poll_handle{};
    bool poll_initialized = false;

    std::unordered_map<int, std::string> wd_to_path;
    std::unordered_map<std::string, int> path_to_wd;

    // Safety net: stop_platform() handles the full teardown; this only
    // closes the fd in case Self outlives the fs_event wrapper.
    ~Self() {
        if(inotify_fd >= 0) {
            ::close(inotify_fd);
            inotify_fd = -1;
        }
        wd_to_path.clear();
        path_to_wd.clear();
    }

    void stop_platform() {
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
        int wd = inotify_add_watch(inotify_fd, path.c_str(), inotify_mask);
        if(wd < 0) {
            return false;
        }
        wd_to_path[wd] = path;
        path_to_wd[path] = wd;
        return true;
    }

    constexpr static int max_scan_depth = 128;

    void scan_directory(const std::string& path, int depth = 0) {
        if(!add_watch(path)) {
            return;
        }
        if(!recursive || depth >= max_scan_depth) {
            return;
        }
        std::error_code ec;
        for(auto& entry: std::filesystem::directory_iterator(path, ec)) {
            if(ec)
                break;
            if(entry.is_directory(ec) && !ec) {
                scan_directory(entry.path().string(), depth + 1);
            }
        }
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
        constexpr size_t inotify_read_buf_size = 8192;
        alignas(struct inotify_event) char buf[inotify_read_buf_size];
        for(;;) {
            ssize_t n = ::read(inotify_fd, buf, sizeof(buf));
            if(n < 0) {
                if(errno == EINTR)
                    continue;
                break;
            }
            if(n == 0)
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

    error init_platform(std::shared_ptr<Self>& s) {
        inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if(inotify_fd < 0) {
            return error::unknown_error;
        }

        scan_directory(root_path);
        if(wd_to_path.empty()) {
            ::close(inotify_fd);
            inotify_fd = -1;
            return error::permission_denied;
        }

        uv_loop_t& uv = *loop;
        int rc = uv_poll_init(&uv, &poll_handle, inotify_fd);
        if(rc < 0) {
            ::close(inotify_fd);
            inotify_fd = -1;
            return error(rc);
        }

        poll_handle.data = s.get();
        poll_initialized = true;

        rc = uv_poll_start(&poll_handle, UV_READABLE, Self::on_poll);
        if(rc < 0) {
            stop_platform();
            return error(rc);
        }

        uv_unref(reinterpret_cast<uv_handle_t*>(&poll_handle));
        return error{};
    }
};

#elif defined(__APPLE__)

namespace {

constexpr FSEventStreamEventFlags IGNORED_FLAGS =
    kFSEventStreamEventFlagItemIsHardlink | kFSEventStreamEventFlagItemIsLastHardlink |
    kFSEventStreamEventFlagItemIsSymlink | kFSEventStreamEventFlagItemIsDir |
    kFSEventStreamEventFlagItemIsFile
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
    | kFSEventStreamEventFlagItemCloned
#endif
    ;

}  // namespace

struct fs_event::Self : fs_event_base, std::enable_shared_from_this<Self> {
    FSEventStreamRef stream = nullptr;
    dispatch_queue_t dispatch_queue = nullptr;

    ~Self() {
        stop_platform();
    }

    void stop_platform() {
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

        std::string pending_old_name;

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
            bool is_renamed = (flags[i] & kFSEventStreamEventFlagItemRenamed) != 0;
            bool is_modified = (flags[i] & (kFSEventStreamEventFlagItemModified |
                                            kFSEventStreamEventFlagItemInodeMetaMod |
                                            kFSEventStreamEventFlagItemFinderInfoMod |
                                            kFSEventStreamEventFlagItemChangeOwner |
                                            kFSEventStreamEventFlagItemXattrMod)) != 0;

            if(is_renamed) {
                struct stat st;
                if(stat(path.c_str(), &st) != 0) {
                    if(!pending_old_name.empty()) {
                        changes.push_back(change{std::move(pending_old_name), effect::destroy, {}});
                    }
                    pending_old_name = std::move(path);
                } else {
                    if(!pending_old_name.empty()) {
                        changes.push_back(
                            change{std::move(path), effect::rename, std::move(pending_old_name)});
                        pending_old_name.clear();
                    } else {
                        changes.push_back(change{std::move(path), effect::create, {}});
                    }
                }
                continue;
            }

            struct stat st;
            bool exists = (stat(path.c_str(), &st) == 0);

            if(!exists) {
                changes.push_back(change{std::move(path), effect::destroy, {}});
            } else if(is_created) {
                // FSEvents may set Created for both genuine creates and for
                // modifications of existing files. Use birthtime vs mtime to
                // disambiguate: a freshly created file has birthtime ≈ mtime.
                long long diff_ms =
                    (st.st_mtimespec.tv_sec - st.st_birthtimespec.tv_sec) * 1000LL +
                    (st.st_mtimespec.tv_nsec - st.st_birthtimespec.tv_nsec) / 1000000LL;
                constexpr long long create_detect_threshold_ms = 200;
                if(diff_ms < create_detect_threshold_ms) {
                    changes.push_back(change{std::move(path), effect::create, {}});
                } else {
                    changes.push_back(change{std::move(path), effect::modify, {}});
                }
            } else if(is_modified) {
                changes.push_back(change{std::move(path), effect::modify, {}});
            }
        }

        if(!pending_old_name.empty()) {
            changes.push_back(change{std::move(pending_old_name), effect::destroy, {}});
        }

        if(changes.empty())
            return;

        if(shared->closed.load(std::memory_order_acquire))
            return;

        shared->post_changes(shared, std::move(changes));
    }

    error init_platform(std::shared_ptr<Self>& s) {
        CFStringRef cf_path =
            CFStringCreateWithCString(nullptr, root_path.c_str(), kCFStringEncodingUTF8);
        if(!cf_path) {
            return error::unknown_error;
        }

        CFArrayRef paths_to_watch = CFArrayCreate(nullptr,
                                                  reinterpret_cast<const void**>(&cf_path),
                                                  1,
                                                  &kCFTypeArrayCallBacks);
        CFRelease(cf_path);

        if(!paths_to_watch) {
            return error::unknown_error;
        }

        FSEventStreamContext ctx{};
        ctx.info = s.get();

        constexpr double latency_sec = 0.001;
        FSEventStreamCreateFlags fs_flags = kFSEventStreamCreateFlagFileEvents;

        stream = FSEventStreamCreate(nullptr,
                                     &Self::fsevents_callback,
                                     &ctx,
                                     paths_to_watch,
                                     kFSEventStreamEventIdSinceNow,
                                     latency_sec,
                                     fs_flags);

        CFRelease(paths_to_watch);

        if(!stream) {
            return error::unknown_error;
        }

        dispatch_queue = dispatch_queue_create("kota.fs_event", DISPATCH_QUEUE_SERIAL);
        if(!dispatch_queue) {
            FSEventStreamRelease(stream);
            stream = nullptr;
            return error::unknown_error;
        }

        FSEventStreamSetDispatchQueue(stream, dispatch_queue);
        FSEventStreamStart(stream);
        return error{};
    }
};

#elif defined(_WIN32)

struct fs_event::Self : fs_event_base, std::enable_shared_from_this<Self> {
    std::wstring root_wpath;

    HANDLE dir_handle = INVALID_HANDLE_VALUE;
    std::thread worker_thread;
    std::atomic<bool> running{false};

    constexpr static DWORD DEFAULT_BUF_SIZE = 1024 * 1024;
    constexpr static DWORD NETWORK_BUF_SIZE = 64 * 1024;

    std::vector<BYTE> read_buffer;
    std::vector<BYTE> write_buffer;
    OVERLAPPED overlapped{};

    ~Self() {
        stop_platform();
    }

    void stop_platform() {
        if(!worker_thread.joinable())
            return;

        DWORD apc_ok = QueueUserAPC(
            [](ULONG_PTR param) {
                auto* s = reinterpret_cast<Self*>(param);
                s->running.store(false, std::memory_order_release);
                if(s->dir_handle != INVALID_HANDLE_VALUE) {
                    CancelIoEx(s->dir_handle, nullptr);
                }
            },
            worker_thread.native_handle(),
            reinterpret_cast<ULONG_PTR>(this));
        if(!apc_ok) {
            running.store(false, std::memory_order_release);
            if(dir_handle != INVALID_HANDLE_VALUE) {
                CancelIoEx(dir_handle, nullptr);
            }
        }
        worker_thread.join();
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
        int written = WideCharToMultiByte(CP_UTF8,
                                          0,
                                          str,
                                          static_cast<int>(len),
                                          result.data(),
                                          size,
                                          nullptr,
                                          nullptr);
        if(written <= 0)
            return {};
        return result;
    }

    void poll() {
        if(!running.load(std::memory_order_acquire))
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
            running.store(false, std::memory_order_release);
            post_change(shared_from_this(), change{root_path, effect::destroy, {}});
        }
    }

    void on_completion(DWORD error_code, DWORD num_bytes) {
        if(!running.load(std::memory_order_acquire))
            return;

        switch(error_code) {
            case ERROR_OPERATION_ABORTED: return;
            case ERROR_INVALID_PARAMETER:
                read_buffer.resize(NETWORK_BUF_SIZE);
                write_buffer.resize(NETWORK_BUF_SIZE);
                poll();
                return;
            case ERROR_NOTIFY_ENUM_DIR:
                post_change(shared_from_this(), change{{}, effect::overflow, {}});
                poll();
                return;
            case ERROR_ACCESS_DENIED: {
                DWORD attrs = GetFileAttributesW(root_wpath.c_str());
                bool is_dir =
                    attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
                if(!is_dir) {
                    post_change(shared_from_this(), change{root_path, effect::destroy, {}});
                    running.store(false, std::memory_order_release);
                    return;
                }
                poll();
                return;
            }
            default:
                if(error_code != ERROR_SUCCESS) {
                    running.store(false, std::memory_order_release);
                    post_change(shared_from_this(), change{root_path, effect::destroy, {}});
                    return;
                }
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
            post_changes(shared_from_this(), std::move(changes));
        }
    }

    void worker_entry() {
        poll();
        while(running.load(std::memory_order_acquire)) {
            SleepEx(INFINITE, TRUE);
        }
        while(SleepEx(0, TRUE) == WAIT_IO_COMPLETION) {}
        if(dir_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(dir_handle);
            dir_handle = INVALID_HANDLE_VALUE;
        }
    }

    error init_platform(std::shared_ptr<Self>& s) {
        std::wstring wpath;
        {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, root_path.c_str(), -1, nullptr, 0);
            if(wlen <= 0)
                return error::invalid_argument;
            wpath.resize(wlen - 1);
            MultiByteToWideChar(CP_UTF8, 0, root_path.c_str(), -1, wpath.data(), wlen);
        }

        dir_handle = CreateFileW(wpath.c_str(),
                                 FILE_LIST_DIRECTORY,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr,
                                 OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                 nullptr);

        if(dir_handle == INVALID_HANDLE_VALUE) {
            return error::no_such_file_or_directory;
        }

        BY_HANDLE_FILE_INFORMATION file_info;
        if(!GetFileInformationByHandle(dir_handle, &file_info) ||
           !(file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            CloseHandle(dir_handle);
            dir_handle = INVALID_HANDLE_VALUE;
            return error::invalid_argument;
        }

        root_wpath = std::move(wpath);
        read_buffer.resize(DEFAULT_BUF_SIZE);
        write_buffer.resize(DEFAULT_BUF_SIZE);
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        // APC-only: hEvent is unused by the kernel when a completion routine
        // is supplied, so we repurpose it to pass Self* to the callback.
        overlapped.hEvent = reinterpret_cast<HANDLE>(s.get());
        running.store(true, std::memory_order_release);

        auto shared_for_thread = s;
        worker_thread = std::thread([shared_for_thread]() { shared_for_thread->worker_entry(); });
        return error{};
    }
};

#endif

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

    auto init_err = s->init_platform(s);
    if(init_err != error{}) {
        return outcome_error(init_err);
    }

    return fs_event(std::move(s));
}

result<fs_event> fs_event::create(std::string_view path, event_loop& loop) {
    return create(path, options{}, loop);
}

// Flow: wait for events → debounce → drain buffer → apply file_filter.
// The outer loop retries when file_filter discards everything in a batch.
// stop() wakes both has_events.wait() and debounce_timer.wait() via the
// closed flag, guaranteeing this coroutine never deadlocks.
task<std::vector<fs_event::change>, error> fs_event::next() {
    if(!self || self->closed.load(std::memory_order_acquire)) {
        co_await fail(error::invalid_argument);
    }

    while(true) {
        while(self->buffer.empty()) {
            if(self->closed.load(std::memory_order_acquire)) {
                co_await fail(error::operation_aborted);
            }
            self->has_events.reset();
            co_await self->has_events.wait();

            if(self->closed.load(std::memory_order_acquire)) {
                co_await fail(error::operation_aborted);
            }
        }

        self->debounce_timer.start(self->debounce_ms);
        co_await self->debounce_timer.wait();

        if(self->closed.load(std::memory_order_acquire)) {
            co_await fail(error::operation_aborted);
        }

        auto batch = std::exchange(self->buffer, {});

        if(self->file_filter.empty()) {
            co_return batch;
        }

        std::vector<change> filtered;
        for(auto& c: batch) {
            auto slash = c.path.rfind('/');
            std::string_view name = (slash != std::string::npos)
                                        ? std::string_view(c.path).substr(slash + 1)
                                        : std::string_view(c.path);
            if(name != self->file_filter)
                continue;

            filtered.push_back(std::move(c));
        }

        if(filtered.empty())
            continue;

        co_return filtered;
    }
}

// Must be called from the event-loop thread. has_events and debounce_timer
// are not thread-safe; calling stop() from another thread races with next().
void fs_event::stop() {
    if(!self) {
        return;
    }

    self->closed.store(true, std::memory_order_release);
    self->has_events.set();
    // Fire immediately instead of stopping: stop() only cancels the timer
    // but does not wake a coroutine suspended on debounce_timer.wait().
    self->debounce_timer.start(std::chrono::milliseconds{0});

    self->stop_platform();
}

}  // namespace kota
