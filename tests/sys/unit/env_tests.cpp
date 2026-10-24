#include <string>
#include <system_error>
#include <vector>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/env.h"

namespace kota::sys {

namespace {

using Paths = std::vector<std::string>;
using fs::path::Style;

ZEST_SUITE(sys_env) {

ZEST_CASE(split_paths_separates_posix_lists_on_colons) {
    ZEXPECT(split_paths("/usr/bin:/bin", Style::Posix) == Paths{"/usr/bin", "/bin"});
    // Empty entries are the current directory to execvp.
    ZEXPECT(split_paths(":/bin:", Style::Posix) == Paths{"", "/bin", ""});
    ZEXPECT(split_paths("", Style::Posix) == Paths{""});
    // A quote is nothing special.
    ZEXPECT(split_paths(R"("a:b")", Style::Posix) == Paths{R"("a)", R"(b")"});
}

ZEST_CASE(split_paths_separates_windows_lists_on_semicolons_outside_quotes) {
    ZEXPECT(split_paths(R"(C:\a;D:\b)", Style::Windows) == Paths{R"(C:\a)", R"(D:\b)"});
    ZEXPECT(split_paths(R"(C:\a;"C:\semi;colon";C:\b)", Style::Windows) ==
            Paths{R"(C:\a)", R"(C:\semi;colon)", R"(C:\b)"});
    // A quote opens and closes anywhere.
    ZEXPECT(split_paths(R"(C:\so"me;di"r)", Style::Windows) == Paths{R"(C:\some;dir)"});
    // A lone one runs to the end.
    ZEXPECT(split_paths(R"(";C:\a)", Style::Windows) == Paths{R"(;C:\a)"});
    ZEXPECT(split_paths(";", Style::Windows) == Paths{"", ""});
    ZEXPECT(split_paths("", Style::Windows).empty());
}

ZEST_CASE(join_paths_reads_back_with_split_paths) {
    Paths posix{"/usr/bin", "", "relative"};
    auto joined = join_paths(posix, Style::Posix);
    ZASSERT(joined.has_value());
    ZEXPECT(*joined == "/usr/bin::relative");
    ZEXPECT(split_paths(*joined, Style::Posix) == posix);

    Paths windows{R"(C:\a)", R"(C:\semi;colon)", ""};
    joined = join_paths(windows, Style::Windows);
    ZASSERT(joined.has_value());
    ZEXPECT(*joined == R"(C:\a;"C:\semi;colon";)");
    ZEXPECT(split_paths(*joined, Style::Windows) == windows);
}

// The two lists an empty string could be: none in the POSIX style, which has
// no list of no entries, and a lone empty entry in the Windows style, which
// is quoted.
ZEST_CASE(join_paths_of_the_empty_cases) {
    ZEXPECT(join_paths(Paths{}, Style::Posix) == "");
    ZEXPECT(split_paths("", Style::Posix) == Paths{""});
    ZEXPECT(join_paths(Paths{""}, Style::Posix) == "");
    ZEXPECT(join_paths(Paths{}, Style::Windows) == "");
    ZEXPECT(join_paths(Paths{""}, Style::Windows) == R"("")");
    ZEXPECT(split_paths(R"("")", Style::Windows) == Paths{""});
}

ZEST_CASE(join_paths_of_an_entry_no_list_holds_fails) {
    auto posix = join_paths(Paths{"/a:b"}, Style::Posix);
    ZASSERT(!posix.has_value());
    ZEXPECT(posix.error() == std::errc::invalid_argument);
    auto windows = join_paths(Paths{R"(C:\a"b)"}, Style::Windows);
    ZASSERT(!windows.has_value());
    ZEXPECT(windows.error() == std::errc::invalid_argument);
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
