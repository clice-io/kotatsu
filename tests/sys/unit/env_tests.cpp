#include <expected>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/sys/env.h"

namespace kota::sys {

namespace {

using Paths = std::vector<std::string>;
using fs::path::Style;

/// The entries `view` gives, read to its end.
Paths collect(const split_paths_view& view) {
    Paths paths;
    for(auto entry: view) {
        paths.emplace_back(entry);
    }
    return paths;
}

/// The entries of `list`.
Paths entries(std::string_view list, Style style) {
    return collect(split_paths(list, style));
}

/// The list join_paths writes of `paths`, or its error.
std::expected<std::string, std::error_code> joined(string_list_ref paths, Style style) {
    std::string out;
    if(auto error = join_paths(out, paths, style)) {
        return std::unexpected(error);
    }
    return out;
}

ZEST_SUITE(sys_env) {

ZEST_CASE(split_paths_is_an_input_view) {
    ZSTATIC_EXPECT(std::ranges::view<split_paths_view>);
    ZSTATIC_EXPECT(std::ranges::input_range<split_paths_view>);
}

ZEST_CASE(split_paths_separates_posix_lists_on_colons) {
    ZEXPECT(entries("/usr/bin:/bin", Style::Posix) == Paths{"/usr/bin", "/bin"});
    // Empty entries are the current directory to execvp.
    ZEXPECT(entries(":/bin:", Style::Posix) == Paths{"", "/bin", ""});
    ZEXPECT(entries("", Style::Posix) == Paths{""});
    // A quote is nothing special.
    ZEXPECT(entries(R"("a:b")", Style::Posix) == Paths{R"("a)", R"(b")"});
}

ZEST_CASE(split_paths_separates_windows_lists_on_semicolons_outside_quotes) {
    ZEXPECT(entries(R"(C:\a;D:\b)", Style::Windows) == Paths{R"(C:\a)", R"(D:\b)"});
    ZEXPECT(entries(R"(C:\a;"C:\semi;colon";C:\b)", Style::Windows) ==
            Paths{R"(C:\a)", R"(C:\semi;colon)", R"(C:\b)"});
    // A quote opens and closes anywhere.
    ZEXPECT(entries(R"(C:\so"me;di"r)", Style::Windows) == Paths{R"(C:\some;dir)"});
    // A lone one runs to the end.
    ZEXPECT(entries(R"(";C:\a)", Style::Windows) == Paths{R"(;C:\a)"});
    ZEXPECT(entries(";", Style::Windows) == Paths{"", ""});
    ZEXPECT(entries("", Style::Windows).empty());
}

// Each iteration reads the list anew.
ZEST_CASE(split_paths_gives_the_same_entries_each_time) {
    auto view = split_paths(R"(C:\a;"C:\semi;colon";;C:\b)", Style::Windows);
    auto first = collect(view);
    ZEXPECT(first == Paths{R"(C:\a)", R"(C:\semi;colon)", "", R"(C:\b)"});
    ZEXPECT(collect(view) == first);
}

// An entry views the list, but for one its quotes are left out of, which
// views the iterator.
ZEST_CASE(split_paths_entries_view_the_list_unless_unquoted) {
    std::string_view list = R"(C:\a;"C:\b";C:\c)";
    auto view = split_paths(list, Style::Windows);
    auto it = view.begin();
    ZEXPECT(*it == R"(C:\a)");
    ZEXPECT(((*it).data() == list.data()));
    ++it;
    ZEXPECT(*it == R"(C:\b)");
    ++it;
    ZEXPECT(*it == R"(C:\c)");
    ZEXPECT(((*it).data() == list.data() + list.find(R"(C:\c)")));
    ++it;
    ZEXPECT((it == view.end()));
}

// A copy of an iterator keeps its own entry, unquoted or not, and so does
// one an adaptor moves.
ZEST_CASE(split_paths_iterator_copies_keep_their_entry) {
    auto view = split_paths(R"("C:\b";"C:\d")", Style::Windows);
    auto it = view.begin();
    auto copy = it;
    ++it;
    ZEXPECT(*copy == R"(C:\b)");
    ZEXPECT(*it == R"(C:\d)");
    auto sizes = view | std::views::transform([](std::string_view entry) { return entry.size(); });
    ZEXPECT(*sizes.begin() == 4U);
}

ZEST_CASE(join_paths_reads_back_with_split_paths) {
    Paths posix{"/usr/bin", "", "relative"};
    auto list = joined(posix, Style::Posix);
    ZASSERT(list.has_value());
    ZEXPECT(*list == "/usr/bin::relative");
    ZEXPECT(entries(*list, Style::Posix) == posix);

    Paths windows{R"(C:\a)", R"(C:\semi;colon)", ""};
    list = joined(windows, Style::Windows);
    ZASSERT(list.has_value());
    ZEXPECT(*list == R"(C:\a;"C:\semi;colon";)");
    ZEXPECT(entries(*list, Style::Windows) == windows);
}

// Any list of strings, into any buffer, over what it held.
ZEST_CASE(join_paths_writes_over_what_the_buffer_held) {
    std::vector<std::string_view> views{"/usr/bin", "/bin"};
    small_string<8> out("some old text");
    ZEXPECT(!join_paths(out, views, Style::Posix));
    ZEXPECT(std::string_view(out) == "/usr/bin:/bin");
    ZEXPECT(!join_paths(out, {R"(C:\a)", R"(C:\semi;colon)"}, Style::Windows));
    ZEXPECT(std::string_view(out) == R"(C:\a;"C:\semi;colon")");
}

// The two lists an empty string could be: none in the POSIX style, which has
// no list of no entries, and a lone empty entry in the Windows style, which
// is quoted.
ZEST_CASE(join_paths_of_the_empty_cases) {
    ZEXPECT(joined(Paths{}, Style::Posix) == "");
    ZEXPECT(entries("", Style::Posix) == Paths{""});
    ZEXPECT(joined(Paths{""}, Style::Posix) == "");
    ZEXPECT(joined(Paths{}, Style::Windows) == "");
    ZEXPECT(joined(Paths{""}, Style::Windows) == R"("")");
    ZEXPECT(entries(R"("")", Style::Windows) == Paths{""});
}

ZEST_CASE(join_paths_of_an_entry_no_list_holds_fails) {
    std::string out;
    ZEXPECT(join_paths(out, {"/a:b"}, Style::Posix) == std::errc::invalid_argument);
    ZEXPECT(join_paths(out, {R"(C:\a"b)"}, Style::Windows) == std::errc::invalid_argument);
}

ZEST_CASE(env_name_equal_ignores_case_on_windows) {
    ZEXPECT(env_name_equal("PATH", "PATH"));
    ZEXPECT(!env_name_equal("PATH", "PATHEXT"));
#ifdef _WIN32
    ZEXPECT(env_name_equal("Path", "PATH"));
    // Past ASCII too, as Windows folds case.
    ZEXPECT(env_name_equal("\xC3\xA9T\xC3\xA9", "\xC3\x89T\xC3\x89"));
#else
    ZEXPECT(!env_name_equal("Path", "PATH"));
#endif
}

};  // ZEST_SUITE(sys_env)

}  // namespace

}  // namespace kota::sys
