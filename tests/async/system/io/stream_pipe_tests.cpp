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

#include "async/harness/loop_fixture.h"
#include "async/harness/os.h"
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

/// Everything `reader` reads until the end of the stream.
task<std::string, error> read_all(stream& reader) {
    std::string all;
    while(true) {
        auto piece = co_await reader.read();
        if(!piece) {
            if(piece.error() != error::end_of_file) {
                co_await fail(piece.error());
            }
            co_return all;
        }
        all += *piece;
    }
}

/// A task that finishes at once: when_any cancels what it races as soon as
/// that has started.
task<> finished() {
    co_return;
}

ZEST_SUITE(async_io_stream_pipe, test::LoopFixture) {

ZEST_CASE(read_returns_what_was_written_then_eof) {
    auto reader = pipe_holding("kotatsu-pipe", loop);
    ASSERT(reader.has_value());
    auto read_twice = [&]() -> task<std::pair<result<std::string>, result<std::string>>> {
        auto first = co_await reader->read();
        auto second = co_await reader->read();
        co_return std::pair{std::move(first), std::move(second)};
    };

    auto [result] = run(read_twice());
    ASSERT(result.has_value());
    auto& [first, second] = *result;
    ASSERT(first.has_value());
    EXPECT(*first == "kotatsu-pipe");
    ASSERT(second.has_error());
    EXPECT(second.error() == error::end_of_file);
}

// An empty buffer reads nothing without waiting; then read_some reads four
// bytes at most per call, and zero at the end.
ZEST_CASE(read_some_fills_the_buffer_and_reports_eof_as_zero) {
    auto reader = pipe_holding("abcdef", loop);
    ASSERT(reader.has_value());
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
    ASSERT(result.has_value());
    EXPECT(result->first == 0U);
    EXPECT(result->second == std::vector<std::string>{"abcd", "ef"});
}

ZEST_CASE(read_chunk_shows_the_buffer_until_consumed) {
    auto reader = pipe_holding("chunk", loop);
    ASSERT(reader.has_value());
    auto chunks = [&]() -> task<std::pair<std::string, std::string>, error> {
        auto first = co_await reader->read_chunk().or_fail();
        std::string seen(first.data(), first.size());
        auto again = co_await reader->read_chunk().or_fail();
        std::string seen_again(again.data(), again.size());
        reader->consume(again.size());
        co_return std::pair{std::move(seen), std::move(seen_again)};
    };

    auto [result] = run(chunks());
    ASSERT(result.has_value());
    EXPECT(result->first == "chunk");
    EXPECT(result->second == "chunk");
    auto [at_end] = run(reader->read_chunk());
    ASSERT(at_end.has_error());
    EXPECT(at_end.error() == error::end_of_file);
}

// What read_chunk() buffered and consume() left is what read_some() and
// read() hand out next, without waiting for the pipe.
ZEST_CASE(reads_serve_what_is_already_buffered) {
    auto reader = pipe_holding("abcdef", loop);
    ASSERT(reader.has_value());
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
    ASSERT(result.has_value());
    EXPECT(*result == std::vector<std::string>{"ab", "cd", "ef"});
}

// The writer sends its second chunk only once the reader has consumed the
// first, so read_some() after read_chunk() sees just the second.
ZEST_CASE(read_some_after_read_chunk_reads_on) {
    int fds[2] = {-1, -1};
    ASSERT(test::create_pipe(fds) == 0);
    auto reader = pipe::open(fds[0], loop);
    ASSERT(reader.has_value());
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
    ASSERT(read.has_value());
    EXPECT(read->first == "kotatsu-chunk");
    EXPECT(read->second == "kotatsu-read-some");
    ASSERT(wrote.has_value());
    EXPECT(*wrote == std::vector<ssize_t>{13, 17});
}

// A Linux pipe that holds exactly as much as the stream's buffer takes gets
// drained by one read that fills the buffer, so libuv reads again at once,
// finds the pipe empty and reports that as 0 bytes: not data, nor the end.
#ifdef __linux__
ZEST_CASE(read_after_draining_a_full_buffer_waits_for_data) {
    int fds[2] = {-1, -1};
    ASSERT(test::create_pipe(fds) == 0);
    const std::string full(64 * 1024, 'x');
    ASSERT(test::write_fd(fds[1], full.data(), full.size()) == static_cast<ssize_t>(full.size()));
    auto reader = pipe::open(fds[0], loop);
    ASSERT(reader.has_value());
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
    ASSERT(read.has_value());
    EXPECT(read->first == full.size());
    EXPECT(read->second == "tail");
    ASSERT(written.has_value());
    EXPECT(*written == 4);
}
#endif

// The reader looks at what arrives without consuming it until the buffer is
// full, then drains it all: reading stops while the buffer is full and picks
// up once it is drained, losing nothing of a MiB.
ZEST_CASE(full_buffer_holds_the_rest_back_until_drained) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
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
    EXPECT(sent_all.has_value());
    ASSERT(received.has_value());
    EXPECT(received->first == 64U * 1024);
    EXPECT(received->second == sent);
}

ZEST_CASE(second_read_while_one_is_pending_fails) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    auto second_then_write = [&]() -> task<error> {
        auto second = co_await ends->reader.read();
        auto written = co_await ends->writer.write(std::string_view("first"));
        EXPECT(written.has_value());
        co_return second.has_error() ? second.error() : error();
    };

    auto [first, second] = run(ends->reader.read(), second_then_write());
    ASSERT(first.has_value());
    EXPECT(*first == "first");
    ASSERT(second.has_value());
    EXPECT(*second == error::resource_busy_or_locked);
}

ZEST_CASE(read_from_the_write_end_fails) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    std::array<char, 8> buffer{};

    auto [buffered, direct] = run(ends->writer.read(), ends->writer.read_some(buffer));
    ASSERT(buffered.has_error());
    EXPECT(buffered.error() == error::socket_is_not_connected);
    ASSERT(direct.has_error());
    EXPECT(direct.error() == error::socket_is_not_connected);
}

ZEST_CASE(write_reaches_the_reader) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    auto send = [&]() -> task<void, error> {
        co_await ends->writer.write(std::string_view("kotatsu-write")).or_fail();
        // Closing the write end lets the reader see the end.
        ends->writer = pipe{};
    };

    auto [sent, received] = run(send(), read_all(ends->reader));
    EXPECT(sent.has_value());
    ASSERT(received.has_value());
    EXPECT(*received == "kotatsu-write");
}

// The first write is larger than the pipe holds, so it is still going out
// when the second is made.
ZEST_CASE(overlapping_writes_arrive_in_order) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    const std::string first(128 * 1024, 'a');
    const std::string second(128 * 1024, 'b');
    auto send = [&]() -> task<void, error> {
        co_await or_fail(co_await when_all(ends->writer.write(first), ends->writer.write(second)));
        ends->writer = pipe{};
    };

    auto [sent, received] = run(send(), read_all(ends->reader));
    EXPECT(sent.has_value());
    ASSERT(received.has_value());
    EXPECT(*received == first + second);
}

// libuv cannot take a write back: a cancelled write still goes out, and its
// task ends cancelled once it has. The cancel reaches the write before
// libuv reports it written, on a later loop turn.
ZEST_CASE(cancelled_write_still_delivers) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    cancellation_source source;
    auto cancel_it = [&]() -> task<> {
        source.cancel();
        co_return;
    };

    auto [written, cancelled, received] =
        run(with_token(ends->writer.write(std::string_view("kept")), source.token()),
            cancel_it(),
            ends->reader.read());
    EXPECT(written.is_cancelled());
    ASSERT(received.has_value());
    EXPECT(*received == "kept");
}

// Nothing reads the pipe, so the write is still going out, and the shutdown
// waits behind it, when their stream closes: libuv ends both, which fails
// them rather than cancelling them. Windows writes an anonymous pipe from a
// thread that the close cannot stop.
#ifndef _WIN32
ZEST_CASE(write_and_shutdown_ended_by_closing_their_stream_fail) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    const std::string large(4 * 1024 * 1024, 'x');
    auto close_it = [&]() -> task<> {
        co_await yield();
        ends->writer = pipe{};
    };

    auto [written, shut, closed] =
        run(ends->writer.write(large), ends->writer.shutdown(), close_it());
    ASSERT(written.has_error());
    EXPECT(written.error() == error::operation_aborted);
    ASSERT(shut.has_error());
    EXPECT(shut.error() == error::operation_aborted);
}
#endif

ZEST_CASE(write_of_nothing_fails) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());

    auto [result] = run(ends->writer.write({}));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::invalid_argument);
}

ZEST_CASE(write_to_the_read_end_fails) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());

    auto [result] = run(ends->reader.write(std::string_view("x")));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::broken_pipe);
}

ZEST_CASE(try_write_of_nothing_writes_nothing) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());

    auto written = ends->writer.try_write({});
    ASSERT(written.has_value());
    EXPECT(*written == 0U);
}

// Windows pipes do not report a full buffer to try_write the same way.
#ifndef _WIN32
ZEST_CASE(try_write_to_a_full_pipe_fails) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    std::string chunk(4096, 'x');

    // Linux pipes hold at most 1 MiB; no write gets anywhere near 1000 chunks.
    error refused;
    for(int i = 0; i < 1000 && !refused; ++i) {
        auto written = ends->writer.try_write(chunk);
        if(!written) {
            refused = written.error();
        }
    }
    EXPECT(refused == error::resource_temporarily_unavailable);
}
#endif

ZEST_CASE(ends_report_their_direction) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    EXPECT(ends->reader.readable());
    EXPECT(!ends->reader.writable());
    EXPECT(ends->writer.writable());
    EXPECT(!ends->writer.readable());
    EXPECT(!ends->writer.set_blocking(true));

    pipe inert;
    EXPECT(!inert.readable());
    EXPECT(!inert.writable());
}

ZEST_CASE(inert_stream_fails) {
    pipe inert;
    std::array<char, 8> buffer{};

    auto [read, read_some, chunk, written] = run(inert.read(),
                                                 inert.read_some(buffer),
                                                 inert.read_chunk(),
                                                 inert.write(std::string_view("x")));
    ASSERT(read.has_error());
    EXPECT(read.error() == error::invalid_argument);
    ASSERT(read_some.has_error());
    EXPECT(read_some.error() == error::invalid_argument);
    ASSERT(chunk.has_error());
    EXPECT(chunk.error() == error::invalid_argument);
    ASSERT(written.has_error());
    EXPECT(written.error() == error::invalid_argument);
    EXPECT(inert.stop() == error::invalid_argument);
    EXPECT(inert.set_blocking(true) == error::invalid_argument);
    auto tried = inert.try_write(std::string_view("x"));
    ASSERT(tried.has_error());
    EXPECT(tried.error() == error::invalid_argument);
}

// stop() is not sticky: the read after it reads again.
ZEST_CASE(stop_ends_a_pending_read) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    std::array<char, 8> buffer{};
    auto stop_it = [&]() -> task<error> {
        co_return ends->reader.stop();
    };

    auto [buffered, stopped] = run(ends->reader.read(), stop_it());
    ASSERT(buffered.has_error());
    EXPECT(buffered.error() == error::operation_aborted);
    ASSERT(stopped.has_value());
    EXPECT(!*stopped);
    auto [direct, again] = run(ends->reader.read_some(buffer), stop_it());
    ASSERT(direct.has_error());
    EXPECT(direct.error() == error::operation_aborted);
    auto exchange = [&]() -> task<std::string, error> {
        co_await ends->writer.write(std::string_view("after")).or_fail();
        co_return co_await ends->reader.read().or_fail();
    };
    auto [received] = run(exchange());
    ASSERT(received.has_value());
    EXPECT(*received == "after");
}

// Nothing is written, so only the cancels can end the reads.
ZEST_CASE(cancelled_reads_leave_the_pipe_usable) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
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
    ASSERT(cancelled.has_value());
    EXPECT(*cancelled == std::pair<std::size_t, std::size_t>{1, 1});
    auto [received] = run(exchange());
    ASSERT(received.has_value());
    EXPECT(*received == "after");
}

ZEST_CASE(destroying_a_stream_ends_its_read) {
    auto ends = pipe_ends(loop);
    ASSERT(ends.has_value());
    std::optional<pipe> reader = std::move(ends->reader);
    auto destroy = [&]() -> task<> {
        reader.reset();
        co_return;
    };

    auto [read, destroyed] = run(reader->read(), destroy());
    ASSERT(read.has_error());
    EXPECT(read.error() == error::operation_aborted);
}

ZEST_CASE(open_of_a_bad_descriptor_fails) {
    auto opened = pipe::open(-1, loop);
    ASSERT(opened.has_error());
    EXPECT(opened.error() == error::bad_file_descriptor);
}

ZEST_CASE(guess_handle_tells_a_pipe_from_a_file) {
    test::TempDir dir;
    int fds[2] = {-1, -1};
    ASSERT(test::create_pipe(fds) == 0);
    auto file = fs::sync::open(dir.file("file.txt"), O_CREAT | O_WRONLY, 0644);

    auto pipe_kind = guess_handle(fds[0]);
    test::close_fd(fds[0]);
    test::close_fd(fds[1]);
    ASSERT(file.has_value());
    auto file_kind = guess_handle(*file);
    EXPECT(!fs::sync::close(*file));
    EXPECT(pipe_kind == handle_type::pipe);
    EXPECT(file_kind == handle_type::file);
    EXPECT(guess_handle(-1) == handle_type::unknown);
}

ZEST_CASE(listener_accepts_what_a_client_writes) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, {.backlog = 16}, loop);
    ASSERT(listener.has_value());
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->accept().or_fail();
        co_return co_await connection.read().or_fail();
    };
    auto client = [&]() -> task<void, error> {
        auto connection = co_await pipe::connect(name).or_fail();
        co_await connection.write(std::string_view("kotatsu-pipe-connect")).or_fail();
    };

    auto [received, sent] = run(serve(), client());
    ASSERT(received.has_value());
    EXPECT(*received == "kotatsu-pipe-connect");
    EXPECT(sent.has_value());
}

ZEST_CASE(connect_to_a_missing_name_fails) {
    test::TempDir dir;

    auto [result] = run(pipe::connect(pipe_name(dir), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

// The cancel closes the connection it interrupts at once: the listener's end
// reads EOF.
ZEST_CASE(connect_can_be_cancelled) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, loop);
    ASSERT(listener.has_value());
    auto cancel_at_once = [&]() -> task<std::size_t, error> {
        auto first = co_await or_fail(co_await when_any(pipe::connect(name, loop), finished()));
        co_return first.index();
    };
    auto serve = [&]() -> task<result<std::string>, error> {
        auto connection = co_await listener->accept().or_fail();
        co_return co_await connection.read();
    };

    auto [raced, served] = run(cancel_at_once(), serve());
    ASSERT(raced.has_value());
    EXPECT(*raced == 1U);
    ASSERT(served.has_value());
    ASSERT(served->has_error());
    EXPECT(served->error() == error::end_of_file);
}

ZEST_CASE(listen_on_a_name_in_use_fails) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto first = pipe::listen(name, loop);
    ASSERT(first.has_value());

    auto taken = pipe::listen(name, loop);
    ASSERT(taken.has_error());
    EXPECT(taken.error() == error::address_already_in_use);
}

ZEST_CASE(listen_without_a_name_fails) {
    auto unnamed = pipe::listen("", loop);
    ASSERT(unnamed.has_error());
    EXPECT(unnamed.error() == error::invalid_argument);
}

ZEST_CASE(no_truncate_listens_and_connects) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    const pipe::options no_truncate{.no_truncate = true};
    auto listener = pipe::listen(name, no_truncate, loop);
    ASSERT(listener.has_value());
    auto serve = [&]() -> task<void, error> {
        co_await listener->accept().or_fail();
    };
    auto client = [&]() -> task<void, error> {
        co_await pipe::connect(name, no_truncate).or_fail();
    };

    auto [served, connected] = run(serve(), client());
    EXPECT(served.has_value());
    EXPECT(connected.has_value());
}

// A socket path longer than sun_path is cut short to fit, unless no_truncate
// has listen() fail instead. Windows never truncates pipe names.
#ifndef _WIN32
ZEST_CASE(listen_on_a_name_too_long_with_no_truncate_fails) {
    test::TempDir dir;
    auto name = dir.file(std::string(200, 'x'));

    auto truncated = pipe::listen(name, loop);
    EXPECT(truncated.has_value());
    auto refused = pipe::listen(name, {.no_truncate = true}, loop);
    ASSERT(refused.has_error());
    EXPECT(refused.error() == error::invalid_argument);
}
#endif

// The shutdown waits for the writes made with it; the listener's end reads
// them, then the end. It goes once it has, which ends the stream here too.
ZEST_CASE(shutdown_lets_the_peer_read_to_the_end) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, loop);
    ASSERT(listener.has_value());
    auto serve = [&]() -> task<std::string, error> {
        auto connection = co_await listener->accept().or_fail();
        co_return co_await read_all(connection).or_fail();
    };
    auto client = [&]() -> task<std::string, error> {
        auto connection = co_await pipe::connect(name, loop).or_fail();
        co_await or_fail(co_await when_all(connection.write(std::string_view("first")),
                                           connection.write(std::string_view("second")),
                                           connection.shutdown()));
        co_return co_await read_all(connection).or_fail();
    };

    auto [served, left] = run(serve(), client());
    ASSERT(served.has_value());
    EXPECT(*served == "firstsecond");
    ASSERT(left.has_value());
    EXPECT(left->empty());
}

// Where a pipe can be half closed, the listener's end answers after reading
// to the end, and the answer still arrives. libuv on Windows closes the whole
// pipe instead, as stream::shutdown() says.
#ifndef _WIN32
ZEST_CASE(shutdown_leaves_the_peer_free_to_answer) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, loop);
    ASSERT(listener.has_value());
    auto serve = [&]() -> task<void, error> {
        auto connection = co_await listener->accept().or_fail();
        auto request = co_await read_all(connection).or_fail();
        co_await connection.write(request + "-answered").or_fail();
    };
    auto client = [&]() -> task<std::string, error> {
        auto connection = co_await pipe::connect(name, loop).or_fail();
        co_await connection.write(std::string_view("asked")).or_fail();
        co_await connection.shutdown().or_fail();
        co_return co_await read_all(connection).or_fail();
    };

    auto [served, answer] = run(serve(), client());
    EXPECT(served.has_value());
    ASSERT(answer.has_value());
    EXPECT(*answer == "asked-answered");
}
#endif

// stop() ends a pending accept with operation_aborted, and is not sticky:
// the next accept takes the next connection.
ZEST_CASE(acceptor_stop_aborts_an_accept) {
    test::TempDir dir;
    auto name = pipe_name(dir);
    auto listener = pipe::listen(name, loop);
    ASSERT(listener.has_value());
    auto stop_it = [&]() -> task<error> {
        co_return listener->stop();
    };

    auto [pending, stopped] = run(listener->accept(), stop_it());
    ASSERT(pending.has_error());
    EXPECT(pending.error() == error::operation_aborted);
    ASSERT(stopped.has_value());
    EXPECT(!*stopped);
    auto [next, connected] = run(listener->accept(), pipe::connect(name, loop));
    EXPECT(next.has_value());
    EXPECT(connected.has_value());
}

ZEST_CASE(destroying_an_acceptor_ends_its_accept) {
    test::TempDir dir;
    auto listened = pipe::listen(pipe_name(dir), loop);
    ASSERT(listened.has_value());
    std::optional<pipe::acceptor> listener = std::move(*listened);
    auto destroy = [&]() -> task<> {
        listener.reset();
        co_return;
    };

    auto [accepted, destroyed] = run(listener->accept(), destroy());
    ASSERT(accepted.has_error());
    EXPECT(accepted.error() == error::operation_aborted);
}

};  // ZEST_SUITE(async_io_stream_pipe)

}  // namespace

}  // namespace kota
