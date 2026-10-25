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
ZEST_CASE(signal_name_names_the_signals_libuv_sends_on_windows) {
    ZEXPECT(signal_name(9) == "SIGKILL");
    ZEXPECT(signal_name(SIGBREAK) == "SIGBREAK");
}
#endif

ZEST_CASE(signal_name_of_no_signal_is_empty) {
    ZEXPECT(signal_name(0).empty());
    ZEXPECT(signal_name(1000).empty());
}

ZEST_CASE(ntstatus_name_names_crashes_and_failed_starts) {
    ZEXPECT(ntstatus_name(0xC0000005) == "STATUS_ACCESS_VIOLATION");
    ZEXPECT(ntstatus_name(0xC0000135) == "STATUS_DLL_NOT_FOUND");
    ZEXPECT(ntstatus_name(0xC0000409) == "STATUS_STACK_BUFFER_OVERRUN");
    ZEXPECT(ntstatus_name(0x80000003) == "STATUS_BREAKPOINT");
}

ZEST_CASE(ntstatus_name_of_another_status_is_empty) {
    ZEXPECT(ntstatus_name(0).empty());
    ZEXPECT(ntstatus_name(1).empty());
    ZEXPECT(ntstatus_name(0xC0000001).empty());
}

};  // ZEST_SUITE(sys_process_exit_status)

}  // namespace

}  // namespace kota::sys
