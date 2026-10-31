#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "fs/harness/files.h"
#include "sys/harness/program.h"
#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/path.h"
#include "kotatsu/sys/process.h"
#include "kotatsu/sys/windows.h"

namespace kotatsu::sys::windows {

namespace {

using namespace std::literals;

#ifdef _WIN32
/// The file find_create_process_program finds for `name` in `directories`,
/// or its error.
std::expected<std::string, std::error_code> found(std::string_view name,
                                                  string_list_ref directories) {
    std::string out;
    if(auto error = find_create_process_program(out, name, directories)) {
        return std::unexpected(error);
    }
    return out;
}

// CreateProcess's search, which Windows alone has.
ZEST_SUITE(sys_windows_program) {

test::TempDir dir;

ZEST_CASE(create_process_directories_start_with_this_programs_directory) {
    auto directories = create_process_directories();
    ZASSERT(directories.has_value());
    ZASSERT(directories->size() >= 5U);
    std::string executable;
    ZASSERT(!executable_path(executable));
    ZEXPECT((*directories)[0] == fs::path::parent(executable));
}

ZEST_CASE(find_create_process_program_appends_exe_to_a_bare_name) {
    std::vector<std::string> directories = {dir.root()};
    auto program = test::make_program(dir, "", "tool.exe");
    test::make_program(dir, "", "tool.bar.exe");
    ZEXPECT(found("tool", directories) == program);
    // A name with an extension stays as it is.
    auto result = found("tool.bar", directories);
    ZASSERT(!result.has_value());
    ZEXPECT(result.error() == std::errc::no_such_file_or_directory);
}

// A bare name is looked for in the directories given alone, PATH's or not.
ZEST_CASE(find_create_process_program_in_no_directory_fails) {
    auto result = found("cmd", {});
    ZASSERT(!result.has_value());
    ZEXPECT(result.error() == std::errc::no_such_file_or_directory);
}

// The first, for a message to name.
ZEST_CASE(find_create_process_program_finding_only_a_directory_names_it) {
    std::filesystem::create_directories(dir.path / "first" / "tool.exe");
    std::vector<std::string> directories = {dir.file("first"), dir.file("second")};
    std::string out;
    ZEXPECT(find_create_process_program(out, "tool", directories) == std::errc::permission_denied);
    ZEXPECT(out == dir.file(R"(first\tool.exe)"));
}

// What a Cygwin package installs for a link such as /usr/bin/cc, which
// CreateProcess cannot start, is the file it is.
ZEST_CASE(find_create_process_program_takes_a_cygwin_symlink_as_it_is) {
    test::make_program(dir, "", "gcc-13.exe");
    test::write_text(dir.file("cc.exe"), "!<symlink>gcc-13\0"sv);
    std::vector<std::string> directories = {dir.root()};
    ZEXPECT(found("cc", directories) == dir.file("cc.exe"));
}

};  // ZEST_SUITE(sys_windows_program)
#endif

}  // namespace

}  // namespace kotatsu::sys::windows
