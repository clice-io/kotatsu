#include <concepts>
#include <functional>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kotatsu/zest/zest.h"
#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/support/string_list_ref.h"
#include "kotatsu/sys/cmd.h"

namespace kotatsu::sys::cmd {

namespace {

using namespace std::literals;

using Arguments = std::vector<std::string>;

constexpr auto windows = Quoting::Windows;
constexpr auto posix = Quoting::POSIX;

/// Response files in memory, by name, which records the names it is asked
/// to read.
struct Files {
    std::map<std::string, std::string, std::less<>> texts;

    std::vector<std::string> asked;

    std::error_code operator()(buffer_ref out, std::string_view name) {
        asked.emplace_back(name);
        auto it = texts.find(name);
        if(it == texts.end()) {
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }
        out.assign(it->second);
        return {};
    }
};

/// The arguments expand() gives of `arguments` with `files`.
Arguments expanded(string_list_ref arguments, Files& files, Quoting quoting = posix) {
    Arguments result;
    for(auto argument: expand(arguments, quoting, files)) {
        result.emplace_back(argument);
    }
    return result;
}

/// The arguments expand() reads in a response file of `text`.
Arguments read(std::string_view text, Quoting quoting) {
    Files files{.texts = {{"file", std::string(text)}}};
    return expanded({"@file"}, files, quoting);
}

ZEST_SUITE(sys_cmd_expand) {

ZEST_CASE(is_an_input_view_its_iterator_moved_only) {
    ZSTATIC_EXPECT(std::ranges::view<expand_view>);
    ZSTATIC_EXPECT(std::ranges::input_range<expand_view>);
    ZSTATIC_EXPECT(!std::copyable<expand_view::iterator>);
}

ZEST_CASE(leaves_a_command_without_response_files_as_it_is) {
    Files files;
    Arguments command{"clang", "-c", "a.c"};
    ZEXPECT(expanded(command, files) == command);
    ZEXPECT(files.asked.empty());
}

// The iterator gives the command's own arguments as they are.
ZEST_CASE(views_the_commands_own_arguments) {
    Files files;
    Arguments command{"clang", "a b"};
    auto view = expand(command, posix, files);
    auto it = view.begin();
    ZEXPECT((*it).data() == command[0].data());
    ++it;
    ZEXPECT((*it).data() == command[1].data());
}

ZEST_CASE(replaces_a_response_file_with_its_arguments) {
    Files files{.texts = {{"args", "-DA=1 'b c'\n-O2\n"}}};
    ZEXPECT(expanded({"clang", "@args", "x.c"}, files) ==
            Arguments{"clang", "-DA=1", "b c", "-O2", "x.c"});
    ZEXPECT(files.asked == Arguments{"args"});
}

ZEST_CASE(reads_response_files_in_response_files) {
    Files files{
        .texts = {{"a", "1 @b 4 @c"}, {"b", "2 @c 3"}, {"c", "c"}}
    };
    ZEXPECT(expanded({"@a", "5"}, files) == Arguments{"1", "2", "c", "3", "4", "c", "5"});
    ZEXPECT(files.asked == Arguments{"a", "b", "c", "c"});
}

ZEST_CASE(leaves_an_unreadable_response_file_as_written) {
    Files files;
    ZEXPECT(expanded({"@missing", "x"}, files) == Arguments{"@missing", "x"});
    ZEXPECT(files.asked == Arguments{"missing"});
}

ZEST_CASE(reads_no_arguments_in_an_empty_response_file) {
    Files files{
        .texts = {{"empty", ""}, {"blank", " \n\t\r\n"}}
    };
    ZEXPECT(expanded({"a", "@empty", "@blank", "b"}, files) == Arguments{"a", "b"});
    ZEXPECT(expanded({"@empty"}, files).empty());
}

ZEST_CASE(reads_eight_files_deep) {
    Files files{.texts = {{"loop", "x @loop"}}};
    Arguments arguments(8, "x");
    arguments.emplace_back("@loop");
    ZEXPECT(expanded({"@loop"}, files) == arguments);
    ZEXPECT(files.asked.size() == 8);
}

// What follows `@`, unquoted when a file holds it.
ZEST_CASE(asks_for_a_file_by_the_name_the_argument_gives) {
    Files files{.texts = {{"list", R"(@"a b" @)"}}};
    ZEXPECT(expanded({"@list"}, files) == Arguments{"@a b", "@"});
    ZEXPECT(files.asked == Arguments{"list", "a b", ""});
}

ZEST_CASE(drops_a_utf8_byte_order_mark) {
    ZEXPECT(read("\xEF\xBB\xBF-a -b", posix) == Arguments{"-a", "-b"});
}

ZEST_CASE(converts_utf16_with_a_byte_order_mark) {
    ZEXPECT(read("\xFF\xFE-\0a\0 \0\xE9\0\x3D\xD8\x00\xDE"sv, posix) ==
            Arguments{"-a", "\xC3\xA9\xF0\x9F\x98\x80"});
    ZEXPECT(read("\xFE\xFF\0-\0a\0 \0\xE9"sv, windows) == Arguments{"-a", "\xC3\xA9"});
}

ZEST_CASE(leaves_utf16_of_an_odd_number_of_bytes_as_written) {
    Files files{.texts = {{"odd", "\xFF\xFE-\0a"s}}};
    ZEXPECT(expanded({"@odd"}, files) == Arguments{"@odd"});
}

// LLVM's cases of cl::TokenizeWindowsCommandLine, by which clang-cl reads a
// response file, from llvm/unittests/Support/CommandLineTest.cpp; its line
// ends, which it marks for clang-cl, left out.
ZEST_CASE(reads_a_windows_file_as_llvms_cases) {
    ZEXPECT(read(R"(a\b c\\d e\\"f g" h\"i j\\\"k "lmn" o pqr "st \"u" \v)", windows) ==
            Arguments{R"(a\b)",
                      R"(c\\d)",
                      R"(e\f g)",
                      R"(h"i)",
                      R"(j\"k)",
                      "lmn",
                      "o",
                      "pqr",
                      R"(st "u)",
                      R"(\v)"});
    ZEXPECT(read(R"(clang -c -DFOO="""ABC""" x.cpp)", windows) ==
            Arguments{"clang", "-c", R"(-DFOO="ABC")", "x.cpp"});
    ZEXPECT(read("clang -Xclang foo\n\nfoo\"bar\"baz\n x.cpp\n", windows) ==
            Arguments{"clang", "-Xclang", "foo", "foobarbaz", "x.cpp"});
    ZEXPECT(read("a b c d ", windows) == Arguments{"a", "b", "c", "d"});
    ZEXPECT(read(R"(a b c d "")", windows) == Arguments{"a", "b", "c", "d", ""});
    ZEXPECT(read(R"(a b c d ")", windows) == Arguments{"a", "b", "c", "d", ""});
    ZEXPECT(read(R"(a b c d "text)", windows) == Arguments{"a", "b", "c", "d", "text"});
}

// Unlike a Windows command line, whose program is read without escapes and
// may be empty.
ZEST_CASE(reads_no_program_in_a_windows_file) {
    ZEXPECT(read(R"("C:\dir\"x y)", windows) == Arguments{R"(C:\dir"x y)"});
    ZEXPECT(read(R"("a""b" c)", windows) == Arguments{R"(a"b)", "c"});
    ZEXPECT(read(" \t\r\n a b", windows) == Arguments{"a", "b"});
}

ZEST_CASE(separates_at_line_ends_and_nuls_in_a_windows_file) {
    ZEXPECT(read("a\r\nb\r\n", windows) == Arguments{"a", "b"});
    ZEXPECT(read("a\nb\rc", windows) == Arguments{"a", "b", "c"});
    ZEXPECT(read("a\0b\0"sv, windows) == Arguments{"a", "b"});
    // In quotes they separate nothing.
    ZEXPECT(read("\"a\nb\r\n\" c", windows) == Arguments{"a\nb\r\n", "c"});
    ZEXPECT(read("\"a\0b\" c"sv, windows) == (Arguments{"a\0b"s, "c"}));
}

ZEST_CASE(reads_a_posix_file_as_split_reads_a_line) {
    ZEXPECT(read("-I'a b'\n\"-DX=\\\"y\\\"\"\r\n'c\\d'", posix) ==
            Arguments{"-Ia b", R"(-DX="y")", R"(c\d)"});
}

};  // ZEST_SUITE(sys_cmd_expand)

}  // namespace

}  // namespace kotatsu::sys::cmd
