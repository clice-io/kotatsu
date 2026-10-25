#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/support/string_list_ref.h"
#include "kota/sys/windows.h"

namespace kota::sys::windows {

namespace {

using Arguments = std::vector<std::string>;

/// `argument` as quote_argument writes it.
std::string quoted(std::string_view argument) {
    std::string out;
    quote_argument(out, argument);
    return out;
}

/// The arguments split_command_line reads in `line`.
Arguments split(std::string_view line) {
    Arguments arguments;
    for(auto argument: split_command_line(line)) {
        arguments.emplace_back(argument);
    }
    return arguments;
}

/// The command line join_command_line writes of `arguments`.
std::string joined(string_list_ref arguments) {
    std::string out;
    join_command_line(out, arguments);
    return out;
}

ZEST_SUITE(sys_windows_command_line) {

ZEST_CASE(quote_leaves_a_plain_argument) {
    ZEXPECT(quoted("plain") == "plain");
    ZEXPECT(quoted(R"(C:\dir\file)") == R"(C:\dir\file)");
    ZEXPECT(quoted("caf\xC3\xA9") == "caf\xC3\xA9");
}

ZEST_CASE(quote_wraps_an_empty_or_blank_argument) {
    ZEXPECT(quoted("") == R"("")");
    ZEXPECT(quoted("a b") == R"("a b")");
    ZEXPECT(quoted("a\tb") == "\"a\tb\"");
    ZEXPECT(quoted("a\nb") == "\"a\nb\"");
    ZEXPECT(quoted("a\vb") == "\"a\vb\"");
}

ZEST_CASE(quote_escapes_quotes_and_the_backslashes_before_them) {
    ZEXPECT(quoted(R"(a"b)") == R"("a\"b")");
    ZEXPECT(quoted(R"(a\"b)") == R"("a\\\"b")");
    // Before the closing quote, backslashes double.
    ZEXPECT(quoted(R"(C:\dir with space\)") == R"("C:\dir with space\\")");
    // Elsewhere they stay as they are.
    ZEXPECT(quoted(R"(a\\b c)") == R"("a\\b c")");
}

ZEST_CASE(join_quotes_the_program_without_escapes) {
    Arguments arguments{R"(C:\Program Files\x.exe)", "a b", "", R"(c")"};
    ZEXPECT(joined(arguments) == R"("C:\Program Files\x.exe" "a b" "" "c\"")");
    ZEXPECT(joined(Arguments{R"(C:\x.exe)"}) == R"(C:\x.exe)");
    ZEXPECT(joined(Arguments{}) == "");
}

ZEST_CASE(quote_and_join_write_over_what_the_buffer_held) {
    small_string<8> out("previous");
    quote_argument(out, "a b c d e");
    ZEXPECT(std::string_view(out) == R"("a b c d e")");
    join_command_line(out, {R"(C:\x.exe)", "a b"});
    ZEXPECT(std::string_view(out) == R"(C:\x.exe "a b")");
}

ZEST_CASE(split_is_an_input_view) {
    ZSTATIC_EXPECT(std::ranges::view<split_command_line_view>);
    ZSTATIC_EXPECT(std::ranges::input_range<split_command_line_view>);
}

// An argument views its iterator: a copy keeps its own.
ZEST_CASE(split_iterator_copies_keep_their_argument) {
    auto view = split_command_line(R"(p "a b" c)");
    auto it = view.begin();
    ++it;
    auto copy = it;
    ++it;
    ZEXPECT(*copy == "a b");
    ZEXPECT(*it == "c");
}

ZEST_CASE(split_reads_the_program_by_its_own_rule) {
    // Backslashes are literal in it, and every quote toggles.
    ZEXPECT(split(R"("C:\dir\"x y)") == Arguments{R"(C:\dir\x)", "y"});
    ZEXPECT(split(R"(C:\a" "b\c.exe arg)") == Arguments{R"(C:\a b\c.exe)", "arg"});
    ZEXPECT(split(R"("" a)") == Arguments{"", "a"});
}

ZEST_CASE(split_separates_on_blanks_outside_quotes) {
    ZEXPECT(split("p  a\t b ") == Arguments{"p", "a", "b"});
    ZEXPECT(split(R"(p "a b" c)") == Arguments{"p", "a b", "c"});
    ZEXPECT(split(R"(p a"b c"d)") == Arguments{"p", "ab cd"});
}

ZEST_CASE(split_keeps_empty_quoted_arguments) {
    ZEXPECT(split(R"(p "" "")") == Arguments{"p", "", ""});
    // Quotes still open at the end.
    ZEXPECT(split(R"(p ")") == Arguments{"p", ""});
    ZEXPECT(split(R"(p "abc)") == Arguments{"p", "abc"});
}

ZEST_CASE(split_halves_backslashes_before_a_quote) {
    ZEXPECT(split(R"(p a\\"b c")") == Arguments{"p", R"(a\b c)"});
    ZEXPECT(split(R"(p a\\\"b)") == Arguments{"p", R"(a\"b)"});
    ZEXPECT(split(R"(p a\\b)") == Arguments{"p", R"(a\\b)"});
    ZEXPECT(split(R"(p "C:\dir\\")") == Arguments{"p", R"(C:\dir\)"});
}

ZEST_CASE(split_reads_two_quotes_in_quotes_as_one) {
    ZEXPECT(split(R"(p "a""b")") == Arguments{"p", R"(a"b)"});
    ZEXPECT(split(R"(p "a""")") == Arguments{"p", R"(a")"});
}

// The rule of the CRT since 2008, which an older one read as `a"b` and ` c`.
ZEST_CASE(split_reads_two_quotes_in_quotes_and_stays_in_them) {
    ZEXPECT(split(R"(p "a""b c")") == Arguments{"p", R"(a"b c)"});
}

ZEST_CASE(split_reads_an_empty_program_before_a_blank) {
    ZEXPECT(split(" p") == Arguments{"", "p"});
    ZEXPECT(split("   ") == Arguments{""});
    // Quotes still open run to the end.
    ZEXPECT(split(R"("C:\a b)") == Arguments{R"(C:\a b)"});
}

ZEST_CASE(split_keeps_backslashes_at_the_end) {
    ZEXPECT(split(R"(p a\\)") == Arguments{"p", R"(a\\)"});
}

ZEST_CASE(split_expands_no_wildcard) {
    ZEXPECT(split("p *.cpp 'foo*' ?") == Arguments{"p", "*.cpp", "'foo*'", "?"});
}

ZEST_CASE(split_of_an_empty_line_has_no_arguments) {
    ZEXPECT(split("").empty());
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
    ZEXPECT(split(joined(arguments)) == arguments);
}

};  // ZEST_SUITE(sys_windows_command_line)

}  // namespace

}  // namespace kota::sys::windows
