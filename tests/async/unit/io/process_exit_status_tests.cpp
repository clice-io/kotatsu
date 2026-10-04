#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

using exit_status = process::exit_status;

ZEST_SUITE(async_io_process_exit_status) {

ZEST_CASE(success_is_exit_code_zero_without_a_signal) {
    ZEXPECT(exit_status{.status = 0, .term_signal = 0}.success());
    ZEXPECT(!exit_status{.status = 1, .term_signal = 0}.success());
    ZEXPECT(!exit_status{.status = 0, .term_signal = 9}.success());
}

ZEST_CASE(to_string_tells_how_the_child_ended) {
    ZEXPECT(exit_status{.status = 3, .term_signal = 0}.to_string() == "exit code 3");
    ZEXPECT(exit_status{.status = 0, .term_signal = 9}.to_string() == "signal 9 (SIGKILL)");
    ZEXPECT(exit_status{.status = 0, .term_signal = 15}.to_string() == "signal 15 (SIGTERM)");
    // No signal has this number.
    ZEXPECT(exit_status{.status = 0, .term_signal = 1000}.to_string() == "signal 1000");
}

#ifdef _WIN32
ZEST_CASE(to_string_names_a_windows_crash) {
    ZEXPECT(exit_status{.status = 0xC000'0005, .term_signal = 0}.to_string() ==
            "exception 0xC0000005 (access violation)");
    ZEXPECT(exit_status{.status = 0xC000'0135, .term_signal = 0}.to_string() ==
            "exception 0xC0000135");
}
#endif

};  // ZEST_SUITE(async_io_process_exit_status)

}  // namespace

}  // namespace kota
