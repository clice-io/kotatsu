#include <optional>

#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/sys/spawn.h"

namespace kotatsu::sys {

namespace {

ZEST_SUITE(sys_spawn_exit_status) {

ZEST_CASE(success_is_exit_code_zero_without_a_signal) {
    ZEXPECT(ExitStatus{.code = 0, .term_signal = 0}.success());
    ZEXPECT(!ExitStatus{.code = 1, .term_signal = 0}.success());
    ZEXPECT(!ExitStatus{.code = 0, .term_signal = 9}.success());
}

ZEST_CASE(to_string_tells_how_the_child_ended) {
    ZEXPECT(ExitStatus{.code = 3, .term_signal = 0}.to_string() == "exit code 3");
    ZEXPECT(ExitStatus{.code = 0, .term_signal = 9}.to_string() == "signal 9 (SIGKILL)");
    ZEXPECT(ExitStatus{.code = 0, .term_signal = 15}.to_string() == "signal 15 (SIGTERM)");
    // No signal has this number.
    ZEXPECT(ExitStatus{.code = 0, .term_signal = 1000}.to_string() == "signal 1000");
}

ZEST_CASE(exception_is_unset_for_an_exit_code_or_a_signal) {
    ZEXPECT(ExitStatus{.code = 3, .term_signal = 0}.exception() == std::nullopt);
    ZEXPECT(ExitStatus{.code = 0, .term_signal = 9}.exception() == std::nullopt);
    ZEXPECT(ExitStatus{.code = 0x7FFF'FFFF, .term_signal = 0}.exception() == std::nullopt);
}

#ifdef _WIN32
ZEST_CASE(exception_is_the_ntstatus_a_windows_child_crashed_with) {
    ZEXPECT(ExitStatus{.code = 0xC000'0005, .term_signal = 0}.exception() == 0xC000'0005u);
    ZEXPECT(ExitStatus{.code = 0x8000'0000, .term_signal = 0}.exception() == 0x8000'0000u);
    ZEXPECT(ExitStatus{.code = 0x8000'0003, .term_signal = 0}.exception() == 0x8000'0003u);
    // exit(-1), which reads as one too.
    ZEXPECT(ExitStatus{.code = 0xFFFF'FFFF, .term_signal = 0}.exception() == 0xFFFF'FFFFu);
}
#else
ZEST_CASE(exception_is_unset_on_posix) {
    ZEXPECT(ExitStatus{.code = 0xC000'0005, .term_signal = 0}.exception() == std::nullopt);
}
#endif

#ifdef _WIN32
ZEST_CASE(to_string_names_a_windows_crash) {
    ZEXPECT(ExitStatus{.code = 0xC000'0005, .term_signal = 0}.to_string() ==
            "exception 0xC0000005 (STATUS_ACCESS_VIOLATION)");
    ZEXPECT(ExitStatus{.code = 0xC000'0135, .term_signal = 0}.to_string() ==
            "exception 0xC0000135 (STATUS_DLL_NOT_FOUND)");
    // No status of the table.
    ZEXPECT(ExitStatus{.code = 0xC000'0001, .term_signal = 0}.to_string() ==
            "exception 0xC0000001");
}
#endif

};  // ZEST_SUITE(sys_spawn_exit_status)

}  // namespace

}  // namespace kotatsu::sys
