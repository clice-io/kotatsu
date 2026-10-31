#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/support/string_list_ref.h"
#include "kotatsu/sys/cmd.h"

namespace kotatsu::sys::cmd {

namespace {

using namespace std::literals;

using Arguments = std::vector<std::string>;

constexpr auto windows = Quoting::Windows;
constexpr auto posix = Quoting::POSIX;

/// The command line join() writes of `arguments`.
std::string joined(string_list_ref arguments, Quoting quoting) {
    std::string out;
    join(out, arguments, quoting);
    return out;
}

/// The arguments split() reads in `line`.
Arguments split_all(std::string_view line, Quoting quoting) {
    Arguments arguments;
    for(auto argument: cmd::split(line, quoting)) {
        arguments.emplace_back(argument);
    }
    return arguments;
}

ZEST_SUITE(sys_cmd_join) {

ZEST_CASE(windows_leaves_a_plain_argument) {
    ZEXPECT(joined({"p", "plain"}, windows) == "p plain");
    ZEXPECT(joined({"p", R"(C:\dir\file)"}, windows) == R"(p C:\dir\file)");
    ZEXPECT(joined({"p", "caf\xC3\xA9"}, windows) == "p caf\xC3\xA9");
}

ZEST_CASE(windows_quotes_an_empty_or_blank_argument) {
    ZEXPECT(joined({"p", ""}, windows) == R"(p "")");
    ZEXPECT(joined({"p", "a b"}, windows) == R"(p "a b")");
    ZEXPECT(joined({"p", "a\tb"}, windows) == "p \"a\tb\"");
    ZEXPECT(joined({"p", "a\nb"}, windows) == "p \"a\nb\"");
    ZEXPECT(joined({"p", "a\vb"}, windows) == "p \"a\vb\"");
}

ZEST_CASE(windows_escapes_quotes_and_the_backslashes_before_them) {
    ZEXPECT(joined({"p", R"(a"b)"}, windows) == R"(p "a\"b")");
    ZEXPECT(joined({"p", R"(a\"b)"}, windows) == R"(p "a\\\"b")");
    // Before the closing quote, backslashes double.
    ZEXPECT(joined({"p", R"(C:\dir with space\)"}, windows) == R"(p "C:\dir with space\\")");
    // Elsewhere they stay as they are.
    ZEXPECT(joined({"p", R"(a\\b c)"}, windows) == R"(p "a\\b c")");
}

ZEST_CASE(windows_writes_the_program_without_escapes) {
    ZEXPECT(joined({R"(C:\Program Files\x.exe)", "a b", "", R"(c")"}, windows) ==
            R"("C:\Program Files\x.exe" "a b" "" "c\"")");
    ZEXPECT(joined({R"(C:\x.exe)"}, windows) == R"(C:\x.exe)");
    ZEXPECT(joined({R"(C:\a b\)"}, windows) == R"("C:\a b\")");
    ZEXPECT(joined({""}, windows) == R"("")");
}

ZEST_CASE(posix_leaves_a_plain_argument) {
    ZEXPECT(joined({"-DA=1", "a/b.c", "@rsp", "%x+y,z:w_v-u"}, posix) ==
            "-DA=1 a/b.c @rsp %x+y,z:w_v-u");
    // Bytes past ASCII are UTF-8, which sh reads as they are.
    ZEXPECT(joined({"caf\xC3\xA9"}, posix) == "caf\xC3\xA9");
}

ZEST_CASE(posix_escapes_what_sh_reads) {
    ZEXPECT(joined({"a b", "a'b", "$HOME", "*", "~", R"(a"b\c)"}, posix) ==
            R"(a\ b a\'b \$HOME \* \~ a\"b\\c)");
    ZEXPECT(joined({"a\tb\rc"}, posix) == "a\\\tb\\\rc");
}

ZEST_CASE(posix_writes_an_empty_argument_in_quotes) {
    ZEXPECT(joined({"a", "", "b"}, posix) == "a '' b");
}

// sh would read a newline after a backslash as a line continuation.
ZEST_CASE(posix_writes_a_newline_in_single_quotes) {
    ZEXPECT(joined({"a\nb"}, posix) == "a'\n'b");
}

ZEST_CASE(of_no_arguments_is_empty) {
    ZEXPECT(joined(Arguments{}, windows) == "");
    ZEXPECT(joined(Arguments{}, posix) == "");
}

ZEST_CASE(writes_over_what_the_buffer_held) {
    small_string<8> out("previous");
    join(out, {"p", "a b c d e"}, windows);
    ZEXPECT(std::string_view(out) == R"(p "a b c d e")");
    join(out, {"a b"}, posix);
    ZEXPECT(std::string_view(out) == R"(a\ b)");
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
        "'a'",
        "\t\r\n\v",
        "a\0b"s,
        "caf\xC3\xA9 \xF0\x9F\x98\x80",
        "*",
    };
    ZEXPECT(split_all(joined(arguments, windows), windows) == arguments);
    ZEXPECT(split_all(joined(arguments, posix), posix) == arguments);
}

// Arguments of every character that either quoting reads specially, drawn
// at random; a Windows program holds no quote.
ZEST_CASE(split_reads_back_what_join_writes_of_any_arguments) {
    constexpr std::uint32_t seed = 20261031;
    constexpr auto alphabet = "ab \t\r\n\v\"'\\@$\0\xC3\xA9"sv;
    std::mt19937 random(seed);
    auto draw = [&](std::size_t most) {
        return std::uniform_int_distribution<std::size_t>(0, most)(random);
    };
    for(int run = 0; run < 2000; ++run) {
        Arguments arguments(draw(4) + 1);
        for(auto& argument: arguments) {
            for(auto length = draw(8); length != 0; --length) {
                argument.push_back(alphabet[draw(alphabet.size() - 1)]);
            }
        }
        ZEST_CONTEXT("seed {}, run {}", seed, run);
        ZASSERT(split_all(joined(arguments, posix), posix) == arguments);
        std::erase(arguments[0], '"');
        ZASSERT(split_all(joined(arguments, windows), windows) == arguments);
    }
}

};  // ZEST_SUITE(sys_cmd_join)

}  // namespace

}  // namespace kotatsu::sys::cmd
