#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "ipc/harness/frames.h"
#include "kota/ipc/framing.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota::ipc {

namespace {

/// What the frames of `input` read as: each payload, or the kind of error
/// that ended or skipped one.
struct Read {
    std::vector<std::string> frames;
    std::vector<ReadError::Kind> errors;
};

/// Feeds `pieces` to `parser` in order, each until it is used up.
Read read_pieces(FrameParser& parser, const std::vector<std::string_view>& pieces) {
    Read read;
    for(auto piece: pieces) {
        while(true) {
            auto step = parser.feed(piece);
            piece.remove_prefix(step.consumed);
            if(!step.frame) {
                break;
            }
            if(*step.frame) {
                read.frames.push_back(std::move(**step.frame));
            } else {
                read.errors.push_back(step.frame->error().kind);
                if(step.frame->error().kind == ReadError::Kind::Malformed) {
                    return read;
                }
            }
        }
    }
    return read;
}

/// The first frame of `input`, fed whole: its payload or its error.
std::expected<std::string, ReadError> first_frame(std::string_view input,
                                                  std::size_t max_payload = default_max_payload) {
    FrameParser parser(max_payload);
    auto step = parser.feed(input);
    if(!step.frame) {
        return std::unexpected(ReadError{.message = "no frame"});
    }
    return std::move(*step.frame);
}

/// Several frames, with headers written in the ways a sender may write them.
constexpr std::string_view stream =
    "Content-Length: 5\r\n\r\nhello"
    "content-length:0\r\n\r\n"
    "CONTENT-LENGTH: 2\r\n\r\nup"
    "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\nContent-Length:  3 \r\n\r\nabc"
    "Content-Length: 6\r\n\r\nx\r\n\r\ny";

// The last payload holds a blank line of its own.
const std::vector<std::string> stream_frames{"hello", "", "up", "abc", "x\r\n\r\ny"};

ZEST_SUITE(ipc_framing) {

ZEST_CASE(frame_header_names_the_content_length) {
    ZEXPECT(frame_header(5) == "Content-Length: 5\r\n\r\n");
    ZEXPECT(frame_header(0) == "Content-Length: 0\r\n\r\n");
}

ZEST_CASE(feed_stops_at_the_end_of_a_frame) {
    FrameParser parser;
    const std::string input = "Content-Length: 1\r\n\r\naContent-Length: 1\r\n\r\nb";
    auto first = parser.feed(input);
    ZEXPECT(first.consumed == input.size() / 2);
    ZASSERT(first.frame.has_value());
    ZEXPECT(*first.frame == "a");
    auto second = parser.feed(std::string_view(input).substr(first.consumed));
    ZEXPECT(second.consumed == input.size() / 2);
    ZASSERT(second.frame.has_value());
    ZEXPECT(*second.frame == "b");
}

ZEST_CASE(payload_leaves_room_past_its_end) {
    // A codec parsing the payload in place writes its parser's padding
    // there without moving it.
    auto payload = first_frame("Content-Length: 5\r\n\r\nhello");
    ZASSERT(payload);
    ZEXPECT(*payload == "hello");
    ZEXPECT(payload->capacity() >= payload->size() + payload_padding);
}

ZEST_CASE(frames_read_whole_from_one_piece) {
    FrameParser parser;
    auto read = read_pieces(parser, {stream});
    ZEXPECT(read.frames == stream_frames);
    ZEXPECT(read.errors.empty());
}

ZEST_CASE(frames_read_the_same_split_anywhere) {
    for(std::size_t at = 0; at <= stream.size(); ++at) {
        ZEST_CONTEXT("split at {}", at);
        FrameParser parser;
        auto read = read_pieces(parser, {stream.substr(0, at), stream.substr(at)});
        ZEXPECT(read.frames == stream_frames);
    }
}

ZEST_CASE(frames_read_the_same_in_random_pieces) {
    constexpr std::uint32_t seed = 20260927;
    std::mt19937 random(seed);
    for(int round = 0; round < 200; ++round) {
        std::vector<std::string_view> pieces;
        std::size_t at = 0;
        while(at < stream.size()) {
            auto size = std::uniform_int_distribution<std::size_t>(0, 8)(random);
            pieces.push_back(stream.substr(at, size));
            at += size;
        }
        ZEST_CONTEXT("seed {}, round {}", seed, round);
        FrameParser parser;
        auto read = read_pieces(parser, pieces);
        ZEXPECT(read.frames == stream_frames);
    }
}

ZEST_CASE(missing_content_length_fails) {
    auto read = first_frame("Content-Type: text/plain\r\n\r\nhello");
    ZASSERT(!read.has_value());
    ZEXPECT(read.error().kind == ReadError::Kind::Malformed);
    ZEXPECT(read.error().message == "missing Content-Length");
}

ZEST_CASE(unreadable_content_length_fails) {
    for(std::string_view length: {"", "5x", "-1", "+5", "0x10", "99999999999999999999999"}) {
        ZEST_CONTEXT("Content-Length: {}", length);
        auto read = first_frame(std::format("Content-Length: {}\r\n\r\nhello", length));
        ZASSERT(!read.has_value());
        ZEXPECT(read.error().kind == ReadError::Kind::Malformed);
        ZEXPECT(zest::starts_with(read.error().message, "invalid Content-Length"));
    }
}

ZEST_CASE(header_past_the_limit_fails) {
    std::string unended(FrameParser::max_header_size + 1, 'A');
    auto read = first_frame(unended);
    ZASSERT(!read.has_value());
    ZEXPECT(read.error().kind == ReadError::Kind::Malformed);

    std::string padded = "Content-Length: 5\r\nX-Padding: ";
    padded.append(FrameParser::max_header_size, 'A');
    padded += "\r\n\r\nhello";
    auto long_block = first_frame(padded);
    ZASSERT(!long_block.has_value());
    ZEXPECT(long_block.error().kind == ReadError::Kind::Malformed);
}

// A header block of exactly the limit is read; the limit applies to the
// header alone, however much input follows it.
ZEST_CASE(header_at_the_limit_is_read) {
    std::string header = "Content-Length: 2\r\nX-Padding: ";
    header.append(FrameParser::max_header_size - header.size() - 4, 'A');
    header += "\r\n\r\n";
    ZASSERT(header.size() == FrameParser::max_header_size);
    FrameParser parser;
    auto read = read_pieces(parser, {header + "ok" + std::string(100000, 'B')});
    ZASSERT(!read.frames.empty());
    ZEXPECT(read.frames[0] == "ok");
}

// Of two Content-Length headers, the first counts.
ZEST_CASE(first_of_duplicate_content_lengths_counts) {
    ZEXPECT(first_frame("Content-Length: 2\r\nContent-Length: 5\r\n\r\nokay!") == "ok");
}

// Nothing after a header that cannot be read can be found: the parser
// keeps reporting it and takes nothing more.
ZEST_CASE(malformed_frame_stays_malformed) {
    FrameParser parser;
    auto first = parser.feed("Content-Length: x\r\n\r\n");
    ZASSERT(first.frame.has_value());
    ZASSERT(!first.frame->has_value());
    auto again = parser.feed("Content-Length: 1\r\n\r\na");
    ZEXPECT(again.consumed == 0U);
    ZASSERT(again.frame.has_value());
    ZASSERT(!again.frame->has_value());
    ZEXPECT(again.frame->error().kind == ReadError::Kind::Malformed);
}

// A header ended by bare line feeds has no blank line: nothing is read.
ZEST_CASE(line_feed_only_header_does_not_end) {
    FrameParser parser;
    const std::string input = "Content-Length: 5\n\nhello";
    auto step = parser.feed(input);
    ZEXPECT(step.consumed == input.size());
    ZEXPECT(!step.frame.has_value());
}

ZEST_CASE(oversized_frame_is_skipped_and_reading_goes_on) {
    FrameParser parser(4);
    auto read =
        read_pieces(parser, {"Content-Length: 10\r\n\r\n0123456789Content-Length: 2\r\n\r\nok"});
    ZEXPECT(read.errors == std::vector{ReadError::Kind::Oversized});
    ZEXPECT(read.frames == std::vector<std::string>{"ok"});
}

ZEST_CASE(oversized_frame_keeps_its_size_and_first_bytes) {
    std::string payload(2 * skipped_prefix_size, 'x');
    payload[0] = '{';
    auto read = first_frame(test::framed(payload), 16);
    ZASSERT(!read.has_value());
    ZEXPECT(read.error().kind == ReadError::Kind::Oversized);
    ZEXPECT(read.error().size == payload.size());
    ZEXPECT(read.error().prefix == payload.substr(0, skipped_prefix_size));
    ZEXPECT(zest::contains(read.error().message, std::to_string(payload.size())));
}

ZEST_CASE(payload_at_the_limit_is_read) {
    ZEXPECT(first_frame("Content-Length: 4\r\n\r\nabcd", 4) == "abcd");
}

};  // ZEST_SUITE(ipc_framing)

}  // namespace

}  // namespace kota::ipc
