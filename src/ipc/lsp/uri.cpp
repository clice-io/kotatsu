#include "kota/ipc/lsp/uri.h"

#include <algorithm>
#include <ranges>

namespace kota::ipc::lsp {

namespace {

constexpr bool is_ascii_alpha(unsigned char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

constexpr bool is_ascii_digit(unsigned char value) noexcept {
    return value >= '0' && value <= '9';
}

constexpr bool is_ascii_alnum(unsigned char value) noexcept {
    return is_ascii_alpha(value) || is_ascii_digit(value);
}

constexpr char to_ascii_lower(unsigned char value) noexcept {
    if(value >= 'A' && value <= 'Z') {
        return static_cast<char>(value + ('a' - 'A'));
    }
    return static_cast<char>(value);
}

constexpr bool is_ascii_iequal(std::string_view left, std::string_view right) noexcept {
    if(left.size() != right.size()) [[unlikely]] {
        return false;
    }

    for(std::size_t index = 0; index < left.size(); ++index) {
        const auto lhs = to_ascii_lower(static_cast<unsigned char>(left[index]));
        const auto rhs = to_ascii_lower(static_cast<unsigned char>(right[index]));
        if(lhs != rhs) [[unlikely]] {
            return false;
        }
    }

    return true;
}

constexpr bool is_valid_scheme(std::string_view scheme) noexcept {
    if(scheme.empty()) {
        return false;
    }

    if(!is_ascii_alpha(static_cast<unsigned char>(scheme.front()))) {
        return false;
    }

    const auto tail = scheme | std::views::drop(1);
    return std::ranges::all_of(tail, [](char value) {
        const unsigned char ch = static_cast<unsigned char>(value);
        return is_ascii_alnum(ch) || ch == '+' || ch == '-' || ch == '.';
    });
}

constexpr void lower_ascii_in_place(std::string& text) {
    std::ranges::transform(text, text.begin(), [](unsigned char value) {
        return to_ascii_lower(value);
    });
}

constexpr bool should_encode(unsigned char value, bool encode_slash) noexcept {
    const bool is_unreserved =
        is_ascii_alnum(value) || value == '-' || value == '.' || value == '_' || value == '~';
    if(is_unreserved) {
        return false;
    }

    if(!encode_slash && value == '/') {
        return false;
    }

    // Keep generic reserved chars unescaped so the caller can encode per-component
    // while still preserving URI structural semantics.
    switch(value) {
        case ':':
        case '@':
        case '!':
        case '$':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '*':
        case '+':
        case ',':
        case ';':
        case '=': return false;
        default: return true;
    }
}

constexpr int hex_value(char value) noexcept {
    if(value >= '0' && value <= '9') {
        return value - '0';
    }
    if(value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if(value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

constexpr bool is_windows_drive_prefix(std::string_view path) noexcept {
    return path.size() >= 2 && is_ascii_alpha(static_cast<unsigned char>(path[0])) &&
           path[1] == ':';
}

constexpr bool is_windows_drive_absolute(std::string_view path) noexcept {
    return path.size() >= 3 && is_windows_drive_prefix(path) && path[2] == '/';
}

constexpr bool is_absolute_fs_path(std::string_view path) noexcept {
    return path.starts_with('/') || path.starts_with("//") || is_windows_drive_absolute(path);
}

std::string percent_encode_with_policy(std::string_view input, bool encode_slash) {
    constexpr static char hex[] = "0123456789ABCDEF";

    std::string output;
    output.reserve(input.size());
    for(unsigned char value: input) {
        if(should_encode(value, encode_slash)) {
            output.push_back('%');
            output.push_back(hex[(value >> 4) & 0x0F]);
            output.push_back(hex[value & 0x0F]);
        } else {
            output.push_back(static_cast<char>(value));
        }
    }
    return output;
}

}  // namespace

std::string_view URI::view(Segment segment) const noexcept {
    if(!segment.exists()) {
        return {};
    }
    return std::string_view(text).substr(segment.offset, segment.size);
}

std::expected<URI, std::string> URI::parse(std::string_view input) {
    if(input.empty()) [[unlikely]] {
        return std::unexpected("uri is empty");
    }

    const std::size_t scheme_end = input.find(':');
    if(scheme_end == std::string_view::npos) [[unlikely]] {
        return std::unexpected("uri is missing scheme delimiter ':'");
    }

    // "/?#" are the earliest tokens that can start the hierarchical part.
    const std::size_t first_delimiter = input.find_first_of("/?#");
    if(first_delimiter != std::string_view::npos && scheme_end > first_delimiter) [[unlikely]] {
        return std::unexpected("uri is missing scheme");
    }

    const std::string_view raw_scheme = input.substr(0, scheme_end);
    if(!is_valid_scheme(raw_scheme)) [[unlikely]] {
        return std::unexpected("uri scheme is invalid");
    }

    std::string scheme_text(raw_scheme);
    lower_ascii_in_place(scheme_text);

    // Split URI as: scheme ":" hier-part [ "?" query ] [ "#" fragment ].
    std::string_view remainder = input.substr(scheme_end + 1);

    bool has_fragment = false;
    std::string_view fragment_text;
    std::string_view without_fragment = remainder;
    const std::size_t fragment_pos = remainder.find('#');
    if(fragment_pos != std::string_view::npos) [[unlikely]] {
        has_fragment = true;
        fragment_text = remainder.substr(fragment_pos + 1);
        without_fragment = remainder.substr(0, fragment_pos);
    }

    bool has_query = false;
    std::string_view query_text;
    std::string_view hierarchical = without_fragment;
    // Query must be parsed after cutting fragment to avoid consuming '?' in fragments.
    const std::size_t query_pos = without_fragment.find('?');
    if(query_pos != std::string_view::npos) [[unlikely]] {
        has_query = true;
        query_text = without_fragment.substr(query_pos + 1);
        hierarchical = without_fragment.substr(0, query_pos);
    }

    bool has_authority = false;
    std::string_view authority_text;
    std::string_view path_text;
    if(hierarchical.starts_with("//")) [[likely]] {
        // Authority form: "//" authority [ "/" path-abempty ].
        has_authority = true;
        const std::string_view authority_and_path = hierarchical.substr(2);
        const std::size_t path_pos = authority_and_path.find('/');
        if(path_pos == std::string_view::npos) [[unlikely]] {
            authority_text = authority_and_path;
            path_text = {};
        } else {
            authority_text = authority_and_path.substr(0, path_pos);
            path_text = authority_and_path.substr(path_pos);
        }
    } else {
        path_text = hierarchical;
    }

    URI uri{};
    uri.text.reserve(input.size());

    auto append_segment = [&uri](std::string_view part, Segment& segment) {
        // Store all URI data in one backing string; segments keep offset/length views.
        segment.offset = uri.text.size();
        segment.size = part.size();
        uri.text.append(part);
    };

    append_segment(scheme_text, uri.scheme_segment);
    uri.text.push_back(':');

    if(has_authority) {
        uri.text.append("//");
        append_segment(authority_text, uri.authority_segment);
    }

    append_segment(path_text, uri.path_segment);

    if(has_query) {
        uri.text.push_back('?');
        append_segment(query_text, uri.query_segment);
    }

    if(has_fragment) {
        uri.text.push_back('#');
        append_segment(fragment_text, uri.fragment_segment);
    }

    return uri;
}

std::expected<URI, std::string> URI::from_file_path(std::string_view path) {
    if(path.empty()) [[unlikely]] {
        return std::unexpected("path is empty");
    }

    std::string normalized_storage;
    std::string_view normalized_path = path;
    if(path.find('\\') != std::string_view::npos) [[unlikely]] {
        normalized_storage.assign(path);
        std::ranges::replace(normalized_storage, '\\', '/');
        normalized_path = normalized_storage;
    }

    // "C:foo" is drive-relative on Windows; only "C:/foo" is absolute.
    if(is_windows_drive_prefix(normalized_path) && !is_windows_drive_absolute(normalized_path))
        [[unlikely]] {
        return std::unexpected("windows drive path must be absolute");
    }
    // Reject relative paths by design so file URIs stay deterministic and
    // independent from process cwd.
    if(!is_absolute_fs_path(normalized_path)) [[unlikely]] {
        return std::unexpected("path must be absolute");
    }

    URI uri{};

    auto append_segment = [&uri](std::string_view part, Segment& segment) {
        segment.offset = uri.text.size();
        segment.size = part.size();
        uri.text.append(part);
    };

    auto encode_authority_host = [](std::string_view host) {
        // Preserve IPv6 literal brackets in authority, e.g. "[::1]".
        if(host.size() >= 2 && host.front() == '[' && host.back() == ']') {
            std::string encoded;
            encoded.reserve(host.size());
            encoded.push_back('[');
            encoded += percent_encode_with_policy(host.substr(1, host.size() - 2), true);
            encoded.push_back(']');
            return encoded;
        }
        return percent_encode_with_policy(host, true);
    };

    append_segment("file", uri.scheme_segment);
    uri.text.push_back(':');
    uri.text.append("//");

    // UNC path: //server/share/... -> file://server/share/...
    if(normalized_path.starts_with("//")) [[unlikely]] {
        const std::string_view unc = normalized_path;
        const std::string_view host_and_rest = unc.substr(2);
        const std::size_t first_slash = host_and_rest.find('/');
        if(first_slash == std::string_view::npos || first_slash == 0) [[unlikely]] {
            return std::unexpected("unc path must include server and share");
        }
        const std::size_t share_start = first_slash + 1;
        if(share_start >= host_and_rest.size() || host_and_rest[share_start] == '/') [[unlikely]] {
            return std::unexpected("unc path must include non-empty share");
        }

        const std::string_view host = host_and_rest.substr(0, first_slash);
        const std::string_view share_and_path = host_and_rest.substr(first_slash);

        const std::string encoded_authority = encode_authority_host(host);
        const std::string encoded_path = percent_encode(share_and_path, false);

        append_segment(encoded_authority, uri.authority_segment);
        append_segment(encoded_path, uri.path_segment);
        return uri;
    }

    // Windows drive path "C:/..." is represented as "/C:/..." in file URIs.
    if(is_windows_drive_absolute(normalized_path)) [[unlikely]] {
        if(normalized_storage.empty()) {
            normalized_storage.assign(normalized_path);
        }
        normalized_storage.insert(normalized_storage.begin(), '/');
        normalized_path = normalized_storage;
    }
    const std::string encoded_path = percent_encode(normalized_path, false);

    // Empty authority is still present in "file:///<path>" form.
    uri.authority_segment.offset = uri.text.size();
    uri.authority_segment.size = 0;
    append_segment(encoded_path, uri.path_segment);

    return uri;
}

std::string URI::percent_encode(std::string_view input, bool encode_slash) {
    return percent_encode_with_policy(input, encode_slash);
}

std::expected<std::string, std::string> URI::percent_decode(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    for(std::size_t index = 0; index < input.size(); ++index) {
        if(input[index] != '%') [[likely]] {
            output.push_back(input[index]);
            continue;
        }

        // Percent escape is exactly "%HH", so we need two chars after '%'.
        if(index + 2 >= input.size()) [[unlikely]] {
            return std::unexpected("invalid percent-encoding: truncated escape");
        }

        const int high = hex_value(input[index + 1]);
        const int low = hex_value(input[index + 2]);
        if(high < 0 || low < 0) [[unlikely]] {
            return std::unexpected("invalid percent-encoding: non-hex digit");
        }

        output.push_back(static_cast<char>((high << 4) | low));
        // Skip the two hex digits we just consumed.
        index += 2;
    }

    return output;
}

std::string URI::str() const {
    return text;
}

std::string_view URI::scheme() const noexcept {
    return view(scheme_segment);
}

bool URI::has_authority() const noexcept {
    return authority_segment.exists();
}

std::string_view URI::authority() const noexcept {
    return view(authority_segment);
}

std::string_view URI::path() const noexcept {
    return view(path_segment);
}

bool URI::has_query() const noexcept {
    return query_segment.exists();
}

std::string_view URI::query() const noexcept {
    return view(query_segment);
}

bool URI::has_fragment() const noexcept {
    return fragment_segment.exists();
}

std::string_view URI::fragment() const noexcept {
    return view(fragment_segment);
}

bool URI::is_file() const noexcept {
    return scheme() == "file";
}

std::expected<std::string, std::string> URI::file_path() const {
    if(!is_file()) [[unlikely]] {
        return std::unexpected("uri scheme is not file");
    }

    auto decoded_path = percent_decode(path());
    if(!decoded_path) [[unlikely]] {
        return std::unexpected(decoded_path.error());
    }

    const auto authority_view = authority();
    if(has_authority() && !authority_view.empty() && !is_ascii_iequal(authority_view, "localhost"))
        [[unlikely]] {
        auto decoded_authority = percent_decode(authority_view);
        if(!decoded_authority) [[unlikely]] {
            return std::unexpected(decoded_authority.error());
        }

        // A decoded authority must remain a single host token. If it contains
        // '/' or '\\', UNC host/path boundaries become ambiguous.
        if(std::ranges::any_of(*decoded_authority, [](char value) {
               return value == '/' || value == '\\';
           })) [[unlikely]] {
            return std::unexpected("file uri authority contains path separator");
        }

        // Build UNC-style "//authority/path" with a single allocation.
        std::string unc_path;
        unc_path.reserve(2 + decoded_authority->size() + decoded_path->size());
        unc_path += "//";
        unc_path += *decoded_authority;
        unc_path += *decoded_path;
        return unc_path;
    }

    // Local file URI path should be absolute.
    if(decoded_path->empty() || (*decoded_path)[0] != '/') [[unlikely]] {
        return std::unexpected("file uri local path must be absolute");
    }

#if defined(_WIN32)
    // On Windows, local file URIs use "/C:/..." form.
    if(decoded_path->size() >= 3 && (*decoded_path)[0] == '/' &&
       is_ascii_alpha(static_cast<unsigned char>((*decoded_path)[1])) &&
       (*decoded_path)[2] == ':') {
        return decoded_path->substr(1);
    }
#endif

    return *decoded_path;
}

}  // namespace kota::ipc::lsp
