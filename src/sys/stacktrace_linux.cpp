#ifdef __linux__

#include "dwarf_cfi.h"
#include "kotatsu/sys/stacktrace.h"

#if KOTATSU_SYS_DWARF_CFI

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <string_view>
#include <sys/auxv.h>
#include <ucontext.h>
#include <unistd.h>

#include "frames.h"
#include "kotatsu/support/config.h"

namespace kotatsu::sys {

namespace {

/// glibc 2.35's struct dl_find_object, which older headers lack; x86-64 and
/// arm64 have neither its dlfo_eh_dbase nor its dlfo_eh_count.
struct FoundObject {
    unsigned long long flags;
    void* map_start;
    void* map_end;
    link_map* map;
    void* eh_frame;
    unsigned long long reserved[7];
};

using FindObject = int (*)(void* address, FoundObject* result);

/// _dl_find_object, the dynamic loader's from glibc 2.35 on: async-signal
/// safe and taking no lock, unlike dl_iterate_phdr and dladdr, whose lock
/// dlopen and dlclose hold while they allocate and run constructors. Null
/// with an older glibc, or before the lookup below has run, when
/// /proc/self/maps tells the modules instead.
constinit std::atomic<FindObject> find_object = nullptr;

/// The path of the executable, whose link_map names none.
constinit char executable[1024] = {};

/// Looks up what a crash handler must not, before main.
struct LookUp {
    LookUp() noexcept {
        auto size = ::readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if(size > 0) {
            executable[size] = '\0';
        } else if(const auto* started = reinterpret_cast<const char*>(::getauxval(AT_EXECFN))) {
            std::strncpy(executable, started, sizeof(executable) - 1);
        }
        find_object.store(reinterpret_cast<FindObject>(::dlsym(RTLD_DEFAULT, "_dl_find_object")),
                          std::memory_order_release);
    }
};

[[maybe_unused]] const LookUp looked_up;

/// The module of a code address: what its line names, and its CFI.
struct Module {
    FrameModule frame;
    const std::uint8_t* eh_frame_hdr = nullptr;
};

/// A line of /proc/self/maps.
struct Mapping {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    std::uintptr_t offset = 0;
    bool executable = false;
    std::string_view path;
};

/// `text`, a line of /proc/self/maps without its newline: "<start>-<end>
/// <perms> <offset> <device> <inode> <path>", the path empty for memory
/// mapped from no file.
bool parse_mapping(std::string_view text, Mapping& mapping) noexcept {
    auto hex = [&](char end, std::uintptr_t& value) {
        value = 0;
        while(!text.empty() && text.front() != end) {
            char c = text.front();
            text.remove_prefix(1);
            if(c >= '0' && c <= '9') {
                value = value * 16 + static_cast<std::uintptr_t>(c - '0');
            } else if(c >= 'a' && c <= 'f') {
                value = value * 16 + static_cast<std::uintptr_t>(c - 'a' + 10);
            } else {
                return false;
            }
        }
        if(text.empty()) {
            return false;
        }
        text.remove_prefix(1);
        return true;
    };
    if(!hex('-', mapping.start) || !hex(' ', mapping.end) || text.size() < 5) {
        return false;
    }
    mapping.executable = text[2] == 'x';
    text.remove_prefix(5);
    if(!hex(' ', mapping.offset)) {
        return false;
    }
    for(int field = 0; field < 2; ++field) {
        auto space = text.find(' ');
        if(space == std::string_view::npos) {
            return false;
        }
        text.remove_prefix(space + 1);
    }
    auto path = text.find_first_not_of(' ');
    mapping.path = path == std::string_view::npos ? std::string_view() : text.substr(path);
    return true;
}

/// The modules of code addresses as /proc/self/maps tells them, read with
/// system calls alone: an address is in the module of a file mapped from
/// its start, its ELF header, at the lowest of its mappings.
struct MapsLookup {
    /// The mapping of the address found last, whose module `module` is, for
    /// the next address in it.
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    Module module;
    char path[1024];
    char line[1100];

    bool find(std::uintptr_t address) noexcept {
        if(address >= start && address < end) {
            return true;
        }
        int fd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
        if(fd < 0) {
            return false;
        }
        // The mapping of the file mapped from its start last, whose path is
        // in `path`.
        Mapping header;
        std::size_t path_size = 0;
        Mapping found;
        bool searching = true;
        std::size_t size = 0;
        bool cut = false;
        char chunk[512];
        while(searching) {
            auto count = ::read(fd, chunk, sizeof(chunk));
            if(count < 0 && errno == EINTR) {
                continue;
            }
            if(count <= 0) {
                break;
            }
            for(std::size_t at = 0; at < static_cast<std::size_t>(count) && searching; ++at) {
                if(chunk[at] != '\n') {
                    if(size < sizeof(line)) {
                        line[size++] = chunk[at];
                    } else {
                        cut = true;
                    }
                    continue;
                }
                Mapping mapping;
                if(!cut && parse_mapping(std::string_view(line, size), mapping)) {
                    if(mapping.offset == 0 && !mapping.path.empty() &&
                       mapping.path.size() <= sizeof(path)) {
                        header = mapping;
                        path_size = mapping.path.size();
                        std::memcpy(path, mapping.path.data(), path_size);
                    }
                    if(address >= mapping.start && address < mapping.end) {
                        searching = false;
                        if(mapping.executable && header.end != 0 &&
                           mapping.path == std::string_view(path, path_size)) {
                            found = mapping;
                        }
                    }
                }
                size = 0;
                cut = false;
            }
        }
        ::close(fd);
        if(found.end == 0 || !read_header(header, std::string_view(path, path_size))) {
            return false;
        }
        start = found.start;
        end = found.end;
        return true;
    }

    /// Takes `module` from the ELF header mapped at `header`: the load bias
    /// of the segment mapped from the file's start, and .eh_frame_hdr's
    /// segment.
    bool read_header(const Mapping& header, std::string_view name) noexcept {
        Elf64_Ehdr elf;
        std::memcpy(&elf, reinterpret_cast<const void*>(header.start), sizeof(elf));
        if(std::memcmp(elf.e_ident, ELFMAG, SELFMAG) != 0 || elf.e_ident[EI_CLASS] != ELFCLASS64 ||
           elf.e_phoff + elf.e_phnum * sizeof(Elf64_Phdr) > header.end - header.start) {
            return false;
        }
        const auto* segments = reinterpret_cast<const Elf64_Phdr*>(header.start + elf.e_phoff);
        const Elf64_Phdr* first = nullptr;
        const Elf64_Phdr* eh_frame_hdr = nullptr;
        for(std::size_t index = 0; index < elf.e_phnum; ++index) {
            const auto& segment = segments[index];
            if(segment.p_type == PT_LOAD && segment.p_offset == 0) {
                first = &segment;
            } else if(segment.p_type == PT_GNU_EH_FRAME) {
                eh_frame_hdr = &segment;
            }
        }
        if(first == nullptr) {
            return false;
        }
        auto bias = header.start - first->p_vaddr;
        module.frame = {.path = name, .base = bias};
        module.eh_frame_hdr =
            eh_frame_hdr == nullptr
                ? nullptr
                : reinterpret_cast<const std::uint8_t*>(bias + eh_frame_hdr->p_vaddr);
        return true;
    }
};

/// Into `module`, the module of code address `address`.
bool find_module(std::uintptr_t address, Module& module, MapsLookup& maps) noexcept {
    auto find = find_object.load(std::memory_order_acquire);
    if(find == nullptr) {
        if(!maps.find(address)) {
            return false;
        }
        module = maps.module;
        return true;
    }
    FoundObject found;
    if(find(reinterpret_cast<void*>(address), &found) != 0) {
        return false;
    }
    const char* name = found.map->l_name;
    module.frame = {.path = *name != '\0' ? name : executable, .base = found.map->l_addr};
    module.eh_frame_hdr = static_cast<const std::uint8_t*>(found.eh_frame);
    return true;
}

/// The context a signal interrupted, when pc is the start of the trampoline
/// a signal handler returns to, and the stack pointer the handler returned
/// with points into the frame the kernel made for the signal.
const ucontext_t* interrupted(const dwarf::Registers& registers) noexcept {
#if defined(__x86_64__)
    // __restore_rt's `mov $15, %rax; syscall`, rt_sigreturn: the frame has
    // the context at the stack pointer.
    constexpr std::uint8_t code[] = {0x48, 0xC7, 0xC0, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x05};
    constexpr std::size_t context = 0;
#else
    // The vDSO's __kernel_rt_sigreturn, `mov x8, #139; svc #0`: the frame
    // has the signal's siginfo_t at the stack pointer, the context after it.
    constexpr std::uint32_t code[] = {0xD2801168, 0xD4000001};
    constexpr std::size_t context = sizeof(siginfo_t);
#endif
    if(std::memcmp(reinterpret_cast<const void*>(registers.pc), code, sizeof(code)) != 0) {
        return nullptr;
    }
    return reinterpret_cast<const ucontext_t*>(registers.value[dwarf::sp_register] + context);
}

void restore(dwarf::Registers& registers, const ucontext_t& context) noexcept {
#if defined(__x86_64__)
    constexpr int numbered[] = {REG_RAX,
                                REG_RDX,
                                REG_RCX,
                                REG_RBX,
                                REG_RSI,
                                REG_RDI,
                                REG_RBP,
                                REG_RSP,
                                REG_R8,
                                REG_R9,
                                REG_R10,
                                REG_R11,
                                REG_R12,
                                REG_R13,
                                REG_R14,
                                REG_R15};
    for(std::size_t reg = 0; reg < std::size(numbered); ++reg) {
        registers.value[reg] = static_cast<std::uint64_t>(context.uc_mcontext.gregs[numbered[reg]]);
    }
    registers.pc = static_cast<std::uint64_t>(context.uc_mcontext.gregs[REG_RIP]);
#else
    for(std::size_t reg = 0; reg < 31; ++reg) {
        registers.value[reg] = context.uc_mcontext.regs[reg];
    }
    registers.value[dwarf::sp_register] = context.uc_mcontext.sp;
    registers.pc = context.uc_mcontext.pc;
#endif
}

}  // namespace

KOTATSU_NOINLINE std::error_code write_stacktrace(int fd) noexcept {
    // This frame's registers, at an address in it its CFI describes.
    dwarf::Registers registers;
#if defined(__x86_64__)
    static_assert(offsetof(dwarf::Registers, pc) == 0x88);
    asm volatile(
        "movq %%rbx, 0x18(%0)\n\t"
        "movq %%rbp, 0x30(%0)\n\t"
        "movq %%rsp, 0x38(%0)\n\t"
        "movq %%r12, 0x60(%0)\n\t"
        "movq %%r13, 0x68(%0)\n\t"
        "movq %%r14, 0x70(%0)\n\t"
        "movq %%r15, 0x78(%0)\n\t"
        "leaq 0(%%rip), %%rax\n\t"
        "movq %%rax, 0x88(%0)"
        :
        : "r"(&registers)
        : "rax", "memory");
#else
    static_assert(offsetof(dwarf::Registers, pc) == 256);
    asm volatile(
        "stp x19, x20, [%0, #152]\n\t"
        "stp x21, x22, [%0, #168]\n\t"
        "stp x23, x24, [%0, #184]\n\t"
        "stp x25, x26, [%0, #200]\n\t"
        "stp x27, x28, [%0, #216]\n\t"
        "stp x29, x30, [%0, #232]\n\t"
        "mov x9, sp\n\t"
        "str x9, [%0, #248]\n\t"
        "adr x9, .\n\t"
        "str x9, [%0, #256]"
        :
        : "r"(&registers)
        : "x9", "memory");
#endif

    FrameWriter writer(fd);
    MapsLookup maps;
    Module module;
    bool exact = true;
    for(bool own = true; registers.pc != 0; own = false) {
        bool found = find_module(registers.pc - (exact ? 0 : 1), module, maps);
        if(!own && !writer.frame(registers.pc, found ? &module.frame : nullptr, exact)) {
            break;
        }
        if(!found) {
            break;
        }
        auto sp = registers.value[dwarf::sp_register];
        if(const auto* context = interrupted(registers)) {
            restore(registers, *context);
            exact = true;
        } else if(module.eh_frame_hdr == nullptr ||
                  !dwarf::step(registers, exact, module.eh_frame_hdr) ||
                  registers.value[dwarf::sp_register] < sp) {
            // No CFI, the outermost frame, or a stack that does not go up.
            break;
        }
    }
    return writer.error;
}

}  // namespace kotatsu::sys

#else

namespace kotatsu::sys {

std::error_code write_stacktrace([[maybe_unused]] int fd) noexcept {
    return std::make_error_code(std::errc::not_supported);
}

}  // namespace kotatsu::sys

#endif

#endif
