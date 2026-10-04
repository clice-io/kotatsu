#include "kota/async/io/process.h"

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "stream_self.h"
#include "kota/support/string_ref.h"
#include "kota/async/runtime/when.h"

namespace kota {

struct process::Self : uv::owned_handle<Self> {
    union {
        uv_handle_t handle;
        uv_process_t process;
    };

    /// The pending wait.
    uv::waiter_slot<exit_status> slot;

    /// How the child ended, once it has.
    std::optional<exit_status> exited;

    struct ExitWait;

    /// capture()'s wait for the exit, while it waits.
    ExitWait* exit_wait = nullptr;

    static void on_exit(uv_process_t* handle, std::int64_t status, int term_signal);
};

/// Waits for the child, started that same turn, to exit; a cancel kills it,
/// and the wait goes on until it has exited.
struct process::Self::ExitWait : uv::uv_op<ExitWait> {
    Self& self;

    explicit ExitWait(Self& self) noexcept : self(self) {}

    bool start() noexcept {
        self.exit_wait = this;
        return true;
    }

    void cancel() noexcept {
        // no_such_process only means a child reaped already, whose exit
        // callback completes this. A child the caller may not signal would
        // keep the cancel waiting for good.
        auto killed = error(::uv_process_kill(&self.process, SIGKILL));
        if(killed && killed != error::no_such_process) {
            std::abort();
        }
    }

    exit_status await_resume() const noexcept {
        return *self.exited;
    }
};

void process::Self::on_exit(uv_process_t* handle, std::int64_t status, int term_signal) {
    auto* self = static_cast<Self*>(handle->data);
    self->exited = exit_status{.status = status, .term_signal = term_signal};
    if(self->slot.waiting()) {
        self->slot.deliver(*self->exited);
    }
    if(auto* waiting = std::exchange(self->exit_wait, nullptr)) {
        waiting->complete();
    }
}

namespace {

/// The name of signal `signum`, among those that end a process; empty for
/// another.
std::string_view signal_name(int signum) {
    switch(signum) {
        case SIGINT: return "SIGINT";
        case SIGILL: return "SIGILL";
        case SIGABRT: return "SIGABRT";
        case SIGFPE: return "SIGFPE";
        case SIGSEGV: return "SIGSEGV";
        case SIGTERM: return "SIGTERM";
        case SIGKILL: return "SIGKILL";
#ifndef _WIN32
        case SIGHUP: return "SIGHUP";
        case SIGQUIT: return "SIGQUIT";
        case SIGTRAP: return "SIGTRAP";
        case SIGBUS: return "SIGBUS";
        case SIGPIPE: return "SIGPIPE";
        case SIGALRM: return "SIGALRM";
        case SIGUSR1: return "SIGUSR1";
        case SIGUSR2: return "SIGUSR2";
#endif
        default: return {};
    }
}

#ifdef _WIN32
/// What the NTSTATUS `status` of a crash says, for the common ones; empty for
/// another.
std::string_view exception_name(std::uint32_t status) {
    switch(status) {
        case 0x80000003: return "breakpoint";
        case 0xC0000005: return "access violation";
        case 0xC000001D: return "illegal instruction";
        case 0xC0000094: return "integer division by zero";
        case 0xC00000FD: return "stack overflow";
        case 0xC0000374: return "heap corruption";
        case 0xC0000409: return "fail fast";
        default: return {};
    }
}
#endif

/// The name in `entry`, a `KEY=VALUE` string. On Windows a name may start
/// with '=', as the per-drive directories do.
std::string_view env_name(std::string_view entry) {
    return entry.substr(0, entry.find('=', 1));
}

bool same_env_name(std::string_view left, std::string_view right) {
#ifdef _WIN32
    return string_ref(left).equals_insensitive(right);
#else
    return left == right;
#endif
}

/// The variables of this process, as `KEY=VALUE` strings.
result<std::vector<std::string>> inherited_environment() {
    uv_env_item_t* items = nullptr;
    int count = 0;
    // Only reads the environment: spawns on several threads may call it at
    // once, and only a write to the environment meanwhile would race it.
    if(auto err = error(::uv_os_environ(&items, &count))) {
        return outcome_error(err);
    }

    // Freed on every way out, a throwing copy below included.
    struct Freed {
        uv_env_item_t* items;
        int count;

        ~Freed() {
            ::uv_os_free_environ(items, count);
        }
    } freed{items, count};

    std::vector<std::string> entries;
    entries.reserve(static_cast<std::size_t>(count));
    for(const auto& item: std::span(items, static_cast<std::size_t>(count))) {
        entries.push_back(std::format("{}={}", item.name, item.value));
    }
    return entries;
}

/// The child's environment as `opts` asks for it: `env`, or the inherited
/// one, with `env_changes` made in order.
result<std::vector<std::string>> child_environment(const process::options& opts) {
    std::vector<std::string> entries;
    if(opts.env.empty()) {
        auto inherited = inherited_environment();
        if(!inherited) {
            return outcome_error(inherited.error());
        }
        entries = std::move(*inherited);
    } else {
        entries = opts.env;
    }
    for(const auto& change: opts.env_changes) {
        std::erase_if(entries, [&](const std::string& entry) {
            return same_env_name(env_name(entry), change.name);
        });
        if(change.value) {
            entries.push_back(std::format("{}={}", change.name, *change.value));
        }
    }
    return entries;
}

/// A NULL-terminated array of the strings in `from`, which libuv takes as
/// char* but only reads.
std::vector<char*> c_strings(const std::vector<std::string>& from) {
    std::vector<char*> out;
    out.reserve(from.size() + 1);
    for(const auto& text: from) {
        out.push_back(const_cast<char*>(text.c_str()));
    }
    out.push_back(nullptr);
    return out;
}

}  // namespace

std::string process::exit_status::to_string() const {
    if(term_signal != 0) {
        auto name = signal_name(term_signal);
        return name.empty() ? std::format("signal {}", term_signal)
                            : std::format("signal {} ({})", term_signal, name);
    }
#ifdef _WIN32
    // A crash exits with its exception's NTSTATUS, an error or a warning.
    const auto code = static_cast<std::uint32_t>(status);
    if(code >= 0x8000'0000) {
        auto name = exception_name(code);
        return name.empty() ? std::format("exception 0x{:08X}", code)
                            : std::format("exception 0x{:08X} ({})", code, name);
    }
#endif
    return std::format("exit code {}", status);
}

process::process() noexcept = default;

process::process(detail::unique_handle<Self> self) noexcept : self(std::move(self)) {}

process::~process() = default;

process::process(process&& other) noexcept = default;

process& process::operator=(process&& other) noexcept = default;

process::stdio process::stdio::inherit() {
    return stdio{};
}

process::stdio process::stdio::ignore() {
    return stdio{.type = kind::ignore};
}

process::stdio process::stdio::from_fd(int fd) {
    return stdio{.type = kind::fd, .descriptor = fd};
}

process::stdio process::stdio::pipe(bool readable, bool writable) {
    return stdio{.type = kind::pipe, .readable = readable, .writable = writable};
}

result<process::spawn_result> process::spawn(const options& opts, event_loop& loop) {
    auto argv = c_strings(opts.args);
    if(opts.args.empty()) {
        argv.insert(argv.begin(), const_cast<char*>(opts.file.c_str()));
    }
    std::vector<std::string> changed_env;
    if(!opts.env_changes.empty()) {
        auto built = child_environment(opts);
        if(!built) {
            return outcome_error(built.error());
        }
        changed_env = std::move(*built);
    }
    const bool inherit_env = opts.env.empty() && opts.env_changes.empty();
    auto envp = c_strings(opts.env_changes.empty() ? opts.env : changed_env);

    std::array<pipe, 3> pipes;
    std::array<uv_stdio_container_t, 3> stdio_containers{};
    for(std::size_t i = 0; i < opts.streams.size(); ++i) {
        const auto& config = opts.streams[i];
        auto& container = stdio_containers[i];
        switch(config.type) {
            case stdio::kind::inherit:
                container.flags = UV_INHERIT_FD;
                container.data.fd = static_cast<int>(i);
                break;
            case stdio::kind::ignore: container.flags = UV_IGNORE; break;
            case stdio::kind::fd:
                container.flags = UV_INHERIT_FD;
                container.data.fd = config.descriptor;
                break;
            case stdio::kind::pipe: {
                int flags = UV_CREATE_PIPE;
                if(config.readable) {
                    flags |= UV_READABLE_PIPE;
                }
                if(config.writable) {
                    flags |= UV_WRITABLE_PIPE;
                }
                container.flags = static_cast<uv_stdio_flags>(flags);
                pipes[i] = pipe::create({}, loop);
                container.data.stream = &pipes[i].self->stream;
                break;
            }
        }
    }

    uv_process_options_t uv_opts{};
    uv_opts.exit_cb = Self::on_exit;
    uv_opts.file = opts.file.c_str();
    uv_opts.args = argv.data();
    uv_opts.env = inherit_env ? nullptr : envp.data();
    uv_opts.cwd = opts.cwd.empty() ? nullptr : opts.cwd.c_str();
    const auto& creation = opts.creation;
    if(creation.detached) {
        uv_opts.flags |= UV_PROCESS_DETACHED;
    }
    if(creation.windows_hide) {
        uv_opts.flags |= UV_PROCESS_WINDOWS_HIDE;
    }
    if(creation.windows_hide_console) {
        uv_opts.flags |= UV_PROCESS_WINDOWS_HIDE_CONSOLE;
    }
    if(creation.windows_hide_gui) {
        uv_opts.flags |= UV_PROCESS_WINDOWS_HIDE_GUI;
    }
    if(creation.windows_verbatim_arguments) {
        uv_opts.flags |= UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
    }
    if(creation.windows_file_path_exact_name) {
        uv_opts.flags |= UV_PROCESS_WINDOWS_FILE_PATH_EXACT_NAME;
    }
    uv_opts.stdio_count = static_cast<int>(stdio_containers.size());
    uv_opts.stdio = stdio_containers.data();

    // uv_spawn registers its SIGCHLD handler in a process-wide tree that is
    // not thread-safe: spawns from loops on different threads take turns.
    static std::mutex spawn_mutex;
    process proc(Self::make());
    {
        std::lock_guard lock(spawn_mutex);
        if(auto err = error(::uv_spawn(loop.native_handle(), &proc.self->process, &uv_opts))) {
            Self::forget_if_unlisted(*proc.self);
            return outcome_error(err);
        }
    }

    return spawn_result{
        .proc = std::move(proc),
        .stdin_pipe = std::move(pipes[0]),
        .stdout_pipe = std::move(pipes[1]),
        .stderr_pipe = std::move(pipes[2]),
    };
}

task<process::capture_result, error> process::capture(options opts, event_loop& loop) {
    opts.streams = {stdio::ignore(), stdio::pipe(false, true), stdio::pipe(false, true)};
    auto spawned = spawn(opts, loop);
    if(!spawned) {
        co_await fail(spawned.error());
    }
    // The exit wait starts first, so that a read failing at once still kills
    // the child and waits for it. Both pipes are read while the child runs:
    // one that fills a pipe waits for it to be read before it can exit.
    auto [status, stdout_data, stderr_data] =
        co_await or_fail(co_await when_all(Self::ExitWait(*spawned->proc.self),
                                           spawned->stdout_pipe.read_to_end(),
                                           spawned->stderr_pipe.read_to_end()));
    co_return capture_result{
        .status = status,
        .stdout_data = std::move(stdout_data),
        .stderr_data = std::move(stderr_data),
    };
}

task<process::exit_status, error> process::wait() {
    if(!self) {
        co_await fail(error::invalid_argument);
    }

    if(self->exited) {
        co_return *self->exited;
    }

    co_return co_await self->slot.wait();
}

int process::pid() const noexcept {
    return self ? self->process.pid : -1;
}

error process::kill(int signum) {
    if(!self) {
        return error::invalid_argument;
    }

    // Once the exit is observed libuv has reaped the child, and its pid may
    // already belong to another process.
    if(self->exited) {
        return error::no_such_process;
    }

    return error(::uv_process_kill(&self->process, signum));
}

error process::kill() {
    // libuv defines SIGKILL on Windows, and terminates the process for it.
    return kill(SIGKILL);
}

}  // namespace kota
