#include "kota/zest/zest.h"
#include "kota/ipc/lsp/uri.h"

namespace kota::ipc::lsp {
namespace {

ZEST_SUITE(ipc_lsp_uri) {

ZEST_CASE(parse_full_uri) {
    auto uri = URI::parse("https://example.com/a/b?x=1#frag");
    ZASSERT(uri);

    ZEXPECT(uri->scheme() == "https");
    ZEXPECT(uri->has_authority());
    ZEXPECT(uri->authority() == "example.com");
    ZEXPECT(uri->path() == "/a/b");
    ZEXPECT(uri->has_query());
    ZEXPECT(uri->query() == "x=1");
    ZEXPECT(uri->has_fragment());
    ZEXPECT(uri->fragment() == "frag");
    ZEXPECT(uri->str() == "https://example.com/a/b?x=1#frag");
}

ZEST_CASE(parse_no_authority) {
    auto uri = URI::parse("mailto:user@example.com");
    ZASSERT(uri);

    ZEXPECT(uri->scheme() == "mailto");
    ZEXPECT(!uri->has_authority());
    ZEXPECT(uri->path() == "user@example.com");
    ZEXPECT(!uri->has_query());
    ZEXPECT(!uri->has_fragment());
}

ZEST_CASE(parse_invalid_uri_fails) {
    ZEXPECT(!URI::parse("noscheme").has_value());
    ZEXPECT(!URI::parse("1abc://example.com").has_value());
    ZEXPECT(!URI::parse("://example.com").has_value());
}

ZEST_CASE(percent_roundtrip) {
    std::string_view raw = "a b/c?d";
    auto encoded = URI::percent_encode(raw, false);
    ZEXPECT(encoded == "a%20b/c%3Fd");

    auto decoded = URI::percent_decode(encoded);
    ZASSERT(decoded);
    ZEXPECT(*decoded == raw);
}

ZEST_CASE(encode_non_ascii) {
    const char raw[] = {static_cast<char>(0xC3), static_cast<char>(0xA9)};
    auto encoded = URI::percent_encode(std::string_view(raw, sizeof(raw)), false);
    ZEXPECT(encoded == "%C3%A9");
}

ZEST_CASE(decode_invalid_input_fails) {
    ZEXPECT(!URI::percent_decode("%").has_value());
    ZEXPECT(!URI::percent_decode("%1").has_value());
    ZEXPECT(!URI::percent_decode("%GG").has_value());
}

ZEST_CASE(file_path_roundtrip) {
    auto uri = URI::from_file_path("/tmp/a b.txt");
    ZASSERT(uri);

    ZEXPECT(uri->is_file());
    ZEXPECT(uri->str() == "file:///tmp/a%20b.txt");

    auto path = uri->file_path();
    ZASSERT(path);
    ZEXPECT(*path == "/tmp/a b.txt");
}

ZEST_CASE(file_windows_roundtrip) {
    auto uri = URI::from_file_path("C:\\work\\a b.txt");
    ZASSERT(uri);

    ZEXPECT(uri->str() == "file:///C:/work/a%20b.txt");

    auto path = uri->file_path();
    ZASSERT(path);

#if defined(_WIN32)
    ZEXPECT(*path == "C:/work/a b.txt");
#else
    ZEXPECT(*path == "/C:/work/a b.txt");
#endif
}

ZEST_CASE(file_unc_roundtrip) {
    auto uri = URI::from_file_path("\\\\server\\share\\a b.txt");
    ZASSERT(uri);

    ZEXPECT(uri->str() == "file://server/share/a%20b.txt");

    auto path = uri->file_path();
    ZASSERT(path);
    ZEXPECT(*path == "//server/share/a b.txt");
}

ZEST_CASE(file_unc_ipv6) {
    auto uri = URI::from_file_path("\\\\[::1]\\share\\a.txt");
    ZASSERT(uri);

    ZEXPECT(uri->str() == "file://[::1]/share/a.txt");

    auto path = uri->file_path();
    ZASSERT(path);
    ZEXPECT(*path == "//[::1]/share/a.txt");
}

ZEST_CASE(relative_file_path_fails) {
    ZEXPECT(!URI::from_file_path("relative/file.txt").has_value());
    ZEXPECT(!URI::from_file_path("C:relative.txt").has_value());
}

ZEST_CASE(unc_path_without_a_share_fails) {
    ZEXPECT(!URI::from_file_path("\\\\server\\").has_value());
    ZEXPECT(!URI::from_file_path("\\\\server\\\\dir").has_value());
}

ZEST_CASE(authority_handling) {
    auto local = URI::parse("file://localhost/tmp/a.txt");
    ZASSERT(local);
    auto local_path = local->file_path();
    ZASSERT(local_path);
    ZEXPECT(*local_path == "/tmp/a.txt");

    auto local_upper = URI::parse("file://LOCALHOST/tmp/a.txt");
    ZASSERT(local_upper);
    auto local_upper_path = local_upper->file_path();
    ZASSERT(local_upper_path);
    ZEXPECT(*local_upper_path == "/tmp/a.txt");

    auto remote = URI::parse("file://server/share/a.txt");
    ZASSERT(remote);
    auto remote_path = remote->file_path();
    ZASSERT(remote_path);
    ZEXPECT(*remote_path == "//server/share/a.txt");
}

ZEST_CASE(file_path_of_a_bad_authority_fails) {
    auto slash_host = URI::parse("file://server%2Fteam/share/a.txt");
    ZASSERT(slash_host);
    ZEXPECT(!slash_host->file_path());

    auto backslash_host = URI::parse("file://server%5Cteam/share/a.txt");
    ZASSERT(backslash_host);
    ZEXPECT(!backslash_host->file_path());
}

ZEST_CASE(non_file_path_fails) {
    auto uri = URI::parse("https://example.com/a.txt");
    ZASSERT(uri);
    ZEXPECT(!uri->file_path());
}

};  // ZEST_SUITE(ipc_lsp_uri)

}  // namespace
}  // namespace kota::ipc::lsp
