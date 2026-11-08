#pragma once

#include <system_error>

namespace kotatsu::sys {

/// Writes the frames of the calling thread's stack to descriptor `fd`, from
/// the caller's outward, at most 256, a line each:
///
///     0x<address>, <module path>(0x<module base>) + 0x<offset> byte(s)
///     0x<address> <unknown module>
///
/// the second for an address in no module this process loaded. The address
/// and the base have 16 hexadecimal digits, the offset as many as it takes,
/// in upper case; the base is the module's ELF load bias, Mach-O slide or
/// Windows module handle, which makes the offset the address a symbolizer
/// looks up in an ELF or Mach-O file, and in a PE file the RVA. Each address
/// is a return address, just past its call, whose function and line are
/// those of the byte before it, but for the one a signal or an exception
/// interrupted, whose line ends with " (interrupted)". No symbols:
/// llvm-symbolizer, atos or addr2line give them, inlined functions too,
/// from the file.
///
/// What a crash handler calls, from sigaction's handler or
/// SetUnhandledExceptionFilter's: async-signal-safe, it allocates nothing,
/// takes no lock of the dynamic loader's or of malloc, and needs nothing set
/// up first. Called from a signal handler or an exception filter, it walks
/// past the signal frame or the exception dispatcher into the code that was
/// interrupted. Linux: by the CFI of .eh_frame, as a C++ exception would
/// unwind, from the exact address of the instruction that faulted; a frame
/// of code without it, as `-fno-asynchronous-unwind-tables` leaves out, ends
/// the trace. macOS: by frame pointers, which its ABI keeps, the instruction
/// that faulted left out (the return address in it of the call it made last,
/// or of the code that called it, comes next). Windows: by the unwind data
/// of its PE modules, as RtlVirtualUnwind reads it, which takes ntdll's own
/// locks in shared mode as the exception dispatcher does. A corrupt stack
/// may fault in it and end the process.
///
/// The error of the write that failed, which ends the trace.
std::error_code write_stacktrace(int fd) noexcept;

}  // namespace kotatsu::sys
