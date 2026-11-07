#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/fs/path.h"

namespace kotatsu::ipc::lsp {

/// A URI, as LSP names documents and workspaces: its text, and where each of
/// its components lies in it. Made by from_string() or from_path(), so that
/// every URI is a valid one.
class URI {
public:
    /// Reads `text` as a URI: `scheme ":" ["//" authority] path ["?" query]
    /// ["#" fragment]`, the scheme written in lower case. Text with no scheme,
    /// or a scheme that is not a letter followed by letters, digits, `+`, `-`
    /// and `.`, fails with std::errc::invalid_argument.
    static std::expected<URI, std::error_code> from_string(std::string_view text);

    /// The `file:` URI of the absolute path `path`, as `style` spells paths,
    /// each byte a URI reserves percent-encoded: `/tmp/a b` is
    /// `file:///tmp/a%20b`, and `//server/share/x` is `file://server/share/x`,
    /// as VS Code makes it on every platform. On Windows `\` separates too, and
    /// a drive's letter is written in lower case, as VS Code keys documents:
    /// `C:\x` is `file:///c:/x`. On POSIX `\` is a character of a name like
    /// any other, `%5C`, and `C:/x` a relative path. A relative path, on
    /// Windows a drive without a root directory (`C:x`) among them, so that a
    /// URI never depends on the current directory, a share without a server
    /// or a share name, and on Windows a verbatim (`\\?\`) or a device
    /// (`\\.\`) path fail with std::errc::invalid_argument.
    static std::expected<URI, std::error_code>
        from_path(std::string_view path, fs::path::Style style = fs::path::native_style);

    /// Writes into `out` the path a `file:` URI names, decoded, with `/`
    /// separating its names: a URI with a server, other than `localhost`, as
    /// a share, `//server/share/x`, and on Windows a drive without the slash
    /// before it, `/c:/x` as `c:/x`, its letter as the URI writes it. Another
    /// scheme, a bad percent escape, a server with a separator in it, and a
    /// path that is not absolute fail with std::errc::invalid_argument.
    std::error_code to_path(buffer_ref out, fs::path::Style style = fs::path::native_style) const;

    /// Writes into `out` the bytes of `text`, each as `%HH` but letters,
    /// digits, `-`, `.`, `_` and `~`, which a URI never reserves, `:`, `@` and
    /// `!$&'()*+,;=`, which delimit the parts of a component, and `/` unless
    /// `encode_slash`.
    static void percent_encode(buffer_ref out, std::string_view text, bool encode_slash = false);

    /// Writes into `out` the bytes `text` percent-encodes; a `%` not followed
    /// by two hex digits fails with std::errc::invalid_argument.
    static std::error_code percent_decode(buffer_ref out, std::string_view text);

    /// The URI's text, its scheme in lower case.
    std::string_view text() const noexcept;

    /// In lower case: `file`, `https`.
    std::string_view scheme() const noexcept;

    /// Whether `//` and an authority follow the scheme, which may be empty:
    /// `file:///x` has one.
    bool has_authority() const noexcept;

    /// The server and port, or nothing for `file:///x`.
    std::string_view authority() const noexcept;

    /// The path component, percent-encoded as the URI writes it.
    std::string_view path() const noexcept;

    bool has_query() const noexcept;

    /// What follows `?`, or nothing when there is no query.
    std::string_view query() const noexcept;

    bool has_fragment() const noexcept;

    /// What follows `#`, or nothing when there is no fragment.
    std::string_view fragment() const noexcept;

    /// Whether the scheme is `file`.
    bool is_file() const noexcept;

private:
    struct Segment {
        constexpr static std::size_t missing = std::string::npos;
        std::size_t offset = missing;
        std::size_t size = 0;

        bool exists() const noexcept {
            return offset != missing;
        }
    };

    URI() = default;

    std::string_view view(Segment segment) const noexcept;

    std::string spelling;
    Segment scheme_segment;
    Segment authority_segment;
    Segment path_segment;
    Segment query_segment;
    Segment fragment_segment;
};

}  // namespace kotatsu::ipc::lsp
