#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "execution.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace kota::zest {

namespace {

/// The worker's end of the runner's channel.
struct Channel {
#ifdef _WIN32
    HANDLE handle;
#else
    int fd;
#endif
    /// Bytes read past the last complete line.
    std::string pending = {};

    /// Moves the channel off stdin, which then reads nothing: a test that
    /// starts a process inheriting stdin must not hand it the runner's
    /// commands, or keep the channel open after this worker dies.
    static Channel take_stdin() {
#ifdef _WIN32
        HANDLE handle = nullptr;
        DuplicateHandle(GetCurrentProcess(),
                        GetStdHandle(STD_INPUT_HANDLE),
                        GetCurrentProcess(),
                        &handle,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS);
        // For a console program, _dup2 onto fd 0 repoints STD_INPUT_HANDLE too.
        int null = ::_open("NUL", _O_RDONLY);
        ::_dup2(null, 0);
        ::_close(null);
        return Channel{.handle = handle};
#else
        int fd = ::fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 3);
        int null = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        ::dup2(null, STDIN_FILENO);
        ::close(null);
        return Channel{.fd = fd};
#endif
    }

    /// The next line without its newline, or nothing once the runner hangs up.
    std::optional<std::string> read_line() {
        while(true) {
            if(auto newline = pending.find('\n'); newline != std::string::npos) {
                auto line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                return line;
            }
            char buffer[4096];
#ifdef _WIN32
            DWORD count = 0;
            if(!ReadFile(handle, buffer, sizeof(buffer), &count, nullptr) || count == 0) {
                return std::nullopt;
            }
#else
            auto count = ::read(fd, buffer, sizeof(buffer));
            if(count < 0 && errno == EINTR) {
                continue;
            }
            if(count <= 0) {
                return std::nullopt;
            }
#endif
            pending.append(buffer, static_cast<std::size_t>(count));
        }
    }

    /// Writes all of `data`; stops early only once the runner has gone, which
    /// the next read_line reports.
    void write(std::string_view data) {
        while(!data.empty()) {
#ifdef _WIN32
            DWORD count = 0;
            if(!WriteFile(handle, data.data(), static_cast<DWORD>(data.size()), &count, nullptr)) {
                return;
            }
#else
            auto count = ::write(fd, data.data(), data.size());
            if(count < 0 && errno == EINTR) {
                continue;
            }
            if(count < 0) {
                return;
            }
#endif
            data.remove_prefix(static_cast<std::size_t>(count));
        }
    }
};

/// The runner's channel, which a failed ZASSERT on any thread replies on too.
std::optional<Channel> channel;

/// Taken for each reply. A failed ZASSERT keeps it to the end, so that the
/// test it ends, finishing meanwhile on another thread, sends no reply.
std::mutex reply_mutex;

/// Replies for the test that ran, with the snapshots it checked; everything it
/// printed is in the log by then.
void reply(TestState state, bool fatal) {
    flush_output();
    std::string text;
    for(const auto& path: take_accessed_snapshots()) {
        text += std::format("{}{}\n", protocol::snapshot, path);
    }
    if(fatal) {
        text += std::format("{}\n", protocol::fatal);
    }
    text += std::format("{}{}\n", protocol::done, protocol::state_name(state));
    channel->write(text);
}

}  // namespace

namespace protocol {

std::string_view state_name(TestState state) {
    switch(state) {
        case TestState::Passed: return "passed";
        case TestState::Skipped: return "skipped";
        case TestState::Failed: return "failed";
    }
    std::unreachable();
}

std::optional<TestState> parse_state(std::string_view name) {
    for(auto state: {TestState::Passed, TestState::Skipped, TestState::Failed}) {
        if(name == state_name(state)) {
            return state;
        }
    }
    return std::nullopt;
}

}  // namespace protocol

void serve(std::span<const Entry> entries) {
    channel = Channel::take_stdin();
    fatal_notice = [] {
        reply_mutex.lock();
        reply(TestState::Failed, true);
    };

    // Output goes to a file the runner reads after each test. Unbuffered, all
    // of it is there by the time the test is reported, even after a crash.
    std::fflush(stdout);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::unordered_map<std::string_view, const Entry*> tests;
    for(const auto& entry: entries) {
        tests.emplace(entry.name, &entry);
    }
    channel->write(std::format("{}\n", protocol::ready));

    while(auto line = channel->read_line()) {
        assert(line->starts_with(protocol::run));
        auto name = std::string_view(*line).substr(protocol::run.size());
        // The runner names only tests it collected from this same program.
        auto test = tests.find(name);
        assert(test != tests.end());

        auto state = run_in_process(*test->second);
        std::lock_guard lock(reply_mutex);
        reply(state, false);
    }
}

}  // namespace kota::zest
