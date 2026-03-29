#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "loop_fixture.h"
#include "eventide/zest/zest.h"

namespace eventide {

namespace {

task<process::wait_result> wait_for_exit(process& proc) {
    auto status = co_await proc.wait();
    event_loop::current().stop();
    co_return status;
}

task<process::wait_result> wait_for_exit(process& proc, int& done, int target) {
    auto status = co_await proc.wait();
    done += 1;
    if(done == target) {
        event_loop::current().stop();
    }
    co_return status;
}

task<std::pair<result<std::string>, result<std::string>>> read_two_chunks(pipe p) {
    auto first = co_await p.read_chunk();
    result<std::string> first_out = outcome_error(error::invalid_argument);
    if(first) {
        first_out = std::string(first->data(), first->size());
        p.consume(first->size());
    } else {
        first_out = outcome_error(first.error());
    }

    auto second = co_await p.read_chunk();
    result<std::string> second_out = outcome_error(error::invalid_argument);
    if(second) {
        second_out = std::string(second->data(), second->size());
        p.consume(second->size());
    } else {
        second_out = outcome_error(second.error());
    }

    event_loop::current().stop();
    co_return std::pair{std::move(first_out), std::move(second_out)};
}

}  // namespace

TEST_SUITE(process_io, loop_fixture) {

TEST_CASE(spawn_wait_simple) {
    process::options opts;
#ifdef _WIN32
    opts.file = "cmd.exe";
    opts.args = {opts.file, "/c", "exit 0"};
#else
    opts.file = "/bin/sh";
    opts.args = {opts.file, "-c", "true"};
#endif
    opts.streams = {process::stdio::ignore(), process::stdio::ignore(), process::stdio::ignore()};

    auto spawn_res = process::spawn(opts, loop);
    ASSERT_TRUE(spawn_res.has_value());

    EXPECT_TRUE(spawn_res->proc.pid() > 0);

    auto worker = wait_for_exit(spawn_res->proc);
    schedule_all(worker);

    auto status = worker.result();
    EXPECT_TRUE(status.has_value());
    EXPECT_EQ(status->status, 0);
    EXPECT_EQ(status->term_signal, 0);
}

TEST_CASE(spawn_pipe_stdout) {
    process::options opts;
#ifdef _WIN32
    opts.file = "cmd.exe";
    opts.args = {opts.file, "/c", "echo eventide-stdout"};
    const std::string expected = "eventide-stdout\r\n";
#else
    opts.file = "/bin/sh";
    opts.args = {opts.file, "-c", "printf 'eventide-stdout'"};
    const std::string expected = "eventide-stdout";
#endif
    opts.streams = {process::stdio::ignore(),
                    process::stdio::pipe(false, true),
                    process::stdio::ignore()};

    auto spawn_res = process::spawn(opts, loop);
    ASSERT_TRUE(spawn_res.has_value());

    auto capture_stdout = [&]() -> task<void> {
        auto stdout_out = co_await spawn_res->stdout_pipe.read();
        auto status = co_await spawn_res->proc.wait();

        EXPECT_TRUE(status.has_value());
        if(status.has_value()) {
            EXPECT_EQ(status->status, 0);
        }

        EXPECT_TRUE(stdout_out.has_value());
        if(stdout_out.has_value()) {
            EXPECT_EQ(*stdout_out, expected);
        }

        event_loop::current().stop();
    };

    auto t = capture_stdout();
    schedule_all(t);
}

TEST_CASE(spawn_pipe_stdio) {
    process::options opts;
#ifdef _WIN32
    opts.file = "cmd.exe";
    opts.args = {opts.file, "/c", "more"};
#else
    opts.file = "/bin/cat";
    opts.args = {opts.file};
#endif
    opts.streams = {process::stdio::pipe(true, false),
                    process::stdio::pipe(false, true),
                    process::stdio::ignore()};

    const std::string payload = "eventide-stdin-payload\n";

    auto spawn_res = process::spawn(opts, loop);
    ASSERT_TRUE(spawn_res.has_value());

    auto write_stdin_capture_stdout = [&]() -> task<void> {
        std::span<const char> data(payload.data(), payload.size());
        auto write_err = co_await spawn_res->stdin_pipe.write(data);
        EXPECT_FALSE(write_err.has_error());

        spawn_res->stdin_pipe = pipe{};

        auto stdout_out = co_await spawn_res->stdout_pipe.read();
        auto status = co_await spawn_res->proc.wait();

        EXPECT_TRUE(stdout_out.has_value());
        EXPECT_TRUE(status.has_value());
        if(status.has_value()) {
            EXPECT_EQ(status->status, 0);
        }

        auto trim_newlines = [](std::string value) {
            while(!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
                value.pop_back();
            }
            return value;
        };

        if(stdout_out.has_value()) {
            EXPECT_EQ(trim_newlines(*stdout_out), trim_newlines(payload));
        }

        event_loop::current().stop();
    };

    auto t = write_stdin_capture_stdout();
    schedule_all(t);
}

TEST_CASE(spawn_pipe_stderr) {
    process::options opts;
#ifdef _WIN32
    opts.file = "cmd.exe";
    opts.args = {opts.file, "/c", "echo eventide-stderr 1>&2"};
#else
    opts.file = "/bin/sh";
    opts.args = {opts.file, "-c", "printf 'eventide-stderr' 1>&2"};
#endif
    opts.streams = {process::stdio::ignore(),
                    process::stdio::pipe(false, true),
                    process::stdio::pipe(false, true)};

    auto spawn_res = process::spawn(opts, loop);
    ASSERT_TRUE(spawn_res.has_value());

    auto capture_stdout_stderr = [&]() -> task<void> {
        auto stdout_out = co_await spawn_res->stdout_pipe.read();
        auto stderr_out = co_await spawn_res->stderr_pipe.read();
        auto status = co_await spawn_res->proc.wait();

        EXPECT_TRUE(status.has_value());
        if(status.has_value()) {
            EXPECT_EQ(status->status, 0);
        }

        EXPECT_TRUE(!stdout_out.has_value());
        EXPECT_TRUE(stderr_out.has_value());

        if(stderr_out.has_value()) {
            EXPECT_TRUE(stderr_out->find("eventide-stderr") != std::string::npos);
        }

        event_loop::current().stop();
    };

    auto t = capture_stdout_stderr();
    schedule_all(t);
}

TEST_CASE(spawn_pipe_stdout_read_chunk_twice) {
#ifdef _WIN32
    ::eventide::zest::skip();
    return;
#else
    process::options opts;
    opts.file = "/bin/sh";
    opts.args = {opts.file, "-c", "printf 'chunk-one'; sleep 0.05; printf 'chunk-two'"};
    opts.streams = {process::stdio::ignore(),
                    process::stdio::pipe(false, true),
                    process::stdio::ignore()};

    auto spawn_res = process::spawn(opts, loop);
    ASSERT_TRUE(spawn_res.has_value());

    auto reader = read_two_chunks(std::move(spawn_res->stdout_pipe));
    schedule_all(reader);

    auto [first, second] = reader.result();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, "chunk-one");
    EXPECT_EQ(*second, "chunk-two");
#endif
}

TEST_CASE(spawn_invalid_file) {
    process::options opts;
#ifdef _WIN32
    opts.file = "Z:\\nonexistent\\eventide-nope.exe";
#else
    opts.file = "/nonexistent/eventide-nope";
#endif

    auto spawn_res = process::spawn(opts, loop);
    EXPECT_FALSE(spawn_res.has_value());
}

TEST_CASE(wait_twice) {
    process::options opts;
#ifdef _WIN32
    opts.file = "cmd.exe";
    opts.args = {opts.file, "/c", "exit 0"};
#else
    opts.file = "/bin/sh";
    opts.args = {opts.file, "-c", "true"};
#endif
    opts.streams = {process::stdio::ignore(), process::stdio::ignore(), process::stdio::ignore()};

    auto spawn_res = process::spawn(opts, loop);
    ASSERT_TRUE(spawn_res.has_value());

    int done = 0;
    auto first = wait_for_exit(spawn_res->proc, done, 2);
    auto second = wait_for_exit(spawn_res->proc, done, 2);
    schedule_all(first, second);

    auto first_result = first.result();
    auto second_result = second.result();

    EXPECT_TRUE(first_result.has_value());
    EXPECT_FALSE(second_result.has_value());
    if(!second_result.has_value()) {
        EXPECT_EQ(second_result.error().value(), error::connection_already_in_progress.value());
    }
}

TEST_CASE(query_info_self) {
    // Query info about our own process (always valid).
    int self_pid = process::current_pid();

    auto info = process::query_info(self_pid);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->pid, self_pid);
    EXPECT_GT(info->rss, std::size_t{0});
    EXPECT_GT(info->vsize, std::size_t{0});
}

TEST_CASE(query_info_child) {
    process::options opts;
#ifdef _WIN32
    opts.file = "cmd.exe";
    // Run a command that takes a moment so we can query it while alive.
    opts.args = {opts.file, "/c", "ping -n 2 127.0.0.1 >nul"};
#else
    opts.file = "/bin/sh";
    opts.args = {opts.file, "-c", "sleep 0.2"};
#endif
    opts.streams = {process::stdio::ignore(), process::stdio::ignore(), process::stdio::ignore()};

    auto spawn_res = process::spawn(opts, loop);
    ASSERT_TRUE(spawn_res.has_value());

    auto pid = spawn_res->proc.pid();
    EXPECT_GT(pid, 0);

    // Query via the instance method.
    auto info = spawn_res->proc.query_info();
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->pid, pid);
    EXPECT_GT(info->rss, std::size_t{0});

    // Query via the static overload.
    auto info2 = process::query_info(pid);
    ASSERT_TRUE(info2.has_value());
    EXPECT_EQ(info2->pid, pid);

    auto worker = wait_for_exit(spawn_res->proc);
    schedule_all(worker);
}

TEST_CASE(query_info_invalid_pid) {
    // A very large pid should not correspond to any real process.
    auto info = process::query_info(999999999);
    EXPECT_FALSE(info.has_value());
}

};  // TEST_SUITE(process_io)

}  // namespace eventide
