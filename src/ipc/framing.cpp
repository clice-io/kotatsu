#include "kota/ipc/framing.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

#include "kota/support/naming.h"

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

/// Whether lhs and rhs are the same ASCII text but for case, as header names
/// are compared, whatever the locale.
bool equals_ignoring_case(std::string_view lhs, std::string_view rhs) {
    return std::ranges::equal(lhs, rhs, [](char a, char b) {
        return naming::to_lower(a) == naming::to_lower(b);
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

std::string frame_header(std::size_t size) {
    return std::format("Content-Length: {}\r\n\r\n", size);
}

FrameParser::FrameParser(std::size_t max_payload) : max_payload(max_payload) {}

std::optional<std::size_t> FrameParser::header_end(std::string_view next) const {
    constexpr std::string_view blank_line = "\r\n\r\n";
    // A blank line that begins in what earlier calls read, earliest first.
    for(std::size_t before = std::min<std::size_t>(3, header.size()); before > 0; --before) {
        if(header.ends_with(blank_line.substr(0, before)) &&
           next.starts_with(blank_line.substr(before))) {
            return blank_line.size() - before;
        }
    }
    if(auto at = next.find(blank_line); at != std::string_view::npos) {
        return at + blank_line.size();
    }
    return std::nullopt;
}

FrameParser::Step FrameParser::feed(std::string_view input) {
    std::size_t consumed = 0;
    auto malformed = [&] {
        return Step{
            .consumed = consumed,
            .frame =
                std::unexpected(ReadError{.kind = ReadError::Kind::Malformed, .message = failure}),
        };
    };
    auto break_with = [&](std::string reason) {
        phase = Phase::Broken;
        failure = std::move(reason);
        return malformed();
    };

    while(true) {
        switch(phase) {
            case Phase::Broken: return malformed();

            case Phase::Header: {
                // Only the header's own bytes are copied, and no more than one
                // past the limit, which tells a header that is too long:
                // copying the whole input would copy the frames after this
                // one again for each of them.
                const auto window = input.substr(consumed, max_header_size + 1 - header.size());
                const auto end = header_end(window);
                if(!end) {
                    header.append(window);
                    consumed += window.size();
                    if(header.size() > max_header_size) {
                        return break_with(std::format("header exceeds {} bytes", max_header_size));
                    }
                    return {.consumed = consumed, .frame = std::nullopt};
                }

                header.append(window.substr(0, *end));
                consumed += *end;
                if(header.size() > max_header_size) {
                    return break_with(std::format("header exceeds {} bytes", max_header_size));
                }
                auto length = content_length(header);
                if(!length) {
                    return break_with(std::move(length).error());
                }
                header.clear();
                payload.clear();
                prefix.clear();
                remaining = *length;
                if(*length > max_payload) {
                    phase = Phase::Skip;
                    skipped_size = *length;
                } else {
                    phase = Phase::Payload;
                    payload.reserve(*length + payload_padding);
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
                const auto keep = std::min(take, skipped_prefix_size - prefix.size());
                prefix.append(input.substr(consumed, keep));
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
                        .prefix = std::move(prefix),
                    }),
                };
            }
        }
    }
}

}  // namespace kota::ipc
