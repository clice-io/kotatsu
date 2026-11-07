#include <csignal>

#include "kotatsu/zest/macro.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/sys/process.h"

namespace kotatsu::sys {

namespace {

ZEST_SUITE(sys_process_signal_name) {

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

ZEST_CASE(sighup_and_sigkill_have_posix_numbers) {
    ZSTATIC_EXPECT(sighup == 1);
    ZSTATIC_EXPECT(sigkill == 9);
#ifndef _WIN32
    ZSTATIC_EXPECT(sighup == SIGHUP);
    ZSTATIC_EXPECT(sigkill == SIGKILL);
#endif
}

// Windows' <csignal> has neither.
ZEST_CASE(signal_name_names_sighup_and_sigkill) {
    ZEXPECT(signal_name(sighup) == "SIGHUP");
    ZEXPECT(signal_name(sigkill) == "SIGKILL");
}

#ifdef _WIN32
ZEST_CASE(signal_name_names_sigbreak_on_windows) {
    ZEXPECT(signal_name(SIGBREAK) == "SIGBREAK");
}
#endif

ZEST_CASE(signal_name_of_no_signal_is_empty) {
    ZEXPECT(signal_name(0).empty());
    ZEXPECT(signal_name(1000).empty());
}

};  // ZEST_SUITE(sys_process_signal_name)

}  // namespace

}  // namespace kotatsu::sys
