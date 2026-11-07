#include "kotatsu/ipc/lsp/uri.h"

#include <algorithm>
#include <iterator>
#include <optional>

#include "kotatsu/support/string_ref.h"

namespace kotatsu::ipc::lsp {

namespace {

constexpr bool is_ascii_alpha(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

constexpr bool is_ascii_alnum(char c) noexcept {
    return is_ascii_alpha(c) || (c >= '0' && c <= '9');
}

constexpr char to_ascii_lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

constexpr bool is_valid_scheme(std::string_view scheme) noexcept {
    return !scheme.empty() && is_ascii_alpha(scheme.front()) &&
           std::ranges::all_of(scheme.substr(1), [](char c) {
               return is_ascii_alnum(c) || c == '+' || c == '-' || c == '.';
           });
}

constexpr bool should_encode(char c, bool encode_slash) noexcept {
    if(is_ascii_alnum(c)) {
        return false;
    }
    switch(c) {
        // Unreserved.
        case '-':
        case '.':
        case '_':
        case '~':
        // Reserved, but left to the caller to delimit a component's parts with.
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
        case '/': return encode_slash;
        default: return true;
    }
}

constexpr int hex_value(char c) noexcept {
    if(c >= '0' && c <= '9') {
        return c - '0';
    }
    if(c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if(c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::error_code invalid() {
    return std::make_error_code(std::errc::invalid_argument);
}

/// Appends `text` to `out`, percent-encoded.
void encode_into(buffer_ref out, std::string_view text, bool encode_slash) {
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::size_t size = 0;
    for(char c: text) {
        size += should_encode(c, encode_slash) ? 3 : 1;
    }
    auto at = out.size();
    auto chars = out.resize(at + size).subspan(at);
    std::size_t i = 0;
    for(char c: text) {
        if(should_encode(c, encode_slash)) {
            auto byte = static_cast<unsigned char>(c);
            chars[i++] = '%';
            chars[i++] = hex[byte >> 4];
            chars[i++] = hex[byte & 0x0F];
        } else {
            chars[i++] = c;
        }
    }
}

/// Appends to `out` the bytes `text` percent-encodes.
std::error_code decode_into(buffer_ref out, std::string_view text) {
    auto at = out.size();
    auto chars = out.resize(at + text.size()).subspan(at);
    std::size_t written = 0;
    for(std::size_t i = 0; i < text.size(); ++i) {
        if(text[i] != '%') {
            chars[written++] = text[i];
            continue;
        }
        if(text.size() - i < 3) {
            return invalid();
        }
        const int high = hex_value(text[i + 1]);
        const int low = hex_value(text[i + 2]);
        if(high < 0 || low < 0) {
            return invalid();
        }
        chars[written++] = static_cast<char>((high << 4) | low);
        i += 2;
    }
    out.resize(at + written);
    return {};
}

}  // namespace

std::string_view URI::view(Segment segment) const noexcept {
    if(!segment.exists()) {
        return {};
    }
    return std::string_view(spelling).substr(segment.offset, segment.size);
}

std::expected<URI, std::error_code> URI::from_string(std::string_view text) {
    const std::size_t scheme_end = text.find(':');
    // A scheme ends before anything that may start the rest.
    if(scheme_end == std::string_view::npos || scheme_end > text.find_first_of("/?#") ||
       !is_valid_scheme(text.substr(0, scheme_end))) {
        return std::unexpected(invalid());
    }

    // scheme ":" hier-part [ "?" query ] [ "#" fragment ]; the query is cut
    // after the fragment, which may hold a '?'.
    std::string_view rest = text.substr(scheme_end + 1);
    std::optional<std::string_view> fragment;
    if(auto at = rest.find('#'); at != std::string_view::npos) {
        fragment = rest.substr(at + 1);
        rest = rest.substr(0, at);
    }
    std::optional<std::string_view> query;
    if(auto at = rest.find('?'); at != std::string_view::npos) {
        query = rest.substr(at + 1);
        rest = rest.substr(0, at);
    }
    // "//" authority path-abempty, or a path alone.
    std::optional<std::string_view> authority;
    if(rest.starts_with("//")) {
        rest.remove_prefix(2);
        const std::size_t path_start = std::min(rest.find('/'), rest.size());
        authority = rest.substr(0, path_start);
        rest = rest.substr(path_start);
    }

    URI uri;
    uri.spelling.reserve(text.size());
    uri.scheme_segment = {.offset = 0, .size = scheme_end};
    std::ranges::transform(text.substr(0, scheme_end),
                           std::back_inserter(uri.spelling),
                           to_ascii_lower);
    uri.spelling.push_back(':');
    auto append = [&uri](std::string_view part, Segment& segment) {
        segment = {.offset = uri.spelling.size(), .size = part.size()};
        uri.spelling.append(part);
    };
    if(authority) {
        uri.spelling.append("//");
        append(*authority, uri.authority_segment);
    }
    append(rest, uri.path_segment);
    if(query) {
        uri.spelling.push_back('?');
        append(*query, uri.query_segment);
    }
    if(fragment) {
        uri.spelling.push_back('#');
        append(*fragment, uri.fragment_segment);
    }
    return uri;
}

std::expected<URI, std::error_code> URI::from_path(std::string_view path, fs::path::Style style) {
    using fs::path::Prefix;
    const auto root = fs::path::root(path, style);
    // A verbatim or a device path names what no file URI does.
    if(root.prefix != Prefix::None && root.prefix != Prefix::Drive && root.prefix != Prefix::UNC) {
        return std::unexpected(invalid());
    }
    // A drive without a root directory, `C:x`, depends on the current one.
    if(root.prefix == Prefix::Drive && root.directory.empty()) {
        return std::unexpected(invalid());
    }
    std::string slashed(path);
    fs::path::to_slash(slashed, style);
    fs::path::lowercase_drive(slashed, style);

    URI uri;
    uri.spelling = "file://";
    uri.scheme_segment = {.offset = 0, .size = 4};
    // Appends what `write` writes as `segment`.
    auto append = [&uri](Segment& segment, auto write) {
        const std::size_t from = uri.spelling.size();
        write();
        segment = {.offset = from, .size = uri.spelling.size() - from};
    };

    // A share, //server/share/..., on POSIX too, as VS Code reads it.
    if(std::string_view(slashed).starts_with("//")) {
        const std::string_view rest = std::string_view(slashed).substr(2);
        const std::size_t slash = rest.find('/');
        if(slash == std::string_view::npos || slash == 0 || slash + 1 == rest.size() ||
           rest[slash + 1] == '/') {
            return std::unexpected(invalid());
        }
        const std::string_view server = rest.substr(0, slash);
        append(uri.authority_segment, [&] {
            // An IPv6 literal keeps its brackets.
            if(server.starts_with('[') && server.ends_with(']')) {
                uri.spelling.push_back('[');
                encode_into(uri.spelling, server.substr(1, server.size() - 2), true);
                uri.spelling.push_back(']');
            } else {
                encode_into(uri.spelling, server, true);
            }
        });
        append(uri.path_segment, [&] { encode_into(uri.spelling, rest.substr(slash), false); });
        return uri;
    }

    uri.authority_segment = {.offset = uri.spelling.size(), .size = 0};
    if(root.prefix == Prefix::Drive) {
        // C:/x is /c:/x.
        append(uri.path_segment, [&] {
            uri.spelling.push_back('/');
            encode_into(uri.spelling, slashed, false);
        });
        return uri;
    }
    if(!slashed.starts_with('/')) {
        return std::unexpected(invalid());
    }
    append(uri.path_segment, [&] { encode_into(uri.spelling, slashed, false); });
    return uri;
}

std::error_code URI::to_path(buffer_ref out, fs::path::Style style) const {
    if(!is_file()) {
        return invalid();
    }
    out.clear();
    const std::string_view server = authority();
    if(!server.empty() && !string_ref(server).equals_insensitive("localhost")) {
        out.append("//");
        if(auto error = decode_into(out, server)) {
            return error;
        }
        // A server with a separator in it would move where the share starts.
        if(std::ranges::any_of(out.view().substr(2),
                               [style](char c) { return fs::path::is_separator(c, style); })) {
            return invalid();
        }
        return decode_into(out, path());
    }
    if(auto error = decode_into(out, path())) {
        return error;
    }
    const std::string_view local = out.view();
    if(!local.starts_with('/')) {
        return invalid();
    }
    if(style == fs::path::Style::Windows && local.size() >= 3 && is_ascii_alpha(local[1]) &&
       local[2] == ':') {
        // A drive without its root directory, c:x, depends on the current one.
        if(local.size() == 3 || !fs::path::is_separator(local[3], style)) {
            return invalid();
        }
        out.assign(local.substr(1));
    }
    return {};
}

void URI::percent_encode(buffer_ref out, std::string_view text, bool encode_slash) {
    out.clear();
    encode_into(out, text, encode_slash);
}

std::error_code URI::percent_decode(buffer_ref out, std::string_view text) {
    out.clear();
    return decode_into(out, text);
}

std::string_view URI::text() const noexcept {
    return spelling;
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

}  // namespace kotatsu::ipc::lsp
