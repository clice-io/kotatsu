#include <concepts>
#include <functional>
#include <iterator>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "sys/harness/cmd.h"
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
    explicit Files(std::map<std::string, std::string, std::less<>> texts = {}) :
        texts(std::move(texts)) {}

    std::map<std::string, std::string, std::less<>> texts;

    std::vector<std::string> asked;

    std::error_code operator()(buffer_ref out, std::string_view name) {
        ZEXPECT(out.empty());
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

using test::read_response_file;

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
    Files files({
        {"args", "-DA=1 'b c'\n-O2\n"}
    });
    ZEXPECT(expanded({"clang", "@args", "x.c"}, files) ==
            Arguments{"clang", "-DA=1", "b c", "-O2", "x.c"});
    ZEXPECT(files.asked == Arguments{"args"});
}

ZEST_CASE(reads_response_files_in_response_files) {
    Files files({
        {"a", "1 @b 4 @c"},
        {"b", "2 @c 3"   },
        {"c", "c"        }
    });
    ZEXPECT(expanded({"@a", "5"}, files) == Arguments{"1", "2", "c", "3", "4", "c", "5"});
    ZEXPECT(files.asked == Arguments{"a", "b", "c", "c"});
}

ZEST_CASE(leaves_an_unreadable_response_file_as_written) {
    Files files;
    ZEXPECT(expanded({"@missing", "x"}, files) == Arguments{"@missing", "x"});
    ZEXPECT(files.asked == Arguments{"missing"});
}

ZEST_CASE(reads_no_arguments_in_an_empty_response_file) {
    Files files({
        {"empty", ""         },
        {"blank", " \n\t\r\n"}
    });
    ZEXPECT(expanded({"a", "@empty", "@blank", "b"}, files) == Arguments{"a", "b"});
    ZEXPECT(expanded({"@empty"}, files).empty());
}

ZEST_CASE(reads_eight_files_deep) {
    Files files({
        {"loop", "x @loop"}
    });
    Arguments arguments(8, "x");
    arguments.emplace_back("@loop");
    ZEXPECT(expanded({"@loop"}, files) == arguments);
    ZEXPECT(files.asked.size() == 8);
    // As deep again after the files it left.
    files.asked.clear();
    auto twice = arguments;
    twice.insert(twice.end(), arguments.begin(), arguments.end());
    ZEXPECT(expanded({"@loop", "@loop"}, files) == twice);
    ZEXPECT(files.asked.size() == 16);
}

// Eight deep, not eight in all.
ZEST_CASE(reads_any_number_of_files_side_by_side) {
    Files files({
        {"leaf", "x"}
    });
    ZEXPECT(expanded(Arguments(10, "@leaf"), files) == Arguments(10, "x"));
    ZEXPECT(files.asked.size() == 10);
}

// The file's place, which its arguments view, moves with the iterator.
ZEST_CASE(a_moved_iterator_keeps_its_argument) {
    Files files({
        {"args", "first 'second' third"}
    });
    std::string_view command[] = {"@args"};
    auto view = expand(command, posix, files);
    auto it = view.begin();
    ++it;
    ZASSERT(it != std::default_sentinel);
    auto moved = std::move(it);
    ZEXPECT(*moved == "second");
    ++moved;
    ZASSERT(moved != std::default_sentinel);
    ZEXPECT(*moved == "third");
}

ZEST_CASE(leaves_a_file_whose_reader_fails_as_written) {
    auto read = [](buffer_ref out, std::string_view) {
        out.assign("written before the error");
        return std::make_error_code(std::errc::io_error);
    };
    std::string_view command[] = {"a", "@file"};
    Arguments arguments;
    for(auto argument: expand(command, posix, read)) {
        arguments.emplace_back(argument);
    }
    ZEXPECT(arguments == Arguments{"a", "@file"});
}

// What follows `@`, unquoted when a file holds it.
ZEST_CASE(asks_for_a_file_by_the_name_the_argument_gives) {
    Files files({
        {"list", R"(@"a b" @)"}
    });
    ZEXPECT(expanded({"@list"}, files) == Arguments{"@a b", "@"});
    ZEXPECT(files.asked == Arguments{"list", "a b", ""});
}

ZEST_CASE(drops_a_utf8_byte_order_mark) {
    ZEXPECT(read_response_file("\xEF\xBB\xBF-a -b", posix) == Arguments{"-a", "-b"});
}

ZEST_CASE(converts_utf16_with_a_byte_order_mark) {
    ZEXPECT(read_response_file("\xFF\xFE-\0a\0 \0\xE9\0\x3D\xD8\x00\xDE"sv, posix) ==
            Arguments{"-a", "\xC3\xA9\xF0\x9F\x98\x80"});
    ZEXPECT(read_response_file("\xFE\xFF\0-\0a\0 \0\xE9"sv, windows) ==
            Arguments{"-a", "\xC3\xA9"});
}

ZEST_CASE(leaves_utf16_of_an_odd_number_of_bytes_as_written) {
    Files files({
        {"little", "\xFF\xFE-\0a"s},
        {"big",    "\xFE\xFF\0-a"s}
    });
    ZEXPECT(expanded({"@little", "@big"}, files) == Arguments{"@little", "@big"});
}

// LLVM's cases of cl::TokenizeWindowsCommandLine, by which clang-cl reads a
// response file, from llvm/unittests/Support/CommandLineTest.cpp; its line
// ends, which it marks for clang-cl, left out.
ZEST_CASE(reads_a_windows_file_as_llvms_cases) {
    ZEXPECT(read_response_file(R"(a\b c\\d e\\"f g" h\"i j\\\"k "lmn" o pqr "st \"u" \v)",
                               windows) == Arguments{R"(a\b)",
                                                     R"(c\\d)",
                                                     R"(e\f g)",
                                                     R"(h"i)",
                                                     R"(j\"k)",
                                                     "lmn",
                                                     "o",
                                                     "pqr",
                                                     R"(st "u)",
                                                     R"(\v)"});
    ZEXPECT(read_response_file(R"(clang -c -DFOO="""ABC""" x.cpp)", windows) ==
            Arguments{"clang", "-c", R"(-DFOO="ABC")", "x.cpp"});
    ZEXPECT(read_response_file("clang -Xclang foo\n\nfoo\"bar\"baz\n x.cpp\n", windows) ==
            Arguments{"clang", "-Xclang", "foo", "foobarbaz", "x.cpp"});
    ZEXPECT(read_response_file("a b c d ", windows) == Arguments{"a", "b", "c", "d"});
    ZEXPECT(read_response_file(R"(a b c d "")", windows) == Arguments{"a", "b", "c", "d", ""});
    ZEXPECT(read_response_file(R"(a b c d ")", windows) == Arguments{"a", "b", "c", "d", ""});
    ZEXPECT(read_response_file(R"(a b c d "text)", windows) ==
            Arguments{"a", "b", "c", "d", "text"});
}

// Unlike a Windows command line, whose program is read without escapes and
// may be empty.
ZEST_CASE(reads_no_program_in_a_windows_file) {
    ZEXPECT(read_response_file(R"("C:\dir\"x y)", windows) == Arguments{R"(C:\dir"x y)"});
    ZEXPECT(read_response_file(R"("a""b" c)", windows) == Arguments{R"(a"b)", "c"});
    ZEXPECT(read_response_file(" \t\r\n a b", windows) == Arguments{"a", "b"});
}

ZEST_CASE(separates_at_line_ends_and_nuls_in_a_windows_file) {
    ZEXPECT(read_response_file("a\r\nb\r\n", windows) == Arguments{"a", "b"});
    ZEXPECT(read_response_file("a\nb\rc", windows) == Arguments{"a", "b", "c"});
    ZEXPECT(read_response_file("a\0b\0"sv, windows) == Arguments{"a", "b"});
    // In quotes they separate nothing.
    ZEXPECT(read_response_file("\"a\nb\r\n\" c", windows) == Arguments{"a\nb\r\n", "c"});
    ZEXPECT(read_response_file("\"a\0b\" c"sv, windows) == (Arguments{"a\0b"s, "c"}));
}

ZEST_CASE(reads_a_posix_file_as_split_reads_a_line) {
    ZEXPECT(read_response_file("-I'a b'\n\"-DX=\\\"y\\\"\"\r\n'c\\d'", posix) ==
            Arguments{"-Ia b", R"(-DX="y")", R"(c\d)"});
}

};  // ZEST_SUITE(sys_cmd_expand)

}  // namespace

}  // namespace kotatsu::sys::cmd
