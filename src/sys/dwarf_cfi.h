#pragma once

#include <cstdint>

// An interpreter of the call frame information of .eh_frame, which moves a
// frame's registers to its caller's as an unwinder does: what
// write_stacktrace walks a Linux stack by, with no unwinder of a library's,
// whose lookups of the modules loaded take the dynamic loader's lock.

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#define KOTATSU_SYS_DWARF_CFI 1

namespace kotatsu::sys::dwarf {

#if defined(__x86_64__)
/// rax, rdx, rcx, rbx, rsi, rdi, rbp, rsp, r8 to r15 and the return address,
/// by DWARF's numbers, which CFI calls them by.
constexpr inline unsigned register_count = 17;
constexpr inline unsigned sp_register = 7;
#else
/// x0 to x30 and sp.
constexpr inline unsigned register_count = 32;
constexpr inline unsigned sp_register = 31;
#endif

/// A frame's registers and the address in its code it is at.
struct Registers {
    std::uint64_t value[register_count] = {};
    std::uint64_t pc = 0;
};

/// Moves `registers` from a frame to its caller's, whose pc is a return
/// address, by the CFI that the .eh_frame_hdr at `header` indexes. `exact`
/// says whether pc is the instruction the frame is at, as of a frame a
/// signal interrupted, rather than a return address, which may be past the
/// end of its function. False when the CFI has no entry for pc, says the
/// frame is the outermost one, its return address undefined, or holds what
/// this does not read.
bool step(Registers& registers, bool exact, const std::uint8_t* header) noexcept;

}  // namespace kotatsu::sys::dwarf
#else
#define KOTATSU_SYS_DWARF_CFI 0
#endif
