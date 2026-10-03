#include <cstdint>

#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

process::exit_status exited(std::int64_t code) {
    return {.status = code, .term_signal = 0};
}

process::exit_status signalled(int signal) {
    return {.status = 0, .term_signal = signal};
}

ZEST_SUITE(async_io_process_status, zest::LoopFixture) {

ZEST_CASE(success_is_exit_code_zero_without_a_signal) {
    EXPECT(exited(0).success());
    EXPECT(!exited(1).success());
    EXPECT(!signalled(9).success());
}

ZEST_CASE(to_string_gives_the_exit_code) {
    EXPECT(exited(0).to_string() == "exit code 0");
    EXPECT(exited(3).to_string() == "exit code 3");
}

ZEST_CASE(to_string_gives_the_signal) {
    auto text = signalled(9).to_string();
    EXPECT(zest::starts_with(text, "signal 9"));
#ifndef _WIN32
    // The system names it: "Killed", or "Killed: 9" on macOS.
    EXPECT(zest::contains(text, "Killed"));
#endif
}

#ifdef _WIN32
ZEST_CASE(to_string_gives_a_crash_code_in_hex) {
    EXPECT(exited(0xC000'0005).to_string() == "exit code 0xC0000005 (access violation)");
    EXPECT(exited(0xC000'00FD).to_string() == "exit code 0xC00000FD (stack overflow)");
    EXPECT(exited(0xC000'0409).to_string() == "exit code 0xC0000409 (stack buffer overrun)");
    EXPECT(exited(0xC000'0001).to_string() == "exit code 0xC0000001");
}
#endif

};  // ZEST_SUITE(async_io_process_status)

}  // namespace

}  // namespace kota
