#include <string>
#include <vector>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/fs/path.h"
#include "kota/sys/process.h"

namespace kota::sys {

namespace {

ZEST_SUITE(sys_process_arguments) {

#ifdef _WIN32
// The command line this process started with, whatever argv says.
ZEST_CASE(arguments_are_read_from_the_command_line) {
    auto arguments = sys::arguments(0, nullptr);
    ZASSERT(!arguments.empty());
    ZEXPECT(fs::path::stem(arguments[0]).ends_with("unit_tests"));
}
#else
ZEST_CASE(arguments_are_argv) {
    const char* argv[] = {"program", "", "caf\xC3\xA9", "a b"};
    ZEXPECT(arguments(4, argv) == std::vector<std::string>{"program", "", "caf\xC3\xA9", "a b"});
}
#endif

};  // ZEST_SUITE(sys_process_arguments)

}  // namespace

}  // namespace kota::sys
