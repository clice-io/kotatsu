#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/fs/path.h"
#include "kota/sys/process.h"

namespace kota::sys {

namespace {

// What this process is; its parent is the integration tests', whose
// sys_probe has the test's runner for one.
ZEST_SUITE(sys_process_self) {

ZEST_CASE(pid_is_a_process) {
    ZEXPECT(pid() > 0);
}

// CMake builds system_tests, Bazel a binary per module: sys_system_tests.
ZEST_CASE(executable_path_names_this_program) {
    auto path = executable_path();
    ZASSERT(path.has_value());
    ZEXPECT(fs::path::is_absolute(*path));
    ZEXPECT(fs::path::stem(*path).ends_with("system_tests"));
}

// Read from the system: the program's name first, as it was started.
ZEST_CASE(arguments_start_with_this_program) {
    const auto& arguments = sys::arguments();
    ZASSERT(!arguments.empty());
    ZEXPECT(fs::path::stem(arguments[0]).ends_with("system_tests"));
    ZEXPECT(&sys::arguments() == &arguments);
}

};  // ZEST_SUITE(sys_process_self)

}  // namespace

}  // namespace kota::sys
