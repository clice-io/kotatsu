#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kotatsu/zest/zest.h"
#include "kotatsu/sys/cmd.h"

namespace kotatsu::sys::cmd {

namespace {

using namespace std::literals;

using Arguments = std::vector<std::string>;

constexpr auto windows = Quoting::Windows;
constexpr auto posix = Quoting::POSIX;

/// The arguments split() reads in `line`.
Arguments split_all(std::string_view line, Quoting quoting) {
    Arguments arguments;
    for(auto argument: cmd::split(line, quoting)) {
        arguments.emplace_back(argument);
    }
    return arguments;
}

/// Whether `argument` is a view of `line` itself.
bool views(std::string_view line, std::string_view argument) {
    return std::less_equal<>()(line.data(), argument.data()) &&
           std::less_equal<>()(argument.data() + argument.size(), line.data() + line.size());
}

ZEST_SUITE(sys_cmd_split) {

ZEST_CASE(is_an_input_view) {
    ZSTATIC_EXPECT(std::ranges::view<split_view>);
    ZSTATIC_EXPECT(std::ranges::input_range<split_view>);
}

ZEST_CASE(views_an_argument_with_nothing_to_unquote) {
    for(auto [line, quoting]: {
            std::pair{R"(p a\b "c d")"sv, windows},
            std::pair{R"(p a/b "c d")"sv, posix  }
    }) {
        ZEST_CONTEXT("{}", line);
        auto view = cmd::split(line, quoting);
        auto it = view.begin();
        ZEXPECT(views(line, *it));
        ++it;
        ZEXPECT(views(line, *it));
        ++it;
        ZEXPECT(*it == "c d");
        ZEXPECT(!views(line, *it));
    }
}

// An argument views its iterator or the line: a copy keeps its own.
ZEST_CASE(copies_of_an_iterator_keep_their_argument) {
    auto view = cmd::split(R"(p "a b" c)", windows);
    auto it = view.begin();
    auto program = it;
    ++it;
    auto copy = it;
    ++it;
    ZEXPECT(*program == "p");
    ZEXPECT(*copy == "a b");
    ZEXPECT(*it == "c");
}

ZEST_CASE(an_empty_line_has_no_arguments) {
    ZEXPECT(split_all("", windows).empty());
    ZEXPECT(split_all("", posix).empty());
    ZEXPECT(split_all(" \t\r\n ", posix).empty());
}

ZEST_CASE(windows_reads_the_program_by_its_own_rule) {
    // Backslashes are literal in it, and every quote toggles.
    ZEXPECT(split_all(R"("C:\dir\"x y)", windows) == Arguments{R"(C:\dir\x)", "y"});
    ZEXPECT(split_all(R"(C:\a" "b\c.exe arg)", windows) == Arguments{R"(C:\a b\c.exe)", "arg"});
    ZEXPECT(split_all(R"("" a)", windows) == Arguments{"", "a"});
    ZEXPECT(split_all(R"("a""b" c)", windows) == Arguments{"ab", "c"});
    ZEXPECT(split_all(R"(\\server\x.exe a\\"b")", windows) == Arguments{R"(\\server\x.exe)", R"(a\b)"});
}

ZEST_CASE(windows_reads_an_empty_program_before_a_blank) {
    ZEXPECT(split_all(" p", windows) == Arguments{"", "p"});
    ZEXPECT(split_all("   ", windows) == Arguments{""});
    // Quotes still open run to the end.
    ZEXPECT(split_all(R"("C:\a b)", windows) == Arguments{R"(C:\a b)"});
}

ZEST_CASE(windows_separates_on_blanks_outside_quotes) {
    ZEXPECT(split_all("p  a\t b ", windows) == Arguments{"p", "a", "b"});
    ZEXPECT(split_all(R"(p "a b" c)", windows) == Arguments{"p", "a b", "c"});
    ZEXPECT(split_all(R"(p a"b c"d)", windows) == Arguments{"p", "ab cd"});
}

ZEST_CASE(windows_keeps_empty_quoted_arguments) {
    ZEXPECT(split_all(R"(p "" "")", windows) == Arguments{"p", "", ""});
    // Quotes still open at the end.
    ZEXPECT(split_all(R"(p ")", windows) == Arguments{"p", ""});
    ZEXPECT(split_all(R"(p "abc)", windows) == Arguments{"p", "abc"});
}

ZEST_CASE(windows_halves_backslashes_before_a_quote) {
    ZEXPECT(split_all(R"(p a\\"b c")", windows) == Arguments{"p", R"(a\b c)"});
    ZEXPECT(split_all(R"(p a\\\"b)", windows) == Arguments{"p", R"(a\"b)"});
    ZEXPECT(split_all(R"(p a\\b)", windows) == Arguments{"p", R"(a\\b)"});
    ZEXPECT(split_all(R"(p "C:\dir\\")", windows) == Arguments{"p", R"(C:\dir\)"});
    ZEXPECT(split_all(R"(p a\\)", windows) == Arguments{"p", R"(a\\)"});
}

// The rule of the C runtime since 2008, which an older one read as `a"b` and
// ` c`.
ZEST_CASE(windows_reads_two_quotes_in_quotes_as_one_and_stays_in_them) {
    ZEXPECT(split_all(R"(p "a""b")", windows) == Arguments{"p", R"(a"b)"});
    ZEXPECT(split_all(R"(p "a""")", windows) == Arguments{"p", R"(a")"});
    ZEXPECT(split_all(R"(p "a""b c")", windows) == Arguments{"p", R"(a"b c)"});
}

ZEST_CASE(windows_keeps_line_ends_and_nuls) {
    ZEXPECT(split_all("p a\nb\r\n", windows) == Arguments{"p", "a\nb\r\n"});
    ZEXPECT(split_all("p\0q a\0b"sv, windows) == (Arguments{"p\0q"s, "a\0b"s}));
}

ZEST_CASE(windows_expands_no_wildcard) {
    ZEXPECT(split_all("p *.cpp 'foo*' ?", windows) == Arguments{"p", "*.cpp", "'foo*'", "?"});
}

// LLVM's cases of cl::TokenizeGNUCommandLine, from
// llvm/unittests/Support/CommandLineTest.cpp, but for the one that the next
// case reads otherwise.
ZEST_CASE(posix_reads_llvms_gnu_cases) {
    ZEXPECT(split_all(R"(foo\ bar "foo bar" 'foo bar' -DFOO=bar\(\) )"
                  R"(foo"bar"baz C:\\src\\foo.cpp "C:\src\foo.cpp")",
                  posix) == Arguments{"foo bar",
                                      "foo bar",
                                      "foo bar",
                                      "-DFOO=bar()",
                                      "foobarbaz",
                                      R"(C:\src\foo.cpp)",
                                      "C:srcfoo.cpp"});
    ZEXPECT(split_all(R"(a b c "" d)", posix) == Arguments{"a", "b", "c", "", "d"});
    ZEXPECT(split_all(R"(a b c '' d)", posix) == Arguments{"a", "b", "c", "", "d"});
    ZEXPECT(split_all(R"('' a b c d "")", posix) == Arguments{"", "a", "b", "c", "d", ""});
    ZEXPECT(split_all(R"("" '')", posix) == Arguments{"", ""});
    ZEXPECT(split_all(R"('''' """" ''"")", posix) == Arguments{"", "", ""});
    ZEXPECT(split_all(R"(  a b c '' d  )", posix) == Arguments{"a", "b", "c", "", "d"});
    ZEXPECT(split_all(R"(a""a ""b c"" d''d ''e f'')", posix) ==
            Arguments{"aa", "b", "c", "dd", "e", "f"});
    ZEXPECT(split_all(R"("'a'"'"b"')", posix) == Arguments{R"('a'"b")"});
    ZEXPECT(split_all(R"(a b ')", posix) == Arguments{"a", "b", ""});
    ZEXPECT(split_all(R"(a b 'c d)", posix) == Arguments{"a", "b", "c d"});
    ZEXPECT(split_all("a 'b\nc' d", posix) == Arguments{"a", "b\nc", "d"});
    ZEXPECT(split_all("clang -Xclang foo\n\nfoo\"bar\"baz\n x.cpp\n", posix) ==
            Arguments{"clang", "-Xclang", "foo", "foobarbaz", "x.cpp"});
}

// As sh and clang's compilation database read them, where LLVM's GNU
// tokenizer reads `'foo\\bar'` as `foo\bar`.
ZEST_CASE(posix_keeps_backslashes_in_single_quotes) {
    ZEXPECT(split_all(R"('foo\\bar')", posix) == Arguments{R"(foo\\bar)"});
    ZEXPECT(split_all(R"('a\' b)", posix) == Arguments{R"(a\)", "b"});
}

// A backslash escapes any character, inside double quotes too: as LLVM and
// clang read them, where sh keeps it before all but `$`, a backquote, `"`,
// `\` and a newline. A generator escapes those alone, which both read alike.
ZEST_CASE(posix_escapes_any_character_with_a_backslash) {
    ZEXPECT(split_all(R"(a\ b \' \" "a\"b" "a\b")", posix) ==
            Arguments{"a b", "'", R"(")", R"(a"b)", "ab"});
    // A newline, which sh would read as a line continuation, too.
    ZEXPECT(split_all("a\\\nb", posix) == Arguments{"a\nb"});
}

ZEST_CASE(posix_keeps_a_backslash_at_the_end) {
    ZEXPECT(split_all(R"(a\)", posix) == Arguments{R"(a\)"});
    ZEXPECT(split_all(R"("a\)", posix) == Arguments{R"(a\)"});
}

ZEST_CASE(posix_separates_at_tabs_and_line_ends) {
    ZEXPECT(split_all("a\tb\r\nc\nd", posix) == Arguments{"a", "b", "c", "d"});
}

ZEST_CASE(posix_keeps_nuls) {
    ZEXPECT(split_all("a\0b c"sv, posix) == (Arguments{"a\0b"s, "c"}));
}

// clang's cases of its compilation database's commands, from
// clang/unittests/Tooling/CompilationDatabaseTest.cpp, unescaped from the
// JSON they are written in.
ZEST_CASE(posix_reads_clangs_database_cases) {
    ZEXPECT(split_all("a b c", posix) == Arguments{"a", "b", "c"});
    ZEXPECT(split_all("   a   b   ", posix) == Arguments{"a", "b"});
    ZEXPECT(split_all(R"(a\\)", posix) == Arguments{R"(a\)"});
    ZEXPECT(split_all(R"(a\")", posix) == Arguments{R"(a")"});
    ZEXPECT(split_all(R"("  a  b  ")", posix) == Arguments{"  a  b  "});
    ZEXPECT(split_all(R"(  " a "  " b "  )", posix) == Arguments{" a ", " b "});
    ZEXPECT(split_all(R"("""")", posix) == Arguments{""});
    ZEXPECT(split_all(R"("\"")", posix) == Arguments{R"(")"});
    ZEXPECT(split_all(R"(  \"  "a \" b "     "and\\c"   \")", posix) ==
            Arguments{R"(")", R"(a " b )", R"(and\c)", R"(")"});
    ZEXPECT(split_all(R"("a""b")", posix) == Arguments{"ab"});
    ZEXPECT(split_all(R"("a"bcd"ef""""g")", posix) == Arguments{"abcdefg"});
    ZEXPECT(split_all(R"("abc)", posix) == Arguments{"abc"});
    ZEXPECT(split_all(R"(")", posix) == Arguments{""});
    ZEXPECT(split_all(R"(a'\b "c"')", posix) == Arguments{R"(a\b "c")"});
}

};  // ZEST_SUITE(sys_cmd_split)

}  // namespace

}  // namespace kotatsu::sys::cmd
