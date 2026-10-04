#include <algorithm>
#include <array>
#include <cstddef>
#include <fcntl.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "async/harness/io.h"
#include "async/harness/os.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

/// A name for pipe::listen() nobody else uses: a socket in `dir`, or on
/// Windows a named pipe named after it.
std::string pipe_name(const test::TempDir& dir) {
#ifdef _WIN32
    return R"(\\.\pipe\)" + dir.path.filename().string();
#else
    return dir.file("socket");
#endif
}

/// An anonymous pipe whose write end already holds `text` and is closed.
result<pipe> pipe_holding(std::string_view text, event_loop& loop) {
    int fds[2] = {-1, -1};
    if(test::create_pipe(fds) != 0) {
        return outcome_error(error::io_error);
    }
    auto written = test::write_fd(fds[1], text.data(), text.size());
    test::close_fd(fds[1]);
    if(written != static_cast<ssize_t>(text.size())) {
        test::close_fd(fds[0]);
        return outcome_error(error::io_error);
    }
    return pipe::open(fds[0], loop);
}

/// Both ends of an anonymous pipe, opened as kota pipes.
struct Ends {
    pipe reader;
    pipe writer;
};

result<Ends> pipe_ends(event_loop& loop) {
    int fds[2] = {-1, -1};
    if(test::create_pipe(fds) != 0) {
        return outcome_error(error::io_error);
    }
    auto reader = pipe::open(fds[0], loop);
    auto writer = pipe::open(fds[1], loop);
    if(!reader || !writer) {
        return outcome_error(error::io_error);
    }
    return Ends{.reader = std::move(*reader), .writer = std::move(*writer)};
}

ZEST_SUITE(async_io_stream_pipe, zest::LoopFixture) {

ZEST_CASE(read_returns_what_was_written_then_eof) {
    auto reader = pipe_holding("kotatsu-pipe", loop);
    ZASSERT(reader.has_value());
    auto read_twice = [&]() -> task<std::pair<result<std::string>, result<std::string>>> {
        auto first = co_await reader->read();
        auto second = co_await reader->read();
        co_return std::pair{std::move(first), std::move(second)};
    };

    auto [result] = run(read_twice());
    ZASSERT(result.has_value());
    auto& [first, second] = *result;
    ZASSERT(first.has_value());
    ZEXPECT(*first == "kotatsu-pipe");
    ZASSERT(second.has_error());
    ZEXPECT(second.error() == error::end_of_file);
}

// A line ends with "\n" or "\r\n"; what follows the last line break is a
// line of its own.
ZEST_CASE(read_line_splits_the_stream_into_lines) {
    auto reader = pipe_holding("first\r\nsecond\n\nlast", loop);
    ZASSERT(reader.has_value());
    auto read_lines = [&]() -> task<std::vector<std::string>, error> {
        std::vector<std::string> lines;
        while(auto line = co_await reader->read_line().or_fail()) {
            lines.push_back(std::move(*line));
        }
        co_return lines;
    };

    auto [lines] = run(read_lines());
    ZASSERT(lines.has_value());
    ZEXPECT(*lines == std::vector<std::string>{"first", "second", "", "last"});
}

// The "\r" of a "\r\n" is buffered before its "\n" is written, so the two
// come in different chunks; the line still ends without it.
ZEST_CASE(read_line_drops_a_carriage_return_read_before_its_newline) {
    int fds[2] = {-1, -1};
    ZASSERT(test::create_pipe(fds) == 0);
    auto reader = pipe::open(fds[0], loop);
    ZASSERT(reader.has_value());
    ZASSERT(test::write_fd(fds[1], "first\r", 6) == 6);
    auto read_split = [&]() -> task<std::optional<std::string>, error> {
        co_await reader->read_chunk().or_fail();
        test::write_fd(fds[1], "\n", 1);
        test::close_fd(fds[1]);
        co_return co_await reader->read_line().or_fail();
    };

    auto [line] = run(read_split());
    ZASSERT(line.has_value());
    ZEXPECT(*line == std::optional<std::string>("first"));
}

ZEST_CASE(read_line_after_the_last_line_break_reads_nothing) {
    auto reader = pipe_holding("line\n", loop);
    ZASSERT(reader.has_value());
    auto read_twice = [&]() -> task<std::vector<std::optional<std::string>>, error> {
        auto first = co_await reader->read_line().or_fail();
        auto second = co_await reader->read_line().or_fail();
        co_return std::vector{std::move(first), std::move(second)};
    };

    auto [lines] = run(read_twice());
    ZASSERT(lines.has_value());
    ZEXPECT(*lines == std::vector<std::optional<std::string>>{"line", std::nullopt});
}

// An empty buffer reads nothing without waiting; then read_some reads four
// bytes at most per call, and zero at the end.
ZEST_CASE(read_some_fills_the_buffer_and_reports_eof_as_zero) {
    auto reader = pipe_holding("abcdef", loop);
    ZASSERT(reader.has_value());
    auto read_all = [&]() -> task<std::pair<std::size_t, std::vector<std::string>>, error> {
        auto nothing = co_await reader->read_some(std::span<char>()).or_fail();
        std::vector<std::string> pieces;
        std::array<char, 4> buffer{};
        while(true) {
            auto count = co_await reader->read_some(buffer).or_fail();
            if(count == 0) {
                break;
            }
            pieces.emplace_back(buffer.data(), count);
        }
        co_return std::pair{nothing, std::move(pieces)};
    };

    auto [result] = run(read_all());
    ZASSERT(result.has_value());
    ZEXPECT(result->first == 0U);
    ZEXPECT(result->second == std::vector<std::string>{"abcd", "ef"});
}

ZEST_CASE(read_chunk_shows_the_buffer_until_consumed) {
    auto reader = pipe_holding("chunk", loop);
    ZASSERT(reader.has_value());
    auto chunks = [&]() -> task<std::pair<std::string, std::string>, error> {
        auto first = co_await reader->read_chunk().or_fail();
        std::string seen(first.data(), first.size());
        auto again = co_await reader->read_chunk().or_fail();
        std::string seen_again(again.data(), again.size());
        reader->consume(again.size());
        co_return std::pair{std::move(seen), std::move(seen_again)};
    };

    auto [result] = run(chunks());
    ZASSERT(result.has_value());
    ZEXPECT(result->first == "chunk");
    ZEXPECT(result->second == "chunk");
    auto [at_end] = run(reader->read_chunk());
    ZASSERT(at_end.has_error());
    ZEXPECT(at_end.error() == error::end_of_file);
}

// What read_chunk() buffered and consume() left is what read_some() and
// read() hand out next, without waiting for the pipe.
ZEST_CASE(reads_serve_what_is_already_buffered) {
    auto reader = pipe_holding("abcdef", loop);
    ZASSERT(reader.has_value());
    auto read_in_parts = [&]() -> task<std::vector<std::string>, error> {
        auto chunk = co_await reader->read_chunk().or_fail();
        std::vector<std::string> parts{std::string(chunk.data(), 2)};
        reader->consume(2);
        std::array<char, 2> buffer{};
        auto count = co_await reader->read_some(buffer).or_fail();
        parts.emplace_back(buffer.data(), count);
        parts.push_back(co_await reader->read().or_fail());
        co_return parts;
    };

    auto [result] = run(read_in_parts());
    ZASSERT(result.has_value());
    ZEXPECT(*result == std::vector<std::string>{"ab", "cd", "ef"});
}

// The writer sends its second chunk only once the reader has consumed the
// first, so read_some() after read_chunk() sees just the second.
ZEST_CASE(read_some_after_read_chunk_reads_on) {
    int fds[2] = {-1, -1};
    ZASSERT(test::create_pipe(fds) == 0);
    auto reader = pipe::open(fds[0], loop);
    ZASSERT(reader.has_value());
    event consumed;
    auto read_both = [&]() -> task<std::pair<std::string, std::string>, error> {
        auto first = co_await reader->read_chunk().or_fail();
        std::string one(first.data(), first.size());
        reader->consume(first.size());
        consumed.set();
        std::array<char, 64> buffer{};
        auto count = co_await reader->read_some(buffer).or_fail();
        co_return std::pair{std::move(one), std::string(buffer.data(), count)};
    };
    auto write_both = [&]() -> task<std::vector<ssize_t>> {
        std::vector<ssize_t> written{test::write_fd(fds[1], "kotatsu-chunk", 13)};
        co_await consumed.wait();
        written.push_back(test::write_fd(fds[1], "kotatsu-read-some", 17));
        test::close_fd(fds[1]);
        co_return written;
    };

    auto [read, wrote] = run(read_both(), write_both());
    ZASSERT(read.has_value());
    ZEXPECT(read->first == "kotatsu-chunk");
    ZEXPECT(read->second == "kotatsu-read-some");
    ZASSERT(wrote.has_value());
    ZEXPECT(*wrote == std::vector<ssize_t>{13, 17});
}

// A Linux pipe that holds exactly as much as the stream's buffer takes gets
// drained by one read that fills the buffer, so libuv reads again at once,
// finds the pipe empty and reports that as 0 bytes: not data, nor the end.
#ifdef __linux__
ZEST_CASE(read_after_draining_a_full_buffer_waits_for_data) {
    int fds[2] = {-1, -1};
    ZASSERT(test::create_pipe(fds) == 0);
    const std::string full(64 * 1024, 'x');
    // A user past pipe-user-pages-soft gets smaller pipes, which the write
    // below would block on for good.
    if(::fcntl(fds[1], F_GETPIPE_SZ) != static_cast<int>(full.size())) {
        test::close_fd(fds[0]);
        test::close_fd(fds[1]);
        zest::skip();
        return;
    }
    ZASSERT(test::write_fd(fds[1], full.data(), full.size()) == static_cast<ssize_t>(full.size()));
    auto reader = pipe::open(fds[0], loop);
    ZASSERT(reader.has_value());
    event drained;
    auto read_both = [&]() -> task<std::pair<std::size_t, std::string>, error> {
        std::string first;
        while(first.size() < full.size()) {
            first += co_await reader->read().or_fail();
        }
        drained.set();
        auto second = co_await reader->read().or_fail();
        co_return std::pair{first.size(), std::move(second)};
    };
    auto write_tail = [&]() -> task<ssize_t> {
        co_await drained.wait();
        // Not before libuv has found the pipe empty, on this turn.
        co_await yield();
        auto written = test::write_fd(fds[1], "tail", 4);
        test::close_fd(fds[1]);
        co_return written;
    };

    auto [read, written] = run(read_both(), write_tail());
    ZASSERT(read.has_value());
    ZEXPECT(read->first == full.size());
    ZEXPECT(read->second == "tail");
    ZASSERT(written.has_value());
    ZEXPECT(*written == 4);
}
#endif

// Once the unread bytes wrap around the stream's buffer, read() takes them
// all, not only the piece up to the buffer's end.
#ifdef __linux__
ZEST_CASE(read_takes_what_wraps_around_the_buffer) {
    int fds[2] = {-1, -1};
    ZASSERT(test::create_pipe(fds) == 0);
    const std::string first(48 * 1024, 'a');
    const std::string second(40 * 1024, 'b');
    // A user past pipe-user-pages-soft gets smaller pipes, which the writes
    // below would block on for good.
    if(::fcntl(fds[1], F_GETPIPE_SZ) < static_cast<int>(first.size())) {
        test::close_fd(fds[0]);
        test::close_fd(fds[1]);
        zest::skip();
        return;
    }
    ZASSERT(test::write_fd(fds[1], first.data(), first.size()) ==
            static_cast<ssize_t>(first.size()));
    auto reader = pipe::open(fds[0], loop);
    ZASSERT(reader.has_value());
    auto read_around = [&]() -> task<std::pair<std::size_t, std::string>, error> {
        // One read takes all the pipe holds.
        auto held = co_await reader->read_chunk().or_fail();
        reader->consume(40 * 1024);
        test::write_fd(fds[1], second.data(), second.size());
        // libuv reads it on this turn: up to the buffer's end, then from its
        // start.
        co_await yield();
        auto all = co_await reader->read().or_fail();
        co_return std::pair{held.size(), std::move(all)};
    };

    auto [read] = run(read_around());
    test::close_fd(fds[1]);
    ZASSERT(read.has_value());
    ZEXPECT(read->first == first.size());
    ZEXPECT(read->second == std::string(8 * 1024, 'a') + second);
}
#endif

// The reader looks at what arrives without consuming it until the buffer is
// full, then drains it all: reading stops while the buffer is full and picks
// up once it is drained, losing nothing of a MiB.
ZEST_CASE(full_buffer_holds_the_rest_back_until_drained) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    std::string sent(1024 * 1024, '\0');
    for(std::size_t i = 0; i < sent.size(); ++i) {
        sent[i] = static_cast<char>('a' + i % 26);
    }
    auto send = [&]() -> task<void, error> {
        co_await ends->writer.write(sent).or_fail();
        ends->writer = pipe{};
    };
    auto receive = [&]() -> task<std::pair<std::size_t, std::string>, error> {
        std::size_t held = 0;
        while(held < 64 * 1024) {
            held = (co_await ends->reader.read_chunk().or_fail()).size();
            co_await yield();
        }
        std::string received;
        std::size_t largest = 0;
        while(true) {
            auto chunk = co_await ends->reader.read_chunk();
            if(!chunk) {
                if(chunk.error() != error::end_of_file) {
                    co_await fail(chunk.error());
                }
                co_return std::pair{largest, std::move(received)};
            }
            largest = std::max(largest, chunk->size());
            received.append(chunk->data(), chunk->size());
            ends->reader.consume(chunk->size());
        }
    };

    auto [sent_all, received] = run(send(), receive());
    ZEXPECT(sent_all.has_value());
    ZASSERT(received.has_value());
    ZEXPECT(received->first == 64U * 1024);
    ZEXPECT(received->second == sent);
}

ZEST_CASE(second_read_while_one_is_pending_fails) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    auto second_then_write = [&]() -> task<error> {
        auto second = co_await ends->reader.read();
        auto written = co_await ends->writer.write(std::string_view("first"));
        ZEXPECT(written.has_value());
        co_return second.has_error() ? second.error() : error();
    };

    auto [first, second] = run(ends->reader.read(), second_then_write());
    ZASSERT(first.has_value());
    ZEXPECT(*first == "first");
    ZASSERT(second.has_value());
    ZEXPECT(*second == error::resource_busy_or_locked);
}

ZEST_CASE(read_from_the_write_end_fails) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    std::array<char, 8> buffer{};

    auto [buffered, direct] = run(ends->writer.read(), ends->writer.read_some(buffer));
    ZASSERT(buffered.has_error());
    ZEXPECT(buffered.error() == error::socket_is_not_connected);
    ZASSERT(direct.has_error());
    ZEXPECT(direct.error() == error::socket_is_not_connected);
}

ZEST_CASE(write_reaches_the_reader) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    auto send = [&]() -> task<void, error> {
        co_await ends->writer.write(std::string_view("kotatsu-write")).or_fail();
        // Closing the write end lets the reader see the end.
        ends->writer = pipe{};
    };

    auto [sent, received] = run(send(), ends->reader.read_to_end());
    ZEXPECT(sent.has_value());
    ZASSERT(received.has_value());
    ZEXPECT(*received == "kotatsu-write");
}

// The first write is larger than the pipe holds, so it is still going out
// when the second is made.
ZEST_CASE(overlapping_writes_arrive_in_order) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    const std::string first(128 * 1024, 'a');
    const std::string second(128 * 1024, 'b');
    auto send = [&]() -> task<void, error> {
        co_await or_fail(co_await when_all(ends->writer.write(first), ends->writer.write(second)));
        ends->writer = pipe{};
    };

    auto [sent, received] = run(send(), ends->reader.read_to_end());
    ZEXPECT(sent.has_value());
    ZASSERT(received.has_value());
    ZEXPECT(*received == first + second);
}

// libuv cannot take a write back: a cancelled write still goes out, and its
// task ends cancelled once it has, never resuming past it.
ZEST_CASE(cancelled_write_still_delivers) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    bool resumed = false;
    auto write = [&]() -> task<> {
        [[maybe_unused]] auto written = co_await ends->writer.write(std::string_view("kept"));
        resumed = true;
    };

    auto [raced, received] = run(test::winner(write(), test::finished()), ends->reader.read());
    ZASSERT(raced.has_value());
    ZEXPECT(*raced == 1U);
    ZEXPECT(!resumed);
    ZASSERT(received.has_value());
    ZEXPECT(*received == "kept");
}

// Nothing reads the pipe, so the write is still going out, and the shutdown
// waits behind it, when their stream closes: libuv ends both, which fails
// them rather than cancelling them. Windows writes an anonymous pipe from a
// thread that the close cannot stop.
#ifndef _WIN32
ZEST_CASE(write_and_shutdown_ended_by_a_close_fails) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    const std::string large(4 * 1024 * 1024, 'x');
    auto close_it = [&]() -> task<> {
        co_await yield();
        ends->writer = pipe{};
    };

    auto [written, shut, closed] =
        run(ends->writer.write(large), ends->writer.shutdown(), close_it());
    ZASSERT(written.has_error());
    ZEXPECT(written.error() == error::operation_aborted);
    ZASSERT(shut.has_error());
    ZEXPECT(shut.error() == error::operation_aborted);
}
#endif

#ifndef _WIN32
// The loop ignores SIGPIPE, which would end the process at a write to a pipe
// nobody reads.
ZEST_CASE(write_to_a_pipe_nobody_reads_fails) {
    int fds[2] = {-1, -1};
    ZASSERT(test::create_pipe(fds) == 0);
    test::close_fd(fds[0]);
    auto writer = pipe::open(fds[1], loop);
    ZASSERT(writer.has_value());
    auto write = [&]() -> task<void, error> {
        std::string_view text = "text";
        co_await writer->write(std::span(text.data(), text.size())).or_fail();
    };

    auto [written] = run(write());
    ZASSERT(written.has_error());
    ZEXPECT(written.error() == error::broken_pipe);
}
#endif

ZEST_CASE(write_of_nothing_fails) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());

    auto [result] = run(ends->writer.write({}));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::invalid_argument);
}

ZEST_CASE(write_to_the_read_end_fails) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());

    auto [result] = run(ends->reader.write(std::string_view("x")));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::broken_pipe);
}

ZEST_CASE(try_write_of_nothing_writes_nothing) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());

    auto written = ends->writer.try_write({});
    ZASSERT(written.has_value());
    ZEXPECT(*written == 0U);
}

// Windows pipes do not report a full buffer to try_write the same way.
#ifndef _WIN32
ZEST_CASE(try_write_to_a_full_pipe_fails) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    std::string chunk(4096, 'x');

    // Linux pipes hold at most 1 MiB; no write gets anywhere near 1000 chunks.
    error refused;
    for(int i = 0; i < 1000 && !refused; ++i) {
        auto written = ends->writer.try_write(chunk);
        if(!written) {
            refused = written.error();
        }
    }
    ZEXPECT(refused == error::resource_temporarily_unavailable);
}
#endif

ZEST_CASE(ends_report_their_direction) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    ZEXPECT(ends->reader.readable());
    ZEXPECT(!ends->reader.writable());
    ZEXPECT(ends->writer.writable());
    ZEXPECT(!ends->writer.readable());
    ZEXPECT(!ends->writer.set_blocking(true));

    pipe inert;
    ZEXPECT(!inert.readable());
    ZEXPECT(!inert.writable());
}

ZEST_CASE(inert_stream_fails) {
    pipe inert;
    std::array<char, 8> buffer{};

    auto [read, read_some, chunk, written, shut] = run(inert.read(),
                                                       inert.read_some(buffer),
                                                       inert.read_chunk(),
                                                       inert.write(std::string_view("x")),
                                                       inert.shutdown());
    ZASSERT(read.has_error());
    ZEXPECT(read.error() == error::invalid_argument);
    ZASSERT(read_some.has_error());
    ZEXPECT(read_some.error() == error::invalid_argument);
    ZASSERT(chunk.has_error());
    ZEXPECT(chunk.error() == error::invalid_argument);
    ZASSERT(written.has_error());
    ZEXPECT(written.error() == error::invalid_argument);
    ZASSERT(shut.has_error());
    ZEXPECT(shut.error() == error::invalid_argument);
    ZEXPECT(inert.stop() == error::invalid_argument);
    ZEXPECT(inert.set_blocking(true) == error::invalid_argument);
    auto tried = inert.try_write(std::string_view("x"));
    ZASSERT(tried.has_error());
    ZEXPECT(tried.error() == error::invalid_argument);
}

// stop() is not sticky: the read after it reads again.
ZEST_CASE(stop_ends_a_pending_read) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    std::array<char, 8> buffer{};
    auto stop_it = [&]() -> task<error> {
        co_return ends->reader.stop();
    };

    auto [buffered, stopped] = run(ends->reader.read(), stop_it());
    ZASSERT(buffered.has_error());
    ZEXPECT(buffered.error() == error::operation_aborted);
    ZASSERT(stopped.has_value());
    ZEXPECT(!*stopped);
    auto [direct, again] = run(ends->reader.read_some(buffer), stop_it());
    ZASSERT(direct.has_error());
    ZEXPECT(direct.error() == error::operation_aborted);
    ZASSERT(again.has_value());
    ZEXPECT(!*again);
    auto exchange = [&]() -> task<std::string, error> {
        co_await ends->writer.write(std::string_view("after")).or_fail();
        co_return co_await ends->reader.read().or_fail();
    };
    auto [received] = run(exchange());
    ZASSERT(received.has_value());
    ZEXPECT(*received == "after");
}

// Nothing is written, so only the cancels can end the reads.
ZEST_CASE(cancelled_reads_leave_the_pipe_usable) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    std::array<char, 8> buffer{};
    auto cancel_reads = [&]() -> task<std::pair<std::size_t, std::size_t>, error> {
        auto buffered = co_await or_fail(co_await when_any(ends->reader.read(), yield()));
        auto direct = co_await or_fail(co_await when_any(ends->reader.read_some(buffer), yield()));
        co_return std::pair{buffered.index(), direct.index()};
    };
    auto exchange = [&]() -> task<std::string, error> {
        co_await ends->writer.write(std::string_view("after")).or_fail();
        co_return co_await ends->reader.read().or_fail();
    };

    auto [cancelled] = run(cancel_reads());
    ZASSERT(cancelled.has_value());
    ZEXPECT(*cancelled == std::pair<std::size_t, std::size_t>{1, 1});
    auto [received] = run(exchange());
    ZASSERT(received.has_value());
    ZEXPECT(*received == "after");
}

ZEST_CASE(read_ended_by_destroying_its_stream_fails) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    std::optional<pipe> reader = std::move(ends->reader);
    auto destroy = [&]() -> task<> {
        reader.reset();
        co_return;
    };

    auto [read, destroyed] = run(reader->read(), destroy());
    ZASSERT(read.has_error());
    ZEXPECT(read.error() == error::operation_aborted);
}

// The destroyed stream's read is cancelled after its destruction has ended
// it, before the loop has resumed it: the cancel leaves that ending alone.
ZEST_CASE(read_cancelled_after_its_stream_is_destroyed_ends) {
    auto ends = pipe_ends(loop);
    ZASSERT(ends.has_value());
    std::optional<pipe> reader = std::move(ends->reader);
    auto destroy = [&]() -> task<> {
        reader.reset();
        co_return;
    };

    auto [result] = run(test::winner(reader->read(), destroy()));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
}

ZEST_CASE(open_of_a_bad_descriptor_fails) {
    auto opened = pipe::open(-1, loop);
    ZASSERT(opened.has_error());
    ZEXPECT(opened.error() == error::bad_file_descriptor);
}

// The loop cannot wait on a regular file to read it: on Linux epoll refuses
// one, and libuv would abort at the first read; on macOS kqueue stops
// reporting one at its end. Windows opens no handle but a pipe's.
ZEST_CASE(read_of_a_file_fails) {
    test::TempDir dir;
    test::write_file(dir.file("file.txt"), "text");
    auto file = fs::sync::open(dir.file("file.txt"), O_RDONLY, 0);
    ZASSERT(file.has_value());

    auto opened = pipe::open(*file, loop);
#ifdef _WIN32
    ZASSERT(opened.has_error());
    ZEXPECT(opened.error() == error::socket_operation_on_non_socket);
    ZEXPECT(!fs::sync::close(*file));
#else
    ZASSERT(opened.has_value());
    auto read_each_way = [&]() -> task<std::vector<error>> {
        auto read = co_await opened->read();
        auto rest = co_await opened->read_to_end();
        auto line = co_await opened->read_line();
        co_return std::vector{read.has_error() ? read.error() : error(),
                              rest.has_error() ? rest.error() : error(),
                              line.has_error() ? line.error() : error()};
    };

    auto [errors] = run(read_each_way());
    ZASSERT(errors.has_value());
    ZEXPECT(*errors == std::vector<error>(3, error::socket_operation_on_non_socket));
#endif
}

#ifndef _WIN32
// The null device as a child's ignored stdout is open for reading and
// writing: writes to it go out, whether or not the loop can wait to read it.
ZEST_CASE(open_of_the_null_device_to_read_and_write_writes_to_it) {
    auto file = fs::sync::open("/dev/null", O_RDWR, 0);
    ZASSERT(file.has_value());
    auto opened = pipe::open(*file, loop);
    ZASSERT(opened.has_value());
    auto write_then_read = [&]() -> task<error, error> {
        std::string_view text = "text";
        co_await opened->write(std::span(text.data(), text.size())).or_fail();
        auto read = co_await opened->read();
        co_return read.has_error() ? read.error() : error();
    };

    auto [read] = run(write_then_read());
    ZASSERT(read.has_value());
#ifdef __linux__
    // epoll refuses the null device.
    ZEXPECT(*read == error::socket_operation_on_non_socket);
#else
    // kqueue reads it to its end.
    ZEXPECT(*read == error::end_of_file);
#endif
}

// Writes to a regular file never wait, so the loop never watches one open
// only for writing.
ZEST_CASE(open_of_a_file_to_write_writes_to_it) {
    test::TempDir dir;
    auto file = fs::sync::open(dir.file("file.txt"), O_CREAT | O_WRONLY, 0644);
    ZASSERT(file.has_value());
    auto opened = pipe::open(*file, loop);
    ZASSERT(opened.has_value());
    auto writer = [&]() -> task<void, error> {
        std::string_view text = "text";
        co_await opened->write(std::span(text.data(), text.size())).or_fail();
    };

    auto [written] = run(writer());
    ZEXPECT(written.has_value());
    *opened = pipe();
    ZEXPECT(test::read_file(dir.file("file.txt")) == "text");
}
#endif

ZEST_CASE(guess_handle_tells_a_pipe_from_a_file) {
    test::TempDir dir;
    int fds[2] = {-1, -1};
    ZASSERT(test::create_pipe(fds) == 0);
    auto file = fs::sync::open(dir.file("file.txt"), O_CREAT | O_WRONLY, 0644);

    auto pipe_kind = guess_handle(fds[0]);
    test::close_fd(fds[0]);
    test::close_fd(fds[1]);
    ZASSERT(file.has_value());
    auto file_kind = guess_handle(*file);
    ZEXPECT(!fs::sync::close(*file));
    ZEXPECT(pipe_kind == handle_type::pipe);
    ZEXPECT(file_kind == handle_type::file);
    ZEXPECT(guess_handle(-1) == handle_type::unknown);
}

ZEST_CASE(listener_accepts_what_a_client_writes) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, {.backlog = 16}, loop);
    ZASSERT(listener.has_value());
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->accept().or_fail();
        co_return co_await connection.read().or_fail();
    };
    auto client = [&]() -> task<void, error> {
        auto connection = co_await pipe::connect(name).or_fail();
        co_await connection.write(std::string_view("kotatsu-pipe-connect")).or_fail();
    };

    auto [received, sent] = run(serve(), client());
    ZASSERT(received.has_value());
    ZEXPECT(*received == "kotatsu-pipe-connect");
    ZEXPECT(sent.has_value());
}

ZEST_CASE(connect_to_a_missing_name_fails) {
    test::TempDir dir;

    auto [result] = run(pipe::connect(pipe_name(dir), loop));
    ZASSERT(result.has_error());
    ZEXPECT(result.error() == error::no_such_file_or_directory);
}

// The cancel closes the connection it interrupts, and the connect's task
// ends cancelled, never resuming past it: the listener's end reads EOF.
ZEST_CASE(connect_can_be_cancelled) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, loop);
    ZASSERT(listener.has_value());
    bool resumed = false;
    auto connect = [&]() -> task<> {
        [[maybe_unused]] auto connected = co_await pipe::connect(name, loop);
        resumed = true;
    };
    auto serve = [&]() -> task<result<std::string>, error> {
        auto connection = co_await listener->accept().or_fail();
        co_return co_await connection.read();
    };

    auto [raced, served] = run(test::winner(connect(), test::finished()), serve());
    ZASSERT(raced.has_value());
    ZEXPECT(*raced == 1U);
    ZEXPECT(!resumed);
    ZASSERT(served.has_value());
    ZASSERT(served->has_error());
    ZEXPECT(served->error() == error::end_of_file);
}

ZEST_CASE(listen_on_a_name_in_use_fails) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto first = pipe::listen(name, loop);
    ZASSERT(first.has_value());

    auto taken = pipe::listen(name, loop);
    ZASSERT(taken.has_error());
    ZEXPECT(taken.error() == error::address_already_in_use);
}

ZEST_CASE(listen_without_a_name_fails) {
    auto unnamed = pipe::listen("", loop);
    ZASSERT(unnamed.has_error());
    ZEXPECT(unnamed.error() == error::invalid_argument);
}

ZEST_CASE(no_truncate_listens_and_connects) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    const pipe::options no_truncate{.no_truncate = true};
    auto listener = pipe::listen(name, no_truncate, loop);
    ZASSERT(listener.has_value());
    auto serve = [&]() -> task<void, error> {
        co_await listener->accept().or_fail();
    };
    auto client = [&]() -> task<void, error> {
        co_await pipe::connect(name, no_truncate).or_fail();
    };

    auto [served, connected] = run(serve(), client());
    ZEXPECT(served.has_value());
    ZEXPECT(connected.has_value());
}

// A socket path longer than sun_path is cut short to fit, unless no_truncate
// has listen() fail instead. Windows never truncates pipe names.
#ifndef _WIN32
ZEST_CASE(listen_on_a_name_too_long_with_no_truncate_fails) {
    test::TempDir dir;
    auto name = dir.file(std::string(200, 'x'));

    auto truncated = pipe::listen(name, loop);
    ZEXPECT(truncated.has_value());
    auto refused = pipe::listen(name, {.no_truncate = true}, loop);
    ZASSERT(refused.has_error());
    ZEXPECT(refused.error() == error::invalid_argument);
}
#endif

// A loop destroyed under a task that writes ends the write, and with it the
// task, which drops its pipe while the loop still closes it; the pipe goes
// once the loop has. Windows writes an anonymous pipe from a thread that the
// close cannot stop.
#ifndef _WIN32
ZEST_CASE(stream_dropped_while_its_loop_closes_it_goes_after) {
    int fds[2] = {-1, -1};
    ZASSERT(test::create_pipe(fds) == 0);
    error written;
    std::optional<event_loop> own(std::in_place);
    auto writer = [&]() -> task<> {
        auto end = pipe::open(fds[1], *own);
        ZASSERT(end.has_value());
        const std::string large(4 * 1024 * 1024, 'x');
        auto result = co_await end->write(large);
        written = result.has_error() ? result.error() : error();
    };
    auto stopper = [&]() -> task<> {
        co_await yield(*own);
        own->stop();
    };

    own->schedule(writer());
    own->schedule(stopper());
    own->run();
    own.reset();
    test::close_fd(fds[0]);
    ZEXPECT(written == error::operation_aborted);
}
#endif

// The shutdown waits for the writes made with it; the listener's end reads
// them, then the end. It goes once it has, which ends the stream here too.
ZEST_CASE(shutdown_lets_the_peer_read_to_the_end) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, loop);
    ZASSERT(listener.has_value());
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->accept().or_fail();
        co_return co_await connection.read_to_end().or_fail();
    };
    auto client = [&]() -> task<std::string, error> {
        auto connection = co_await pipe::connect(name, loop).or_fail();
        co_await or_fail(co_await when_all(connection.write(std::string_view("first")),
                                           connection.write(std::string_view("second")),
                                           connection.shutdown()));
        co_return co_await connection.read_to_end().or_fail();
    };

    auto [served, left] = run(serve(), client());
    ZASSERT(served.has_value());
    ZEXPECT(*served == "firstsecond");
    ZASSERT(left.has_value());
    ZEXPECT(left->empty());
}

// Where a pipe can be half closed, the listener's end answers after reading
// to the end, and the answer still arrives. libuv on Windows closes the whole
// pipe instead, as stream::shutdown() says.
#ifndef _WIN32
ZEST_CASE(shutdown_leaves_the_peer_free_to_answer) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, loop);
    ZASSERT(listener.has_value());
    auto serve = [&]() -> task<void, error> {
        auto connection = co_await listener->accept().or_fail();
        auto request = co_await connection.read_to_end().or_fail();
        co_await connection.write(request + "-answered").or_fail();
    };
    auto client = [&]() -> task<std::string, error> {
        auto connection = co_await pipe::connect(name, loop).or_fail();
        co_await connection.write(std::string_view("asked")).or_fail();
        co_await connection.shutdown().or_fail();
        co_return co_await connection.read_to_end().or_fail();
    };

    auto [served, answer] = run(serve(), client());
    ZEXPECT(served.has_value());
    ZASSERT(answer.has_value());
    ZEXPECT(*answer == "asked-answered");
}
#endif

// stop() ends a pending accept with operation_aborted, and is not sticky:
// the next accept takes the next connection.
ZEST_CASE(acceptor_stop_aborts_an_accept) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, loop);
    ZASSERT(listener.has_value());
    auto stop_it = [&]() -> task<error> {
        co_return listener->stop();
    };

    auto [pending, stopped] = run(listener->accept(), stop_it());
    ZASSERT(pending.has_error());
    ZEXPECT(pending.error() == error::operation_aborted);
    ZASSERT(stopped.has_value());
    ZEXPECT(!*stopped);
    auto [next, connected] = run(listener->accept(), pipe::connect(name, loop));
    ZEXPECT(next.has_value());
    ZEXPECT(connected.has_value());
}

ZEST_CASE(accept_ended_by_destroying_its_acceptor_fails) {
    test::TempDir dir;
    auto listened = pipe::listen(pipe_name(dir), loop);
    ZASSERT(listened.has_value());
    std::optional<pipe::acceptor> listener = std::move(*listened);
    auto destroy = [&]() -> task<> {
        listener.reset();
        co_return;
    };

    auto [accepted, destroyed] = run(listener->accept(), destroy());
    ZASSERT(accepted.has_error());
    ZEXPECT(accepted.error() == error::operation_aborted);
}

// As for a read: the cancel leaves the ending the destruction queued alone.
ZEST_CASE(accept_cancelled_after_its_acceptor_is_destroyed_ends) {
    test::TempDir dir;
    auto listened = pipe::listen(pipe_name(dir), loop);
    ZASSERT(listened.has_value());
    std::optional<pipe::acceptor> listener = std::move(*listened);
    auto destroy = [&]() -> task<> {
        listener.reset();
        co_return;
    };

    auto [result] = run(test::winner(listener->accept(), destroy()));
    ZASSERT(result.has_value());
    ZEXPECT(*result == 1U);
}

};  // ZEST_SUITE(async_io_stream_pipe)

}  // namespace

}  // namespace kota
