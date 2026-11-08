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
/// SetUnhandledExceptionFilter's: async-signal-safe, it allocates nothing
/// and takes no lock of the dynamic loader's or of malloc. Called from a
/// signal handler or an exception filter, it walks past the signal frame or
/// the exception dispatcher into the code that was interrupted, and past an
/// address a call through a bad pointer went to into the caller. It takes
/// about 3 KiB of stack, 6 KiB on Linux with a glibc older than 2.35, which
/// an alternate stack must have left after the kernel's signal frame and
/// the handler's own.
///
/// - Linux, x86-64 and arm64: by the CFI of .eh_frame, as a C++ exception
///   unwinds, from the exact address of the instruction that faulted. A
///   frame of code without CFI, as `-fno-asynchronous-unwind-tables` leaves
///   out, ends the trace; programs linking kotatsu::sys are linked with
///   `--eh-frame-hdr`, which indexes the CFI. not_supported elsewhere.
/// - macOS: by frame pointers, which its ABI keeps, the instruction that
///   faulted left out: next comes the return address in it of the call it
///   made last, or in the code that called it.
/// - Windows: by the unwind data of its PE modules, as RtlVirtualUnwind
///   reads it, which takes ntdll's own locks in shared mode as the exception
///   dispatcher does. `fd` must be open: the C runtime's invalid-parameter
///   handler, which ends the process unless the program set another, takes
///   one that is not, but for one below 0.
///
/// The modules are those loaded once static initialization has set up the
/// tables of macOS and Windows, which a trace of a crash before leaves
/// unnamed. A corrupt stack may fault in it, ending the process.
///
/// The error of the write that failed, which ends the trace: bad_file_descriptor
/// for a descriptor below 0.
std::error_code write_stacktrace(int fd) noexcept;

}  // namespace kotatsu::sys
