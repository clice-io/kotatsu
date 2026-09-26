#include "kota/zest/zest.h"
#include "kota/ipc/lsp/uri.h"

namespace kota::ipc::lsp {
namespace {

ZEST_SUITE(language_uri) {

ZEST_CASE(parse_full_uri) {
    auto uri = URI::parse("https://example.com/a/b?x=1#frag");
    ASSERT(uri);

    EXPECT(uri->scheme() == "https");
    EXPECT(uri->has_authority());
    EXPECT(uri->authority() == "example.com");
    EXPECT(uri->path() == "/a/b");
    EXPECT(uri->has_query());
    EXPECT(uri->query() == "x=1");
    EXPECT(uri->has_fragment());
    EXPECT(uri->fragment() == "frag");
    EXPECT(uri->str() == "https://example.com/a/b?x=1#frag");
}

ZEST_CASE(parse_no_authority) {
    auto uri = URI::parse("mailto:user@example.com");
    ASSERT(uri);

    EXPECT(uri->scheme() == "mailto");
    EXPECT(!uri->has_authority());
    EXPECT(uri->path() == "user@example.com");
    EXPECT(!uri->has_query());
    EXPECT(!uri->has_fragment());
}

ZEST_CASE(parse_invalid_uri) {
    EXPECT(!URI::parse("noscheme").has_value());
    EXPECT(!URI::parse("1abc://example.com").has_value());
    EXPECT(!URI::parse("://example.com").has_value());
}

ZEST_CASE(percent_roundtrip) {
    std::string_view raw = "a b/c?d";
    auto encoded = URI::percent_encode(raw, false);
    EXPECT(encoded == "a%20b/c%3Fd");

    auto decoded = URI::percent_decode(encoded);
    ASSERT(decoded);
    EXPECT(*decoded == raw);
}

ZEST_CASE(encode_non_ascii) {
    const char raw[] = {static_cast<char>(0xC3), static_cast<char>(0xA9)};
    auto encoded = URI::percent_encode(std::string_view(raw, sizeof(raw)), false);
    EXPECT(encoded == "%C3%A9");
}

ZEST_CASE(decode_invalid_input) {
    EXPECT(!URI::percent_decode("%").has_value());
    EXPECT(!URI::percent_decode("%1").has_value());
    EXPECT(!URI::percent_decode("%GG").has_value());
}

ZEST_CASE(file_path_roundtrip) {
    auto uri = URI::from_file_path("/tmp/a b.txt");
    ASSERT(uri);

    EXPECT(uri->is_file());
    EXPECT(uri->str() == "file:///tmp/a%20b.txt");

    auto path = uri->file_path();
    ASSERT(path);
    EXPECT(*path == "/tmp/a b.txt");
}

ZEST_CASE(file_windows_roundtrip) {
    auto uri = URI::from_file_path("C:\\work\\a b.txt");
    ASSERT(uri);

    EXPECT(uri->str() == "file:///C:/work/a%20b.txt");

    auto path = uri->file_path();
    ASSERT(path);

#if defined(_WIN32)
    EXPECT(*path == "C:/work/a b.txt");
#else
    EXPECT(*path == "/C:/work/a b.txt");
#endif
}

ZEST_CASE(file_unc_roundtrip) {
    auto uri = URI::from_file_path("\\\\server\\share\\a b.txt");
    ASSERT(uri);

    EXPECT(uri->str() == "file://server/share/a%20b.txt");

    auto path = uri->file_path();
    ASSERT(path);
    EXPECT(*path == "//server/share/a b.txt");
}

ZEST_CASE(file_unc_ipv6) {
    auto uri = URI::from_file_path("\\\\[::1]\\share\\a.txt");
    ASSERT(uri);

    EXPECT(uri->str() == "file://[::1]/share/a.txt");

    auto path = uri->file_path();
    ASSERT(path);
    EXPECT(*path == "//[::1]/share/a.txt");
}

ZEST_CASE(reject_relative_path) {
    EXPECT(!URI::from_file_path("relative/file.txt").has_value());
    EXPECT(!URI::from_file_path("C:relative.txt").has_value());
}

ZEST_CASE(reject_unc_shareless) {
    EXPECT(!URI::from_file_path("\\\\server\\").has_value());
    EXPECT(!URI::from_file_path("\\\\server\\\\dir").has_value());
}

ZEST_CASE(authority_handling) {
    auto local = URI::parse("file://localhost/tmp/a.txt");
    ASSERT(local);
    auto local_path = local->file_path();
    ASSERT(local_path);
    EXPECT(*local_path == "/tmp/a.txt");

    auto local_upper = URI::parse("file://LOCALHOST/tmp/a.txt");
    ASSERT(local_upper);
    auto local_upper_path = local_upper->file_path();
    ASSERT(local_upper_path);
    EXPECT(*local_upper_path == "/tmp/a.txt");

    auto remote = URI::parse("file://server/share/a.txt");
    ASSERT(remote);
    auto remote_path = remote->file_path();
    ASSERT(remote_path);
    EXPECT(*remote_path == "//server/share/a.txt");
}

ZEST_CASE(reject_bad_authority) {
    auto slash_host = URI::parse("file://server%2Fteam/share/a.txt");
    ASSERT(slash_host);
    EXPECT(!slash_host->file_path());

    auto backslash_host = URI::parse("file://server%5Cteam/share/a.txt");
    ASSERT(backslash_host);
    EXPECT(!backslash_host->file_path());
}

ZEST_CASE(non_file_path_fails) {
    auto uri = URI::parse("https://example.com/a.txt");
    ASSERT(uri);
    EXPECT(!uri->file_path());
}

};  // ZEST_SUITE(language_uri)

}  // namespace
}  // namespace kota::ipc::lsp
