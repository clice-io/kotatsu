#include "kota/async/io/process.h"

#include <csignal>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>

#include "stream_self.h"

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

    static void on_exit(uv_process_t* handle, std::int64_t status, int term_signal) {
        auto* self = static_cast<Self*>(handle->data);
        self->exited = exit_status{.status = status, .term_signal = term_signal};
        if(self->slot.waiting()) {
            self->slot.deliver(*self->exited);
        }
    }
};

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
    auto envp = c_strings(opts.env);

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
    uv_opts.env = opts.env.empty() ? nullptr : envp.data();
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
