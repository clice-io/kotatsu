#include <string>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

// `?`, classes and escapes take one code point at a time. A path byte that is not UTF-8 is
// one character that wildcards and negated classes cover, and that no literal or positive
// range names.

ZEST_SUITE(support_glob_pattern_unicode) {

ZEST_CASE(question_takes_one_code_point) {
    test::expect_glob("?文.txt", {"中文.txt"}, {"文.txt", "中中文.txt"});
    test::expect_glob("?.txt", {"中.txt", "🚀.txt"}, {"中文.txt"});
    test::expect_glob("??.txt", {"中文.txt"}, {"中.txt"});
}

ZEST_CASE(classes_take_one_code_point) {
    test::expect_glob("[中文].txt", {"中.txt", "文.txt"}, {"英.txt", "a.txt"});
    // 中 (U+4E2D) lies between 一 (U+4E00) and 十 (U+5341).
    test::expect_glob("[一-十].txt", {"中.txt"}, {"a.txt"});
    test::expect_glob("[!一-十].txt", {"a.txt"}, {"中.txt"});
    test::expect_glob("[a-z中]?", {"中文", "x文"}, {"文文"});
    test::expect_glob("[α-γ-ε]", {"β", "-", "ε"}, {"δ"});
}

ZEST_CASE(escapes_and_literals_take_code_points) {
    test::expect_glob("\\中.txt", {"中.txt"}, {"文.txt"});
    test::expect_glob("*文.txt", {"中文.txt", "文.txt"}, {"中英.txt"});
    test::expect_glob("**/中.txt", {"a/b/中.txt", "中.txt"});
    test::expect_glob("中/文.txt", {"中/文.txt"}, {"中文.txt"});
    test::expect_glob("*/文*", {"中/文x"}, {"中/x文"});
    test::expect_glob("**/*说明.cpp", {"文档/说明.cpp"}, {"文档/说明.cpp/"});
}

ZEST_CASE(invalid_path_bytes_are_single_characters) {
    test::expect_glob("*.txt", {"\xFF\xFE.txt"});
    test::expect_glob("??.txt", {"\xFF\xFE.txt"});
    test::expect_glob("?.txt", {"\x80.txt"}, {"\xFF\xFE.txt"});
    test::expect_glob("[!a].txt", {"\xFF.txt"}, {"a.txt"});
    test::expect_glob("**/[!a].cpp", {"\xFF\x80/\xFE.cpp"}, {"\xFF\x80\xFE.cpp"});
}

ZEST_CASE(a_code_point_never_matches_its_latin1_byte) {
    // é is U+00E9; the lone byte E9 is not UTF-8.
    test::expect_glob("é.txt", {}, {"\xE9.txt"});
    test::expect_glob("[é].txt", {}, {"\xE9.txt"});
    test::expect_glob("[!é].txt", {"\xE9.txt"});
}

ZEST_CASE(star_retries_by_whole_code_points) {
    // Retrying byte by byte would stop inside 中 and let `?` and `[!X]` take its trailing
    // bytes as two more characters.
    test::expect_glob("*?[!X]X", {"中aX"}, {"中X"});
    test::expect_glob("**/*?[!X]X", {"/中aX"}, {"/中X"});
    test::expect_glob("*?[!X]X/**", {"中aX"}, {"中X"});
}

ZEST_CASE(retries_across_long_multibyte_runs) {
    const std::string rockets = test::repeat("🚀", 65537);
    test::expect_glob(
        "*[Z]",
        {test::repeat("🚀", 65535) + "Z", test::repeat("🚀", 65536) + "Z", rockets + "Z"});
    test::expect_glob("*Z", {rockets + "Z"}, {rockets});
    const std::string chinese = test::repeat("中", 1000);
    test::expect_glob("**/中*?b", {chinese + "/中🚀b"}, {chinese + "/中b"});
    test::expect_glob("源**/?.cpp",
                      {"源" + std::string(70000, 'a') + "/中.cpp"},
                      {"源中.cpp", "源" + std::string(70000, 'a') + "/中文.cpp"});
}

ZEST_CASE(retries_keep_classes_in_order) {
    const std::string prefix = test::repeat("中", 32);
    test::expect_glob("**/*[中]*?*[中]*[中]b/x",
                      {prefix + "b/y/中中中中b/x"},
                      {prefix + "b/y/中中中b/x"});
    test::expect_glob("**/[中]*[文]*/[🚀]*/目标",
                      {"中文/🚀/错/中文/🚀/目标"},
                      {"中文/🚀/错/中文/x/目标"});
    test::expect_glob("**/[中]*[文]/**/[🚀]/目标",
                      {"中文/中x文/🚀/错/🚀/目标"},
                      {"中文/中x文/🚀/错/x/目标"});
}

ZEST_CASE(escaped_class_brackets_in_literals) {
    test::expect_glob(R"(src/*/test_\[中\]*.cpp)",
                      {"src/目录/test_[中]文.cpp", "src//test_[中].cpp"},
                      {"src/目录/sub/test_[中].cpp", "src/目录/test_中.cpp"});
}

};  // ZEST_SUITE(support_glob_pattern_unicode)

}  // namespace

}  // namespace kota
