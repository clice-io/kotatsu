// Closes a stdio transport while a write to stdout is pending, stdout being a
// pipe another descriptor also holds, for transport.test.ts. It sets its own
// fds 0 and 1 up that way, closes the transport, reads some of the pipe so
// that it takes writes again, waits 300 ms on a timer, and writes to stderr
// how much CPU time the wait took:
//
//   idle cpu: <ms> ms
//
// Closing must leave nothing for the loop to poll: the pipe left registered
// with the loop would wake it for good once writable. POSIX only: on Windows
// it writes nothing and exits with 0.

#include <cstdio>
#include <string>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include "kota/ipc/transport.h"
#include "kota/async/async.h"

namespace kota::test {
namespace {

#ifndef _WIN32

double cpu_ms() {
    rusage usage{};
    ::getrusage(RUSAGE_SELF, &usage);
    auto ms = [](const timeval& time) {
        return static_cast<double>(time.tv_sec) * 1e3 + static_cast<double>(time.tv_usec) / 1e3;
    };
    return ms(usage.ru_utime) + ms(usage.ru_stime);
}

int run() {
    int output[2] = {-1, -1};
    int input[2] = {-1, -1};
    if(::pipe(output) != 0 || ::pipe(input) != 0) {
        std::perror("pipe");
        return 2;
    }
    // output[1] stays open, another holder of stdout's pipe, as a child that
    // inherited it would be; nobody reads output[0], so a large write stays
    // pending.
    if(::dup2(output[1], 1) < 0 || ::dup2(input[0], 0) < 0) {
        std::perror("dup2");
        return 2;
    }

    event_loop loop;
    auto opened = ipc::StreamTransport::open_stdio(loop);
    if(!opened) {
        std::fprintf(stderr, "open_stdio failed: %s\n", opened.error().message.c_str());
        return 2;
    }
    auto transport = std::move(*opened);
    double idle_cpu = -1;

    auto writer = [&]() -> task<> {
        auto written = co_await transport->write_message(std::string(1 << 20, 'x')).catch_cancel();
        static_cast<void>(written);
    };
    auto closer = [&]() -> task<> {
        // The write is pending by then.
        co_await sleep(50, loop);
        auto closed = transport->close();
        if(!closed) {
            std::fprintf(stderr, "close failed: %s\n", closed.error().message.c_str());
        }
        std::string drained(65536, '\0');
        ::fcntl(output[0], F_SETFL, O_NONBLOCK);
        if(::read(output[0], drained.data(), drained.size()) <= 0) {
            std::perror("read");
        }
        co_await sleep(20, loop);
        const auto before = cpu_ms();
        co_await sleep(300, loop);
        idle_cpu = cpu_ms() - before;
        loop.stop();
    };

    auto writing = writer();
    auto closing = closer();
    loop.schedule(writing);
    loop.schedule(closing);
    loop.run();
    std::fprintf(stderr, "idle cpu: %.0f ms\n", idle_cpu);
    return 0;
}

#else

int run() {
    return 0;
}

#endif

}  // namespace
}  // namespace kota::test

int main() {
    return kota::test::run();
}
