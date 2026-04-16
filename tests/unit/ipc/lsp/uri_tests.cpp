#include "kota/zest/zest.h"
#include "kota/ipc/lsp/uri.h"

namespace kota::ipc::lsp {

TEST_SUITE(language_uri) {

TEST_CASE(parse_full_uri) {
    auto uri = URI::parse("https://example.com/a/b?x=1#frag");
    ASSERT_TRUE(uri.has_value());

    EXPECT_EQ(uri->scheme(), "https");
    EXPECT_TRUE(uri->has_authority());
    EXPECT_EQ(uri->authority(), "example.com");
    EXPECT_EQ(uri->path(), "/a/b");
    EXPECT_TRUE(uri->has_query());
    EXPECT_EQ(uri->query(), "x=1");
    EXPECT_TRUE(uri->has_fragment());
    EXPECT_EQ(uri->fragment(), "frag");
    EXPECT_EQ(uri->str(), "https://example.com/a/b?x=1#frag");
}

TEST_CASE(parse_no_authority) {
    auto uri = URI::parse("mailto:user@example.com");
    ASSERT_TRUE(uri.has_value());

    EXPECT_EQ(uri->scheme(), "mailto");
    EXPECT_FALSE(uri->has_authority());
    EXPECT_EQ(uri->path(), "user@example.com");
    EXPECT_FALSE(uri->has_query());
    EXPECT_FALSE(uri->has_fragment());
}

TEST_CASE(parse_invalid_uri) {
    EXPECT_FALSE(URI::parse("noscheme").has_value());
    EXPECT_FALSE(URI::parse("1abc://example.com").has_value());
    EXPECT_FALSE(URI::parse("://example.com").has_value());
}

TEST_CASE(percent_roundtrip) {
    std::string_view raw = "a b/c?d";
    auto encoded = URI::percent_encode(raw, false);
    EXPECT_EQ(encoded, "a%20b/c%3Fd");

    auto decoded = URI::percent_decode(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, raw);
}

TEST_CASE(encode_non_ascii) {
    const char raw[] = {static_cast<char>(0xC3), static_cast<char>(0xA9)};
    auto encoded = URI::percent_encode(std::string_view(raw, sizeof(raw)), false);
    EXPECT_EQ(encoded, "%C3%A9");
}

TEST_CASE(decode_invalid_input) {
    EXPECT_FALSE(URI::percent_decode("%").has_value());
    EXPECT_FALSE(URI::percent_decode("%1").has_value());
    EXPECT_FALSE(URI::percent_decode("%GG").has_value());
}

TEST_CASE(file_path_roundtrip) {
    auto uri = URI::from_file_path("/tmp/a b.txt");
    ASSERT_TRUE(uri.has_value());

    EXPECT_TRUE(uri->is_file());
    EXPECT_EQ(uri->str(), "file:///tmp/a%20b.txt");

    auto path = uri->file_path();
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "/tmp/a b.txt");
}

TEST_CASE(file_windows_roundtrip) {
    auto uri = URI::from_file_path("C:\\work\\a b.txt");
    ASSERT_TRUE(uri.has_value());

    EXPECT_EQ(uri->str(), "file:///C:/work/a%20b.txt");

    auto path = uri->file_path();
    ASSERT_TRUE(path.has_value());

#if defined(_WIN32)
    EXPECT_EQ(*path, "C:/work/a b.txt");
#else
    EXPECT_EQ(*path, "/C:/work/a b.txt");
#endif
}

TEST_CASE(file_unc_roundtrip) {
    auto uri = URI::from_file_path("\\\\server\\share\\a b.txt");
    ASSERT_TRUE(uri.has_value());

    EXPECT_EQ(uri->str(), "file://server/share/a%20b.txt");

    auto path = uri->file_path();
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "//server/share/a b.txt");
}

TEST_CASE(file_unc_ipv6) {
    auto uri = URI::from_file_path("\\\\[::1]\\share\\a.txt");
    ASSERT_TRUE(uri.has_value());

    EXPECT_EQ(uri->str(), "file://[::1]/share/a.txt");

    auto path = uri->file_path();
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(*path, "//[::1]/share/a.txt");
}

TEST_CASE(reject_relative_path) {
    EXPECT_FALSE(URI::from_file_path("relative/file.txt").has_value());
    EXPECT_FALSE(URI::from_file_path("C:relative.txt").has_value());
}

TEST_CASE(reject_unc_shareless) {
    EXPECT_FALSE(URI::from_file_path("\\\\server\\").has_value());
    EXPECT_FALSE(URI::from_file_path("\\\\server\\\\dir").has_value());
}

TEST_CASE(authority_handling) {
    auto local = URI::parse("file://localhost/tmp/a.txt");
    ASSERT_TRUE(local.has_value());
    auto local_path = local->file_path();
    ASSERT_TRUE(local_path.has_value());
    EXPECT_EQ(*local_path, "/tmp/a.txt");

    auto local_upper = URI::parse("file://LOCALHOST/tmp/a.txt");
    ASSERT_TRUE(local_upper.has_value());
    auto local_upper_path = local_upper->file_path();
    ASSERT_TRUE(local_upper_path.has_value());
    EXPECT_EQ(*local_upper_path, "/tmp/a.txt");

    auto remote = URI::parse("file://server/share/a.txt");
    ASSERT_TRUE(remote.has_value());
    auto remote_path = remote->file_path();
    ASSERT_TRUE(remote_path.has_value());
    EXPECT_EQ(*remote_path, "//server/share/a.txt");
}

TEST_CASE(reject_bad_authority) {
    auto slash_host = URI::parse("file://server%2Fteam/share/a.txt");
    ASSERT_TRUE(slash_host.has_value());
    EXPECT_FALSE(slash_host->file_path().has_value());

    auto backslash_host = URI::parse("file://server%5Cteam/share/a.txt");
    ASSERT_TRUE(backslash_host.has_value());
    EXPECT_FALSE(backslash_host->file_path().has_value());
}

TEST_CASE(non_file_path_fails) {
    auto uri = URI::parse("https://example.com/a.txt");
    ASSERT_TRUE(uri.has_value());
    EXPECT_FALSE(uri->file_path().has_value());
}

};  // TEST_SUITE(language_uri)

}  // namespace kota::ipc::lsp
