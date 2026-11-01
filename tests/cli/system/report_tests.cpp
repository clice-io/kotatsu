// What report() and print_usage() write to a descriptor that is no terminal: the text, in no
// color.

#include <array>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

/// What is written to the write end of a pipe while `write` runs, read once it closes.
template <typename Write>
std::string captured(Write write) {
    std::array<int, 2> ends{};
#ifdef _WIN32
    ZASSERT(::_pipe(ends.data(), 1 << 16, _O_BINARY) == 0);
#else
    ZASSERT(::pipe(ends.data()) == 0);
#endif
    write(ends[1]);
    std::string text;
    std::array<char, 4096> chunk{};
#ifdef _WIN32
    ::_close(ends[1]);
    for(int n = 0; (n = ::_read(ends[0], chunk.data(), static_cast<unsigned>(chunk.size()))) > 0;) {
        text.append(chunk.data(), static_cast<std::size_t>(n));
    }
    ::_close(ends[0]);
#else
    ::close(ends[1]);
    for(ssize_t n = 0; (n = ::read(ends[0], chunk.data(), chunk.size())) > 0;) {
        text.append(chunk.data(), static_cast<std::size_t>(n));
    }
    ::close(ends[0]);
#endif
    return text;
}

KANNOTATE_TYPE(struct Options, description = "A tool");

struct Options {
    KANNOTATE(description = "Workers")
    <int> jobs = 2;
};

ZEST_SUITE(cli_report) {

ZEST_CASE(report_writes_the_rendered_error_and_a_newline) {
    const Error error{.kind = Error::Kind::UnknownOption,
                      .message = "unknown option '--x'",
                      .argv = {"--x"},
                      .begin = 0,
                      .end = 1};
    ZEXPECT(captured([&](int fd) { cli::report(error, fd); }) == error.render() + "\n");
}

ZEST_CASE(print_usage_writes_the_usage) {
    ZEXPECT(captured([](int fd) { cli::print_usage<Options>("tool", fd); }) ==
            cli::usage<Options>("tool"));
}

ZEST_CASE(style_for_a_pipe_has_no_color) {
    static_cast<void>(captured([](int fd) { ZEXPECT(!cli::style_for(fd).color); }));
}

};  // ZEST_SUITE(cli_report)

}  // namespace

}  // namespace kotatsu::cli
