#include <algorithm>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "execution.h"
#include "kota/async/io/fs.h"
#include "kota/async/io/loop.h"
#include "kota/async/io/process.h"
#include "kota/async/io/stream.h"
#include "kota/async/io/system.h"
#include "kota/async/io/watcher.h"
#include "kota/async/runtime/task.h"
#include "kota/async/runtime/when.h"

namespace kota::zest {

namespace {

namespace stdfs = std::filesystem;

using std::chrono::milliseconds;
using std::chrono::steady_clock;

#ifdef SIGKILL
constexpr int kill_signal = SIGKILL;
#else
// libuv reads 9 as SIGKILL on Windows and terminates the process.
constexpr int kill_signal = 9;
#endif

std::string describe(const process::exit_status& status) {
#ifndef _WIN32
    if(status.term_signal != 0) {
        return std::format("signal {} ({})", status.term_signal, ::strsignal(status.term_signal));
    }
#endif
    // Windows reports a crash as an NTSTATUS exit code, which reads best in hex.
    if(status.status >= 0xC000'0000) {
        return std::format("exit code 0x{:08X}", status.status);
    }
    return std::format("exit code {}", status.status);
}

std::string utf8(const stdfs::path& path) {
    auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// Awaits `work` for at most `timeout`, zero meaning no limit; nothing if it
/// ran out of time.
template <typename T>
task<std::optional<T>> within(task<T> work, milliseconds timeout) {
    if(timeout.count() == 0) {
        co_return co_await std::move(work);
    }
    auto first = co_await when_any(std::move(work), sleep(timeout));
    if(first.index() != 0) {
        co_return std::nullopt;
    }
    co_return std::get<0>(std::move(first));
}

/// A running worker process.
struct Worker {
    process proc;
    /// The worker's stdin, which carries the protocol both ways.
    pipe channel;
    /// The worker's stdout and stderr, its own file for its whole life.
    stdfs::path log;
    /// Bytes read from the channel past the last complete line.
    std::string pending = {};
    /// Log bytes already handed out.
    std::uintmax_t offset = 0;

    /// The next line from the worker, or nothing once its end closes.
    task<std::optional<std::string>> read_line() {
        while(true) {
            if(auto line = protocol::take_line(pending)) {
                co_return line;
            }
            auto data = co_await channel.read();
            if(!data.has_value()) {
                co_return std::nullopt;
            }
            pending += *data;
        }
    }

    /// The state reported for the running test, counting the snapshots it
    /// checked as checked here; nothing if the worker goes before reporting.
    task<std::optional<TestState>> read_reply() {
        while(auto line = co_await read_line()) {
            std::string_view text = *line;
            if(text.starts_with(protocol::snapshot)) {
                record_snapshot_access(text.substr(protocol::snapshot.size()));
                continue;
            }
            // Test code runs in the worker and can garble the channel; any
            // other line counts as the worker failing.
            if(text.starts_with(protocol::done)) {
                co_return protocol::parse_state(text.substr(protocol::done.size()));
            }
            break;
        }
        co_return std::nullopt;
    }

    /// Log bytes written since the last call.
    std::string take_output() {
        std::ifstream file(log, std::ios::binary);
        file.seekg(static_cast<std::streamoff>(offset));
        std::string output{std::istreambuf_iterator<char>(file), {}};
        offset += output.size();
        return output;
    }

    /// Everything the worker has printed, handed out or not.
    std::string whole_output() const {
        std::ifstream file(log, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), {}};
    }

    task<process::exit_status> wait() {
        // wait() fails only without a process or with a second waiter.
        auto status = co_await proc.wait();
        assert(status.has_value());
        co_return *status;
    }

    /// Ends the worker whatever state it is in.
    task<process::exit_status> kill() {
        [[maybe_unused]] auto error = proc.kill(kill_signal);
        co_return co_await wait();
    }
};

struct Pool {
    const PoolOptions& options;
    function_ref<void(const Entry&, const Outcome&)> report;
    std::string executable;
    stdfs::path directory;
    /// Workers started so far, which names each one's log.
    unsigned started = 0;
    /// The worker that could not start, which ends the run.
    std::optional<WorkerFailure> broken = {};
    std::vector<WorkerFailure> failures = {};

    task<std::expected<Worker, WorkerFailure>> start() {
        // Each worker gets a fresh log: a killed worker's leftover children may
        // still be writing to the old one.
        auto log = directory / std::format("{}.log", started++);
        // O_EXCL: names never repeat within a run, so a file already there
        // was planted by someone else.
        auto fd = fs::sync::open(utf8(log), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if(!fd) {
            co_return std::unexpected(WorkerFailure{
                .detail = std::format("cannot create {}: {}", utf8(log), fd.error().message()),
            });
        }

        process::options spawn;
        spawn.file = executable;
        // The flag goes right after argv[0], ahead of anything the program
        // passes through untouched after `--`.
        spawn.args = options.args;
        spawn.args.emplace(spawn.args.begin() + std::min<std::size_t>(1, spawn.args.size()),
                           protocol::worker_flag);
        spawn.streams = {
            process::stdio::pipe(true, true),
            process::stdio::from_fd(*fd),
            process::stdio::from_fd(*fd),
        };
        auto spawned = process::spawn(spawn);
        [[maybe_unused]] auto closed = fs::sync::close(*fd);
        if(!spawned) {
            co_return std::unexpected(WorkerFailure{
                .detail = std::format("cannot start a worker: {}", spawned.error().message()),
            });
        }

        Worker worker{
            .proc = std::move(spawned->proc),
            .channel = std::move(spawned->stdin_pipe),
            .log = std::move(log),
        };
        // Starting up is no test's time, and what it prints no test's output.
        auto ready = co_await within(worker.read_line(), options.timeout);
        if(!ready || *ready != protocol::ready) {
            auto status = co_await worker.kill();
            co_return std::unexpected(WorkerFailure{
                .detail =
                    ready ? std::format("a worker ended while starting with {}", describe(status))
                          : std::string("a worker did not start within --timeout"),
                .output = worker.take_output(),
            });
        }
        worker.take_output();
        co_return worker;
    }

    task<Outcome> run(Worker& worker, const Entry& entry) {
        auto begin = steady_clock::now();
        // A worker gone before the command arrives fails the write, and the
        // read after it reports the crash.
        [[maybe_unused]] auto written =
            co_await worker.channel.write(std::format("{}{}\n", protocol::run, entry.name));
        // Outer: whether the worker answered in time. Inner: whether it
        // reported a state before going away.
        auto reply = co_await within(worker.read_reply(), options.timeout);
        auto duration = elapsed_since(begin);

        if(!reply) {
            co_await worker.kill();
            co_return Outcome{
                .verdict = Verdict::TimedOut,
                .duration = duration,
                .output = worker.take_output(),
            };
        }
        if(*reply) {
            co_return Outcome{
                .verdict = verdict_of(**reply),
                .duration = duration,
                .output = worker.take_output(),
            };
        }
        // Whatever broke the channel, the worker must be gone before its
        // status says how it ended.
        auto status = co_await worker.kill();
        co_return Outcome{
            .verdict = Verdict::Crashed,
            .duration = duration,
            .output = worker.take_output(),
            .detail = std::format("{} before the test finished", describe(status)),
        };
    }

    /// Runs `tests` one after another on one worker at a time, starting a new
    /// one whenever a test takes the current one down.
    task<> drive(std::span<const Entry* const> tests, std::size_t& next) {
        std::optional<Worker> worker;
        while(!broken && next < tests.size()) {
            const auto& entry = *tests[next++];
            if(!worker) {
                auto started = co_await start();
                if(!started) {
                    broken = std::move(started.error());
                    co_return;
                }
                worker.emplace(std::move(*started));
            }

            auto outcome = co_await run(*worker, entry);
            if(outcome.verdict == Verdict::Crashed || outcome.verdict == Verdict::TimedOut) {
                worker.reset();
            }
            report(entry, outcome);
        }

        if(!worker) {
            co_return;
        }
        // Hanging up tells the worker to exit. If it ends badly, all it printed
        // is shown: a sanitizer reports during the test it catches, which may
        // well have passed.
        worker->channel = pipe{};
        // Giving up on the wait kills the worker.
        auto status = co_await within(worker->wait(), options.timeout);
        if(!status) {
            failures.push_back(WorkerFailure{
                .detail = "a worker did not exit within --timeout after its last test",
                .output = worker->whole_output(),
            });
        } else if(status->status != 0 || status->term_signal != 0) {
            failures.push_back(WorkerFailure{
                .detail =
                    std::format("a worker ended with {} after its last test", describe(*status)),
                .output = worker->whole_output(),
            });
        }
    }
};

}  // namespace

std::expected<std::vector<WorkerFailure>, WorkerFailure>
    run_pool(std::span<const Entry* const> tests,
             const PoolOptions& options,
             function_ref<void(const Entry&, const Outcome&)> report) {
    auto executable = sys::executable_path();
    if(!executable) {
        return std::unexpected(WorkerFailure{
            .detail = std::format("cannot find this program to start workers: {}",
                                  executable.error().message()),
        });
    }

    // A directory of this run's own, which nobody can have planted files in
    // or can read test output from.
    std::error_code error;
    auto base = stdfs::temp_directory_path(error);
    stdfs::path directory;
    std::random_device random;
    while(!error) {
        directory = base / std::format("zest-{}-{:08x}", sys::pid(), random());
        if(stdfs::create_directory(directory, error)) {
            stdfs::permissions(directory,
                               stdfs::perms::owner_all,
                               stdfs::perm_options::replace,
                               error);
            break;
        }
    }
    if(error) {
        return std::unexpected(WorkerFailure{
            .detail =
                std::format("cannot create a directory for worker output: {}", error.message()),
        });
    }

#ifdef SIGPIPE
    // A worker that dies between tests closes the channel under the next
    // command; the write should fail and report the crash, not kill the runner.
    // libuv starts every child, workers included, with default dispositions.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    std::vector<const Entry*> concurrent;
    std::vector<const Entry*> serial;
    for(const auto* entry: tests) {
        (entry->test_case.attrs.serial ? serial : concurrent).push_back(entry);
    }

    Pool pool{
        .options = options,
        .report = report,
        .executable = std::move(*executable),
        .directory = directory,
    };

    auto run_all = [&]() -> task<> {
        std::size_t next = 0;
        std::vector<task<>> workers;
        auto jobs = options.jobs != 0 ? options.jobs : std::max(1u, sys::parallelism());
        auto count = std::min<std::size_t>(jobs, concurrent.size());
        for(std::size_t slot = 0; slot < count; ++slot) {
            workers.push_back(pool.drive(concurrent, next));
        }
        co_await when_all(std::move(workers));

        // Serial tests run on one worker once nothing else is running.
        std::size_t next_serial = 0;
        co_await pool.drive(serial, next_serial);
    };
    event_loop loop;
    auto all = run_all();
    loop.schedule(all);
    loop.run();

    stdfs::remove_all(directory, error);
    if(pool.broken) {
        return std::unexpected(std::move(*pool.broken));
    }
    return std::move(pool.failures);
}

}  // namespace kota::zest
