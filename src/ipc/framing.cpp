#include "kota/ipc/framing.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace kota::ipc {

namespace {

std::string_view trim(std::string_view value) {
    auto start = value.find_first_not_of(" \t");
    if(start == std::string_view::npos) {
        return {};
    }
    auto end = value.find_last_not_of(" \t");
    return value.substr(start, end - start + 1);
}

bool equals_ignoring_case(std::string_view lhs, std::string_view rhs) {
    return std::ranges::equal(lhs, rhs, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    });
}

/// The payload size the first Content-Length of `header`, a header block up
/// to its blank line, names, or why there is none.
std::expected<std::size_t, std::string> content_length(std::string_view header) {
    while(!header.empty()) {
        auto end = header.find("\r\n");
        auto line = header.substr(0, end);
        header.remove_prefix(end + 2);

        auto colon = line.find(':');
        if(colon == std::string_view::npos ||
           !equals_ignoring_case(trim(line.substr(0, colon)), "Content-Length")) {
            continue;
        }

        auto value = trim(line.substr(colon + 1));
        std::size_t length = 0;
        auto [last, error] = std::from_chars(value.data(), value.data() + value.size(), length);
        if(value.empty() || error != std::errc() || last != value.data() + value.size()) {
            return std::unexpected(std::format("invalid Content-Length: {}", value));
        }
        return length;
    }
    return std::unexpected("missing Content-Length");
}

}  // namespace

std::string frame(std::string_view payload) {
    auto header = std::format("Content-Length: {}\r\n\r\n", payload.size());
    std::string framed;
    framed.reserve(header.size() + payload.size());
    framed.append(header);
    framed.append(payload);
    return framed;
}

FrameParser::FrameParser(std::size_t max_payload) : max_payload(max_payload) {}

FrameParser::Step FrameParser::feed(std::string_view input) {
    std::size_t consumed = 0;
    auto malformed = [&] {
        return Step{
            .consumed = consumed,
            .frame =
                std::unexpected(ReadError{.kind = ReadError::Kind::Malformed, .message = header}),
        };
    };
    auto break_with = [&](std::string reason) {
        phase = Phase::Broken;
        header = std::move(reason);
        return malformed();
    };

    while(true) {
        switch(phase) {
            case Phase::Broken: return malformed();

            case Phase::Header: {
                const auto old_size = header.size();
                // The blank line may begin in what an earlier call read.
                const auto scan_from = old_size < 3 ? 0 : old_size - 3;
                header.append(input.substr(consumed));
                const auto marker = header.find("\r\n\r\n", scan_from);
                if(marker == std::string::npos) {
                    consumed = input.size();
                    if(header.size() > max_header_size) {
                        return break_with(std::format("header exceeds {} bytes", max_header_size));
                    }
                    return {.consumed = consumed, .frame = std::nullopt};
                }

                const auto header_end = marker + 4;
                consumed += header_end - old_size;
                if(header_end > max_header_size) {
                    return break_with(std::format("header exceeds {} bytes", max_header_size));
                }
                header.resize(header_end);
                auto length = content_length(header);
                if(!length) {
                    return break_with(std::move(length).error());
                }
                header.clear();
                payload.clear();
                remaining = *length;
                if(*length > max_payload) {
                    phase = Phase::Skip;
                    skipped_size = *length;
                } else {
                    phase = Phase::Payload;
                    payload.reserve(*length);
                }
                continue;
            }

            case Phase::Payload: {
                const auto take = std::min(remaining, input.size() - consumed);
                payload.append(input.substr(consumed, take));
                consumed += take;
                remaining -= take;
                if(remaining != 0) {
                    return {.consumed = consumed, .frame = std::nullopt};
                }
                phase = Phase::Header;
                return {.consumed = consumed, .frame = std::move(payload)};
            }

            case Phase::Skip: {
                const auto take = std::min(remaining, input.size() - consumed);
                const auto keep = std::min(take, skipped_prefix_size - payload.size());
                payload.append(input.substr(consumed, keep));
                consumed += take;
                remaining -= take;
                if(remaining != 0) {
                    return {.consumed = consumed, .frame = std::nullopt};
                }
                phase = Phase::Header;
                return {
                    .consumed = consumed,
                    .frame = std::unexpected(ReadError{
                        .kind = ReadError::Kind::Oversized,
                        .message =
                            std::format("a message of {} bytes exceeds the limit of {} bytes",
                                        skipped_size,
                                        max_payload),
                        .size = skipped_size,
                        .prefix = std::move(payload),
                    }),
                };
            }
        }
    }
}

}  // namespace kota::ipc
