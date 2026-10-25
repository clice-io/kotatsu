#include <csignal>

#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/process.h"

namespace kota::sys {

namespace {

ZEST_SUITE(sys_process_exit_status) {

ZEST_CASE(signal_name_names_this_hosts_signals) {
    ZEXPECT(signal_name(SIGINT) == "SIGINT");
    ZEXPECT(signal_name(SIGSEGV) == "SIGSEGV");
    ZEXPECT(signal_name(SIGTERM) == "SIGTERM");
    ZEXPECT(signal_name(SIGABRT) == "SIGABRT");
#ifndef _WIN32
    ZEXPECT(signal_name(SIGKILL) == "SIGKILL");
    ZEXPECT(signal_name(SIGPIPE) == "SIGPIPE");
#endif
}

#ifdef _WIN32
ZEST_CASE(signal_name_names_sigkill_by_its_posix_number_on_windows) {
    ZEXPECT(signal_name(9) == "SIGKILL");
    ZEXPECT(signal_name(SIGBREAK) == "SIGBREAK");
}
#endif

ZEST_CASE(signal_name_of_no_signal_is_empty) {
    ZEXPECT(signal_name(0).empty());
    ZEXPECT(signal_name(1000).empty());
}

};  // ZEST_SUITE(sys_process_exit_status)

}  // namespace

}  // namespace kota::sys
