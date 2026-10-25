#include <string>
#include <vector>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/process.h"

namespace kota::sys {

namespace {

using Arguments = std::vector<std::string>;

ZEST_SUITE(sys_process_command_line) {

ZEST_CASE(quote_leaves_a_plain_argument) {
    ZEXPECT(quote_windows_argument("plain") == "plain");
    ZEXPECT(quote_windows_argument(R"(C:\dir\file)") == R"(C:\dir\file)");
    ZEXPECT(quote_windows_argument("caf\xC3\xA9") == "caf\xC3\xA9");
}

ZEST_CASE(quote_wraps_an_empty_or_blank_argument) {
    ZEXPECT(quote_windows_argument("") == R"("")");
    ZEXPECT(quote_windows_argument("a b") == R"("a b")");
    ZEXPECT(quote_windows_argument("a\tb") == "\"a\tb\"");
    ZEXPECT(quote_windows_argument("a\nb") == "\"a\nb\"");
    ZEXPECT(quote_windows_argument("a\vb") == "\"a\vb\"");
}

ZEST_CASE(quote_escapes_quotes_and_the_backslashes_before_them) {
    ZEXPECT(quote_windows_argument(R"(a"b)") == R"("a\"b")");
    ZEXPECT(quote_windows_argument(R"(a\"b)") == R"("a\\\"b")");
    // Before the closing quote, backslashes double.
    ZEXPECT(quote_windows_argument(R"(C:\dir with space\)") == R"("C:\dir with space\\")");
    // Elsewhere they stay as they are.
    ZEXPECT(quote_windows_argument(R"(a\\b c)") == R"("a\\b c")");
}

ZEST_CASE(join_quotes_the_program_without_escapes) {
    Arguments arguments{R"(C:\Program Files\x.exe)", "a b", "", R"(c")"};
    ZEXPECT(join_windows_command_line(arguments) == R"("C:\Program Files\x.exe" "a b" "" "c\"")");
    ZEXPECT(join_windows_command_line(Arguments{R"(C:\x.exe)"}) == R"(C:\x.exe)");
    ZEXPECT(join_windows_command_line(Arguments{}) == "");
}

ZEST_CASE(split_reads_the_program_by_its_own_rule) {
    // Backslashes are literal in it, and every quote toggles.
    ZEXPECT(split_windows_command_line(R"("C:\dir\"x y)") == Arguments{R"(C:\dir\x)", "y"});
    ZEXPECT(split_windows_command_line(R"(C:\a" "b\c.exe arg)") ==
            Arguments{R"(C:\a b\c.exe)", "arg"});
    ZEXPECT(split_windows_command_line(R"("" a)") == Arguments{"", "a"});
}

ZEST_CASE(split_separates_on_blanks_outside_quotes) {
    ZEXPECT(split_windows_command_line("p  a\t b ") == Arguments{"p", "a", "b"});
    ZEXPECT(split_windows_command_line(R"(p "a b" c)") == Arguments{"p", "a b", "c"});
    ZEXPECT(split_windows_command_line(R"(p a"b c"d)") == Arguments{"p", "ab cd"});
}

ZEST_CASE(split_keeps_empty_quoted_arguments) {
    ZEXPECT(split_windows_command_line(R"(p "" "")") == Arguments{"p", "", ""});
    // Quotes still open at the end.
    ZEXPECT(split_windows_command_line(R"(p ")") == Arguments{"p", ""});
    ZEXPECT(split_windows_command_line(R"(p "abc)") == Arguments{"p", "abc"});
}

ZEST_CASE(split_halves_backslashes_before_a_quote) {
    ZEXPECT(split_windows_command_line(R"(p a\\"b c")") == Arguments{"p", R"(a\b c)"});
    ZEXPECT(split_windows_command_line(R"(p a\\\"b)") == Arguments{"p", R"(a\"b)"});
    ZEXPECT(split_windows_command_line(R"(p a\\b)") == Arguments{"p", R"(a\\b)"});
    ZEXPECT(split_windows_command_line(R"(p "C:\dir\\")") == Arguments{"p", R"(C:\dir\)"});
}

ZEST_CASE(split_reads_two_quotes_in_quotes_as_one) {
    ZEXPECT(split_windows_command_line(R"(p "a""b")") == Arguments{"p", R"(a"b)"});
    ZEXPECT(split_windows_command_line(R"(p "a""")") == Arguments{"p", R"(a")"});
}

// The rule of the CRT since 2008, which an older one read as `a"b` and ` c`.
ZEST_CASE(split_reads_two_quotes_in_quotes_and_stays_in_them) {
    ZEXPECT(split_windows_command_line(R"(p "a""b c")") == Arguments{"p", R"(a"b c)"});
}

ZEST_CASE(split_reads_an_empty_program_before_a_blank) {
    ZEXPECT(split_windows_command_line(" p") == Arguments{"", "p"});
    ZEXPECT(split_windows_command_line("   ") == Arguments{""});
    // Quotes still open run to the end.
    ZEXPECT(split_windows_command_line(R"("C:\a b)") == Arguments{R"(C:\a b)"});
}

ZEST_CASE(split_keeps_backslashes_at_the_end) {
    ZEXPECT(split_windows_command_line(R"(p a\\)") == Arguments{"p", R"(a\\)"});
}

ZEST_CASE(split_expands_no_wildcard) {
    ZEXPECT(split_windows_command_line("p *.cpp 'foo*' ?") ==
            Arguments{"p", "*.cpp", "'foo*'", "?"});
}

ZEST_CASE(split_of_an_empty_line_has_no_arguments) {
    ZEXPECT(split_windows_command_line("").empty());
}

ZEST_CASE(split_reads_back_what_join_writes) {
    Arguments arguments{
        R"(C:\Program Files\x.exe)",
        "",
        "a b",
        R"(a"b)",
        R"(a\"b)",
        R"(a\)",
        R"(a b\\)",
        "\t",
        "caf\xC3\xA9 \xF0\x9F\x98\x80",
        "*",
    };
    ZEXPECT(split_windows_command_line(join_windows_command_line(arguments)) == arguments);
}

};  // ZEST_SUITE(sys_process_command_line)

}  // namespace

}  // namespace kota::sys
