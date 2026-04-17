#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace kota::ipc::lsp {

/// Parsed URI value with helpers for LSP-style URI handling.
///
/// Instances are created via `parse(...)` or `from_file_path(...)` so callers
/// always receive either a valid URI or an explicit error.
class URI {
public:
    /// Parses a URI string into structured components.
    static std::expected<URI, std::string> parse(std::string_view text);

    /// Builds a `file://` URI from an absolute filesystem path.
    ///
    /// Relative paths are rejected intentionally to avoid cwd-dependent URI
    /// semantics in language-server workflows.
    static std::expected<URI, std::string> from_file_path(std::string_view path);

    /// Percent-encodes bytes that are not valid URI characters.
    static std::string percent_encode(std::string_view text, bool encode_slash = false);

    /// Decodes percent-encoded bytes (`%HH`), returning an error on invalid input.
    static std::expected<std::string, std::string> percent_decode(std::string_view text);

    /// Reconstructs the normalized URI string.
    std::string str() const;

    /// Lower-case URI scheme (e.g. `file`, `https`).
    std::string_view scheme() const noexcept;

    /// Whether the URI contains `//authority`.
    bool has_authority() const noexcept;

    /// Authority component (host[:port], possibly empty for `file:///...`).
    std::string_view authority() const noexcept;

    /// URI path component.
    std::string_view path() const noexcept;

    /// Whether a query component was present.
    bool has_query() const noexcept;

    /// Query component (without `?`), or empty when absent.
    std::string_view query() const noexcept;

    /// Whether a fragment component was present.
    bool has_fragment() const noexcept;

    /// Fragment component (without `#`), or empty when absent.
    std::string_view fragment() const noexcept;

    /// Convenience check for `file` scheme.
    bool is_file() const noexcept;

    /// Returns decoded path string for `file` URIs.
    ///
    /// For remote authorities, returns UNC-like form: `//authority/path`.
    /// Local returned paths are always absolute.
    std::expected<std::string, std::string> file_path() const;

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

    std::string text;
    Segment scheme_segment;
    Segment authority_segment;
    Segment path_segment;
    Segment query_segment;
    Segment fragment_segment;
};

}  // namespace kota::ipc::lsp
