#include "eventide/async/process.h"

#include <cassert>

#include "awaiter.h"
#include "eventide/async/error.h"
#include "eventide/async/loop.h"

namespace eventide {

static unsigned int to_uv_process_flags(const process::creation_options& options) {
    unsigned int out = 0;
    if(options.detached) {
        out |= UV_PROCESS_DETACHED;
    }
    if(options.windows_hide) {
        out |= UV_PROCESS_WINDOWS_HIDE;
    }
    if(options.windows_hide_console) {
        out |= UV_PROCESS_WINDOWS_HIDE_CONSOLE;
    }
    if(options.windows_hide_gui) {
        out |= UV_PROCESS_WINDOWS_HIDE_GUI;
    }
    if(options.windows_verbatim_arguments) {
        out |= UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
    }
    if(options.windows_file_path_exact_name) {
        out |= UV_PROCESS_WINDOWS_FILE_PATH_EXACT_NAME;
    }
    return out;
}

struct process::Self :
    uv_handle<process::Self, uv_process_t>,
    uv::latched_delivery<process::exit_status> {
    uv_process_t handle{};
};

namespace {

struct process_await : uv::await_op<process_await> {
    using await_base = uv::await_op<process_await>;
    using promise_t = task<process::wait_result>::promise_type;

    // Process self used to install/remove waiter and active result pointers.
    process::Self* self;
    // Exit status slot filled by process exit callback.
    process::exit_status result{};

    explicit process_await(process::Self* self) : self(self) {}

    static void on_cancel(system_op* op) {
        await_base::complete_cancel(op, [](auto& aw) {
            if(aw.self) {
                aw.self->disarm();
            }
        });
    }

    static void notify(process::Self& self, process::exit_status status) {
        self.deliver(status);
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
        self->arm(*this, result);
        return this->link_continuation(&waiting.promise(), location);
    }

    process::wait_result await_resume() noexcept {
        if(self) {
            self->disarm();
        }
        return result;
    }
};

}  // namespace

process::process() noexcept = default;

process::process(unique_handle<Self> self) noexcept : self(std::move(self)) {}

process::~process() = default;

process::process(process&& other) noexcept = default;

process& process::operator=(process&& other) noexcept = default;

process::Self* process::operator->() noexcept {
    return self.get();
}

process::stdio process::stdio::inherit() {
    return stdio{};
}

process::stdio process::stdio::ignore() {
    stdio io{};
    io.type = kind::ignore;
    return io;
}

process::stdio process::stdio::from_fd(int fd) {
    stdio io{};
    io.type = kind::fd;
    io.descriptor = fd;
    return io;
}

process::stdio process::stdio::pipe(bool readable, bool writable) {
    stdio io{};
    io.type = kind::pipe;
    io.readable = readable;
    io.writable = writable;
    return io;
}

result<process::spawn_result> process::spawn(const options& opts, event_loop& loop) {
    spawn_result out{process(Self::make())};

    std::vector<std::string> argv_storage;
    if(opts.args.empty()) {
        argv_storage.push_back(opts.file);
    } else {
        argv_storage = opts.args;
    }

    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for(auto& arg: argv_storage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    std::vector<std::string> env_storage = opts.env;
    std::vector<char*> envp;
    if(!env_storage.empty()) {
        envp.reserve(env_storage.size() + 1);
        for(auto& e: env_storage) {
            envp.push_back(e.data());
        }
        envp.push_back(nullptr);
    }

    std::array<pipe, 3> created_pipes{};
    std::array<uv_stdio_container_t, 3> stdio{};

    for(std::size_t i = 0; i < opts.streams.size(); ++i) {
        auto& cfg = opts.streams[i];
        auto& dst = stdio[i];

        switch(cfg.type) {
            case stdio::kind::inherit:
                dst.flags = UV_INHERIT_FD;
                dst.data.fd = static_cast<int>(i);
                break;
            case stdio::kind::ignore: dst.flags = UV_IGNORE; break;
            case stdio::kind::fd:
                dst.flags = UV_INHERIT_FD;
                dst.data.fd = cfg.descriptor;
                break;
            case stdio::kind::pipe: {
                auto pipe = pipe::create(pipe::options{}, loop);
                if(!pipe) {
                    return std::unexpected(pipe.error());
                }

                dst.flags = UV_CREATE_PIPE;
                if(cfg.readable) {
                    dst.flags =
                        static_cast<uv_stdio_flags>(dst.flags | static_cast<int>(UV_READABLE_PIPE));
                }
                if(cfg.writable) {
                    dst.flags =
                        static_cast<uv_stdio_flags>(dst.flags | static_cast<int>(UV_WRITABLE_PIPE));
                }

                dst.data.stream = static_cast<uv_stream_t*>(pipe->handle());

                created_pipes[i] = std::move(*pipe);
                break;
            }
        }
    }

    uv_process_options_t uv_opts{};
    uv_opts.exit_cb = +[](uv_process_t* handle, int64_t exit_status, int term_signal) {
        auto* self = static_cast<process::Self*>(handle->data);
        assert(self != nullptr && "process exit callback requires process state in handle->data");

        process_await::notify(*self, process::exit_status{exit_status, term_signal});
    };
    uv_opts.file = opts.file.c_str();
    uv_opts.args = argv.data();
    uv_opts.stdio_count = static_cast<int>(stdio.size());
    uv_opts.stdio = stdio.data();

    if(!envp.empty()) {
        uv_opts.env = envp.data();
    }

    if(!opts.cwd.empty()) {
        uv_opts.cwd = opts.cwd.c_str();
    }

    uv_opts.flags = to_uv_process_flags(opts.creation);

    auto* self = out.proc.self.get();
    if(self == nullptr) {
        return std::unexpected(error::invalid_argument);
    }

    auto& proc_handle = self->handle;
    if(auto err = uv::spawn(loop, proc_handle, uv_opts)) {
        return std::unexpected(err);
    }

    out.stdin_pipe = std::move(created_pipes[0]);
    out.stdout_pipe = std::move(created_pipes[1]);
    out.stderr_pipe = std::move(created_pipes[2]);

    return out;
}

task<process::wait_result> process::wait() {
    if(!self) {
        co_return std::unexpected(error::invalid_argument);
    }

    if(self->has_pending()) {
        co_return self->peek_pending();
    }

    if(self->has_waiter()) {
        co_return std::unexpected(error::connection_already_in_progress);
    }

    co_return co_await process_await{self.get()};
}

int process::pid() const noexcept {
    if(!self || !self->initialized()) {
        return -1;
    }

    return self->handle.pid;
}

error process::kill(int signum) {
    if(!self || !self->initialized()) {
        return error::invalid_argument;
    }

    if(auto err = uv::process_kill(self->handle, signum)) {
        return err;
    }

    return {};
}

}  // namespace eventide
