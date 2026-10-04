#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace kota::ipc {

/// The largest payload a frame may carry by default; a larger frame is
/// skipped.
constexpr std::size_t default_max_payload = 64 * 1024 * 1024;

/// How much of a skipped frame's payload is kept, for the codec to read the
/// message's id from.
constexpr std::size_t skipped_prefix_size = 4 * 1024;

/// Why a message could not be read.
struct ReadError {
    enum class Kind : std::uint8_t {
        /// The input ended, between messages or inside one.
        Closed,
        /// A frame header that cannot be read: nothing after it can be
        /// found, so the input is over.
        Malformed,
        /// A frame larger than the limit: it was skipped whole, and reading
        /// goes on after it.
        Oversized,
    };

    Kind kind = Kind::Closed;
    std::string message = {};
    /// Oversized: the size of the skipped payload, and its first bytes, up
    /// to skipped_prefix_size.
    std::size_t size = 0;
    std::string prefix = {};
};

/// `payload` in the LSP base protocol's framing: a Content-Length header, a
/// blank line, then the payload.
std::string frame(std::string_view payload);

/// Reads the frames of a byte stream handed to it in pieces of any size.
/// Header names are matched without regard to case, headers other than
/// Content-Length are ignored, and the first Content-Length counts.
class FrameParser {
public:
    /// The largest header block read, blank line included.
    constexpr static std::size_t max_header_size = 8 * 1024;

    explicit FrameParser(std::size_t max_payload = default_max_payload);

    /// What one feed() did: how many bytes of its input it took, and, when
    /// they finished a frame, its payload or why it cannot be read.
    struct Step {
        std::size_t consumed = 0;
        std::optional<std::expected<std::string, ReadError>> frame;
    };

    /// Takes bytes from the front of `input`, up to the end of the next
    /// frame; the bytes after it are left for the next call. After a
    /// Malformed frame every call returns it again and takes nothing.
    Step feed(std::string_view input);

private:
    /// How many bytes of `next`, which follows the header read so far, it
    /// takes to end the header with its blank line, if they do.
    std::optional<std::size_t> header_end(std::string_view next) const;

    enum class Phase : std::uint8_t {
        Header,
        Payload,
        Skip,
        Broken,
    };

    std::size_t max_payload;
    Phase phase = Phase::Header;
    /// The header read so far.
    std::string header;
    /// The payload read so far.
    std::string payload;
    /// The first bytes of the payload being skipped.
    std::string prefix;
    /// Why the input is Broken.
    std::string failure;
    std::size_t remaining = 0;
    std::size_t skipped_size = 0;
};

}  // namespace kota::ipc
