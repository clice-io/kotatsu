#include <string>
#include <string_view>
#include <system_error>

#include "kotatsu/zest/zest.h"
#include "kotatsu/ipc/lsp/uri.h"

namespace kotatsu::ipc::lsp {
namespace {

using fs::path::Style;

/// The path `uri` names, as `style` writes it; empty when it names none.
std::string path_of(const URI& uri, Style style) {
    std::string path;
    if(uri.to_path(path, style)) {
        return {};
    }
    return path;
}

ZEST_SUITE(ipc_lsp_uri) {

ZEST_CASE(from_string_reads_every_component) {
    auto uri = URI::from_string("https://example.com/a/b?x=1#frag");
    ZASSERT(uri);

    ZEXPECT(uri->scheme() == "https");
    ZEXPECT(uri->has_authority());
    ZEXPECT(uri->authority() == "example.com");
    ZEXPECT(uri->path() == "/a/b");
    ZEXPECT(uri->has_query());
    ZEXPECT(uri->query() == "x=1");
    ZEXPECT(uri->has_fragment());
    ZEXPECT(uri->fragment() == "frag");
    ZEXPECT(uri->text() == "https://example.com/a/b?x=1#frag");
}

ZEST_CASE(from_string_without_authority) {
    auto uri = URI::from_string("mailto:user@example.com");
    ZASSERT(uri);

    ZEXPECT(uri->scheme() == "mailto");
    ZEXPECT(!uri->has_authority());
    ZEXPECT(uri->path() == "user@example.com");
    ZEXPECT(!uri->has_query());
    ZEXPECT(!uri->has_fragment());
}

ZEST_CASE(from_string_lowers_the_scheme) {
    auto uri = URI::from_string("FILE:///Tmp/A.txt?Q#F");
    ZASSERT(uri);
    ZEXPECT(uri->scheme() == "file");
    ZEXPECT(uri->text() == "file:///Tmp/A.txt?Q#F");
}

ZEST_CASE(from_string_fragment_holds_a_question_mark) {
    auto uri = URI::from_string("https://example.com/a#b?c");
    ZASSERT(uri);
    ZEXPECT(!uri->has_query());
    ZEXPECT(uri->fragment() == "b?c");
}

ZEST_CASE(from_string_without_a_scheme_fails) {
    for(std::string_view text: {"", "noscheme", "1abc://example.com", "://example.com", "a/b:c"}) {
        ZEST_CONTEXT("text: {}", text);
        auto uri = URI::from_string(text);
        ZASSERT(!uri);
        ZEXPECT(uri.error() == std::errc::invalid_argument);
    }
}

ZEST_CASE(percent_roundtrip) {
    std::string encoded;
    URI::percent_encode(encoded, "a b/c?d");
    ZEXPECT(encoded == "a%20b/c%3Fd");

    URI::percent_encode(encoded, "a/b", true);
    ZEXPECT(encoded == "a%2Fb");

    std::string decoded;
    ZEXPECT(!URI::percent_decode(decoded, "a%20b/c%3Fd"));
    ZEXPECT(decoded == "a b/c?d");
}

ZEST_CASE(percent_encode_non_ascii) {
    std::string encoded;
    URI::percent_encode(encoded, "\xC3\xA9");
    ZEXPECT(encoded == "%C3%A9");
}

ZEST_CASE(percent_decode_bad_escape_fails) {
    std::string decoded;
    for(std::string_view text: {"%", "%1", "%GG", "a%2"}) {
        ZEST_CONTEXT("text: {}", text);
        ZEXPECT(URI::percent_decode(decoded, text) == std::errc::invalid_argument);
    }
}

ZEST_CASE(posix_path_roundtrip) {
    auto uri = URI::from_path("/tmp/a b.txt", Style::Posix);
    ZASSERT(uri);

    ZEXPECT(uri->is_file());
    ZEXPECT(uri->text() == "file:///tmp/a%20b.txt");
    ZEXPECT(path_of(*uri, Style::Posix) == "/tmp/a b.txt");
}

// On POSIX `\` is a character of a name, not a separator.
ZEST_CASE(posix_backslash_stays_in_its_name) {
    auto uri = URI::from_path(R"(/tmp/a\b.cpp)", Style::Posix);
    ZASSERT(uri);

    ZEXPECT(uri->text() == "file:///tmp/a%5Cb.cpp");
    ZEXPECT(path_of(*uri, Style::Posix) == R"(/tmp/a\b.cpp)");
}

// On POSIX `C:/x` is a relative path.
ZEST_CASE(posix_drive_path_fails) {
    for(std::string_view path: {"C:/x/y.cpp", R"(C:\x\y.cpp)"}) {
        ZEST_CONTEXT("path: {}", path);
        auto uri = URI::from_path(path, Style::Posix);
        ZASSERT(!uri);
        ZEXPECT(uri.error() == std::errc::invalid_argument);
    }
}

ZEST_CASE(windows_drive_roundtrip) {
    auto uri = URI::from_path(R"(C:\work\a b.txt)", Style::Windows);
    ZASSERT(uri);

    ZEXPECT(uri->text() == "file:///c:/work/a%20b.txt");
    ZEXPECT(path_of(*uri, Style::Windows) == "c:/work/a b.txt");
    ZEXPECT(path_of(*uri, Style::Posix) == "/c:/work/a b.txt");
}

ZEST_CASE(windows_mixed_separators) {
    auto uri = URI::from_path(R"(D:/work\src/a.cpp)", Style::Windows);
    ZASSERT(uri);
    ZEXPECT(uri->text() == "file:///d:/work/src/a.cpp");
}

ZEST_CASE(windows_to_path_keeps_the_drive_as_written) {
    auto uri = URI::from_string("file:///C:/x.cpp");
    ZASSERT(uri);
    ZEXPECT(path_of(*uri, Style::Windows) == "C:/x.cpp");
}

// VS Code writes the drive's colon percent-encoded.
ZEST_CASE(windows_to_path_reads_an_encoded_drive) {
    auto uri = URI::from_string("file:///c%3A/work/a%20b.txt");
    ZASSERT(uri);
    ZEXPECT(path_of(*uri, Style::Windows) == "c:/work/a b.txt");
}

ZEST_CASE(windows_path_on_the_current_drive) {
    auto uri = URI::from_path(R"(\x\a.cpp)", Style::Windows);
    ZASSERT(uri);
    ZEXPECT(uri->text() == "file:///x/a.cpp");
    ZEXPECT(path_of(*uri, Style::Windows) == "/x/a.cpp");
}

ZEST_CASE(windows_to_path_of_a_drive_without_root_fails) {
    for(std::string_view text: {"file:///c:x.cpp", "file:///c:", "file:///c%3A"}) {
        ZEST_CONTEXT("text: {}", text);
        auto uri = URI::from_string(text);
        ZASSERT(uri);
        std::string path;
        ZEXPECT(uri->to_path(path, Style::Windows) == std::errc::invalid_argument);
    }
}

ZEST_CASE(windows_drive_without_root_fails) {
    for(std::string_view path: {"C:relative.txt", "C:"}) {
        ZEST_CONTEXT("path: {}", path);
        auto uri = URI::from_path(path, Style::Windows);
        ZASSERT(!uri);
        ZEXPECT(uri.error() == std::errc::invalid_argument);
    }
}

ZEST_CASE(windows_verbatim_or_device_path_fails) {
    for(std::string_view path: {R"(\\?\C:\x.cpp)", R"(\\?\UNC\server\share\x)", R"(\\.\pipe\x)"}) {
        ZEST_CONTEXT("path: {}", path);
        auto uri = URI::from_path(path, Style::Windows);
        ZASSERT(!uri);
        ZEXPECT(uri.error() == std::errc::invalid_argument);
    }
}

ZEST_CASE(unc_roundtrip) {
    auto windows = URI::from_path(R"(\\server\share\a b.txt)", Style::Windows);
    ZASSERT(windows);
    ZEXPECT(windows->text() == "file://server/share/a%20b.txt");
    ZEXPECT(path_of(*windows, Style::Windows) == "//server/share/a b.txt");

    auto posix = URI::from_path("//server/share/a b.txt", Style::Posix);
    ZASSERT(posix);
    ZEXPECT(posix->text() == "file://server/share/a%20b.txt");
    ZEXPECT(path_of(*posix, Style::Posix) == "//server/share/a b.txt");
}

ZEST_CASE(unc_ipv6) {
    auto uri = URI::from_path(R"(\\[::1]\share\a.txt)", Style::Windows);
    ZASSERT(uri);

    ZEXPECT(uri->text() == "file://[::1]/share/a.txt");
    ZEXPECT(path_of(*uri, Style::Windows) == "//[::1]/share/a.txt");
}

ZEST_CASE(relative_path_fails) {
    for(auto style: {Style::Posix, Style::Windows}) {
        for(std::string_view path: {"", "relative/file.txt"}) {
            ZEST_CONTEXT("path: {}", path);
            auto uri = URI::from_path(path, style);
            ZASSERT(!uri);
            ZEXPECT(uri.error() == std::errc::invalid_argument);
        }
    }
}

ZEST_CASE(unc_path_without_a_share_fails) {
    for(std::string_view path: {R"(\\server\)", R"(\\server\\dir)", R"(\\\share)"}) {
        ZEST_CONTEXT("path: {}", path);
        auto uri = URI::from_path(path, Style::Windows);
        ZASSERT(!uri);
        ZEXPECT(uri.error() == std::errc::invalid_argument);
    }
}

ZEST_CASE(to_path_reads_localhost_as_local) {
    for(std::string_view text: {"file://localhost/tmp/a.txt", "file://LOCALHOST/tmp/a.txt"}) {
        ZEST_CONTEXT("text: {}", text);
        auto uri = URI::from_string(text);
        ZASSERT(uri);
        ZEXPECT(path_of(*uri, Style::Posix) == "/tmp/a.txt");
    }

    auto remote = URI::from_string("file://server/share/a.txt");
    ZASSERT(remote);
    ZEXPECT(path_of(*remote, Style::Posix) == "//server/share/a.txt");
}

// On POSIX `\` in a server's name moves no boundary.
ZEST_CASE(posix_to_path_keeps_a_backslash_in_a_server) {
    auto uri = URI::from_string("file://a%5Cb/share/x");
    ZASSERT(uri);
    ZEXPECT(path_of(*uri, Style::Posix) == R"(//a\b/share/x)");
    std::string path;
    ZEXPECT(uri->to_path(path, Style::Windows) == std::errc::invalid_argument);
}

ZEST_CASE(to_path_of_a_server_with_a_separator_fails) {
    auto uri = URI::from_string("file://server%2Fteam/share/a.txt");
    ZASSERT(uri);
    for(auto style: {Style::Posix, Style::Windows}) {
        std::string path;
        ZEXPECT(uri->to_path(path, style) == std::errc::invalid_argument);
    }
}

ZEST_CASE(to_path_replaces_what_the_buffer_held) {
    auto uri = URI::from_string("file:///tmp/a.txt");
    ZASSERT(uri);
    std::string path = "left over";
    ZEXPECT(!uri->to_path(path, Style::Posix));
    ZEXPECT(path == "/tmp/a.txt");
}

ZEST_CASE(to_path_of_a_bad_escape_fails) {
    for(std::string_view text: {"file:///a%2", "file:///a%GG", "file://srv%2/share/x"}) {
        ZEST_CONTEXT("text: {}", text);
        auto uri = URI::from_string(text);
        ZASSERT(uri);
        std::string path;
        ZEXPECT(uri->to_path(path) == std::errc::invalid_argument);
    }
}

ZEST_CASE(to_path_of_a_relative_path_fails) {
    auto uri = URI::from_string("file:relative.txt");
    ZASSERT(uri);
    std::string path;
    ZEXPECT(uri->to_path(path) == std::errc::invalid_argument);
}

ZEST_CASE(to_path_of_another_scheme_fails) {
    auto uri = URI::from_string("https://example.com/a.txt");
    ZASSERT(uri);
    std::string path;
    ZEXPECT(uri->to_path(path) == std::errc::invalid_argument);
}

};  // ZEST_SUITE(ipc_lsp_uri)

}  // namespace
}  // namespace kotatsu::ipc::lsp
