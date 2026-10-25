#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/sys/windows.h"

namespace kota::sys::windows {

namespace {

ZEST_SUITE(sys_windows_exit_status) {

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

};  // ZEST_SUITE(sys_windows_exit_status)

}  // namespace

}  // namespace kota::sys::windows
