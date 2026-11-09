#ifdef __APPLE__

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <optional>
#include <pthread.h>
#include <signal.h>
#include <string_view>
#include <sys/ucontext.h>

#include "frames.h"
#include "os.h"
#include "kotatsu/support/config.h"
#include "kotatsu/sys/stacktrace.h"

namespace kotatsu::sys {

namespace {

/// A function's code: from its start to the next one's.
struct Function {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;

    bool contains(std::uintptr_t address) const noexcept {
        return address >= start && address < end;
    }
};

/// An image dyld loaded: its __TEXT segment, slide, path, and where its
/// functions start.
struct Image {
    std::uintptr_t start;
    std::uintptr_t end;
    std::uintptr_t slide;
    const mach_header* header;
    const char* path;
    /// The data of LC_FUNCTION_STARTS, ULEB128 offsets of the image's
    /// functions, each from the one before and the first from the start of
    /// __TEXT, ending in a zero; null for an image linked without it.
    const std::uint8_t* function_starts;
    const std::uint8_t* function_starts_end;
    std::atomic<bool> unloaded = false;
    Image* next;

    /// The function `address`, in the image's code, is in: std::nullopt
    /// without LC_FUNCTION_STARTS.
    std::optional<Function> function_of(std::uintptr_t address) const noexcept {
        std::optional<Function> found;
        auto at = start;
        const auto* byte = function_starts;
        while(byte != function_starts_end) {
            std::uintptr_t delta = 0;
            for(int shift = 0; byte != function_starts_end; shift += 7) {
                delta |= std::uintptr_t(*byte & 0x7F) << shift;
                if(!(*byte++ & 0x80)) {
                    break;
                }
            }
            if(delta == 0) {
                break;
            }
            at += delta;
            if(at > address) {
                break;
            }
            found = Function{.start = at, .end = end};
        }
        if(found && at > address) {
            found->end = at;
        }
        return found;
    }
};

/// Where the image's __LINKEDIT, which the offsets of LC_SYMTAB and
/// LC_FUNCTION_STARTS are into, would start were it mapped from its file's
/// start. dyld's shared cache maps one __LINKEDIT that its images share,
/// whose offsets are into it.
std::uintptr_t linkedit_base(const mach_header* header, std::uintptr_t slide) noexcept {
    const auto& linkedit = find_segment(header, SEG_LINKEDIT);
    return linkedit.vmaddr + slide - linkedit.fileoff;
}

/// The images, the last loaded first, each added once and never taken out:
/// read without a lock, where dladdr and dyld's unwinding lookups take the
/// lock of its list of images, which dlopen and dlclose hold while they
/// allocate and run initializers and terminators.
constinit std::atomic<Image*> images = nullptr;

void add(const mach_header* header, std::intptr_t slide, const char* path) noexcept {
    const auto& text = find_segment(header, SEG_TEXT);
    auto start = text.vmaddr + static_cast<std::uintptr_t>(slide);
    const std::uint8_t* starts = nullptr;
    std::size_t size = 0;
    if(const auto* found = find_command<linkedit_data_command>(header, LC_FUNCTION_STARTS)) {
        starts = reinterpret_cast<const std::uint8_t*>(
            linkedit_base(header, static_cast<std::uintptr_t>(slide)) + found->dataoff);
        size = found->datasize;
    }
    auto* image = new Image{.start = start,
                            .end = start + text.vmsize,
                            .slide = static_cast<std::uintptr_t>(slide),
                            .header = header,
                            .path = ::strdup(path),
                            .function_starts = starts,
                            .function_starts_end = starts + size,
                            .next = images.load(std::memory_order_relaxed)};
    while(!images.compare_exchange_weak(image->next,
                                        image,
                                        std::memory_order_release,
                                        std::memory_order_relaxed)) {}
}

void added(const mach_header* header, std::intptr_t slide) noexcept {
    // Registering calls this for each image loaded by then, and an image
    // loaded meanwhile may be told of twice.
    for(auto* image = images.load(std::memory_order_acquire); image != nullptr;
        image = image->next) {
        if(image->header == header && !image->unloaded.load(std::memory_order_relaxed)) {
            return;
        }
    }
    Dl_info info;
    if(::dladdr(header, &info) != 0 && info.dli_fname != nullptr) {
        add(header, slide, info.dli_fname);
    }
}

void removed(const mach_header* header, std::intptr_t) noexcept {
    for(auto* image = images.load(std::memory_order_acquire); image != nullptr;
        image = image->next) {
        if(image->header == header) {
            image->unloaded.store(true, std::memory_order_relaxed);
        }
    }
}

const Image* find_image(std::uintptr_t address) noexcept {
    for(auto* image = images.load(std::memory_order_acquire); image != nullptr;
        image = image->next) {
        if(address >= image->start && address < image->end &&
           !image->unloaded.load(std::memory_order_relaxed)) {
            return image;
        }
    }
    return nullptr;
}

std::optional<FrameModule> module_of(const Image* image) noexcept {
    if(image == nullptr) {
        return std::nullopt;
    }
    return FrameModule{.path = image->path, .base = image->slide};
}

/// _sigtramp, libsystem_platform's signal trampoline, which the kernel
/// starts a signal's handler in: a return address in it is the handler's.
/// Empty until static initialization finds it.
constinit Function sigtramp;

/// Finds _sigtramp by libsystem_platform's own symbol table, where it is
/// private on arm64, out of dlsym's reach, and its end by the image's
/// function starts.
void find_sigtramp() noexcept {
    constexpr std::string_view library = "/usr/lib/system/libsystem_platform.dylib";
    for(auto* image = images.load(std::memory_order_acquire); image != nullptr;
        image = image->next) {
        if(image->path != library) {
            continue;
        }
        const auto& symtab = *find_command<symtab_command>(image->header, LC_SYMTAB);
        auto base = linkedit_base(image->header, image->slide);
        const auto* symbols = reinterpret_cast<const nlist_64*>(base + symtab.symoff);
        const auto* names = reinterpret_cast<const char*>(base + symtab.stroff);
        for(std::uint32_t index = 0; index < symtab.nsyms; ++index) {
            const auto& symbol = symbols[index];
            if((symbol.n_type & N_STAB) == 0 && (symbol.n_type & N_TYPE) == N_SECT &&
               std::string_view(names + symbol.n_un.n_strx) == "__sigtramp") {
                if(auto function = image->function_of(symbol.n_value + image->slide)) {
                    sigtramp = *function;
                }
                return;
            }
        }
        return;
    }
}

/// Registers with dyld before main, which calls `added` for the images
/// loaded then, and for each one loaded later as it does.
struct Register {
    Register() noexcept {
        ::_dyld_register_func_for_add_image(added);
        ::_dyld_register_func_for_remove_image(removed);
        // dyld itself, which runs the outermost frame, start, and which
        // neither callback tells of.
        task_dyld_info_data_t dyld{};
        mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
        if(::task_info(::mach_task_self(),
                       TASK_DYLD_INFO,
                       reinterpret_cast<task_info_t>(&dyld),
                       &count) == KERN_SUCCESS) {
            const auto* infos =
                reinterpret_cast<const dyld_all_image_infos*>(dyld.all_image_info_addr);
            const auto* header = infos->dyldImageLoadAddress;
            add(header,
                reinterpret_cast<std::intptr_t>(header) -
                    static_cast<std::intptr_t>(find_segment(header, SEG_TEXT).vmaddr),
                infos->dyldPath);
        }
        find_sigtramp();
    }
};

[[maybe_unused]] const Register registered;

/// A stack frames may be on: the thread's, or the alternate one a signal
/// handler runs on.
struct Stack {
    std::uintptr_t low = 0;
    std::uintptr_t high = 0;

    /// Whether the `size` bytes at `address` are on it.
    bool contains(std::uintptr_t address, std::size_t size) const noexcept {
        return address >= low && address <= high && size <= high - address;
    }

    /// Whether a frame record, two words, at `address` is on it.
    bool holds(std::uintptr_t address) const noexcept {
        return contains(address, 2 * sizeof(std::uintptr_t)) &&
               address % sizeof(std::uintptr_t) == 0;
    }
};

/// The calling thread's stacks.
struct Stacks {
    Stack thread;
    Stack alternate;

    Stacks() noexcept {
        auto self = ::pthread_self();
        auto top = reinterpret_cast<std::uintptr_t>(::pthread_get_stackaddr_np(self));
        thread = {.low = top - ::pthread_get_stacksize_np(self), .high = top};
        if(stack_t stack; ::sigaltstack(nullptr, &stack) == 0 && !(stack.ss_flags & SS_DISABLE)) {
            alternate.low = reinterpret_cast<std::uintptr_t>(stack.ss_sp);
            alternate.high = alternate.low + stack.ss_size;
        }
    }

    /// The stack that holds a frame record at `address`: null for none.
    const Stack* holding(std::uintptr_t address) const noexcept {
        return thread.holds(address) ? &thread : alternate.holds(address) ? &alternate : nullptr;
    }
};

/// A frame record: the caller's record and the return address into it.
const std::uintptr_t* record_at(std::uintptr_t address) noexcept {
    return reinterpret_cast<const std::uintptr_t*>(address);
}

/// A return address as a frame record or lr holds it: on arm64 without the
/// pointer authentication code of code built with it.
std::uintptr_t return_address(std::uintptr_t address) noexcept {
#ifdef __aarch64__
    return strip_return_address(address);
#else
    return address;
#endif
}

/// The registers of a frame a signal interrupted that a walk by frame
/// pointers goes on from.
struct Interrupted {
    std::uintptr_t pc = 0;
    std::uintptr_t fp = 0;
#ifdef __aarch64__
    /// Where the frame returns to until it saves it in its record.
    std::uintptr_t lr = 0;
#else
    /// At the return address until the frame pushes rbp.
    std::uintptr_t sp = 0;
#endif
};

/// The registers in the mcontext at `mcontext`, which the kernel aligns on no
/// boundary on x86-64.
Interrupted interrupted_at(std::uintptr_t mcontext) noexcept {
#ifdef __aarch64__
    const auto& state = reinterpret_cast<const _STRUCT_MCONTEXT64*>(mcontext)->__ss;
    return Interrupted{.pc = __darwin_arm_thread_state64_get_pc(state),
                       .fp = __darwin_arm_thread_state64_get_fp(state),
                       .lr = __darwin_arm_thread_state64_get_lr(state)};
#else
    _STRUCT_X86_THREAD_STATE64 state;
    std::memcpy(&state,
                reinterpret_cast<const void*>(mcontext + offsetof(_STRUCT_MCONTEXT64, __ss)),
                sizeof(state));
    return Interrupted{.pc = state.__rip, .fp = state.__rbp, .sp = state.__rsp};
#endif
}

/// The registers a signal interrupted, as the kernel saved them for the
/// handler _sigtramp called, whose frame record is at `record`: std::nullopt
/// unless the pointers the kernel wrote into the context, to the stack
/// _sigtramp started on and to the context's own registers, are where it
/// writes them.
std::optional<Interrupted> signal_context(std::uintptr_t record, const Stacks& stacks) noexcept {
    const auto* holding = stacks.holding(record);
    if(holding == nullptr) {
        return std::nullopt;
    }
    const auto& stack = *holding;
#ifdef __aarch64__
    // xnu's sendsig (bsd/dev/arm/unix_signal.c) starts _sigtramp with sp at
    // a struct user_sigframe64: the siginfo, the ucontext, the mcontext.
    // _sigtramp keeps its frame record at the top of its frame, 16 bytes
    // below that sp, as every arm64 function does.
    auto entry = record + 2 * sizeof(std::uintptr_t);
    auto at = entry + sizeof(siginfo_t);
    if(!stack.contains(at, sizeof(ucontext_t) + sizeof(_STRUCT_MCONTEXT64))) {
        return std::nullopt;
    }
    const auto& context = *reinterpret_cast<const ucontext_t*>(at);
    if(reinterpret_cast<std::uintptr_t>(context.uc_stack.ss_sp) != entry ||
       reinterpret_cast<std::uintptr_t>(context.uc_mcontext) != at + sizeof(ucontext_t) ||
       context.uc_mcsize != sizeof(_STRUCT_MCONTEXT64)) {
        return std::nullopt;
    }
    return interrupted_at(at + sizeof(ucontext_t));
#else
    // xnu's sendsig (bsd/dev/i386/unix_signal.c) puts below the interrupted
    // stack's red zone the ucontext, the siginfo, then the mcontext, of the
    // size of the state the CPU has, and starts _sigtramp with rsp at the
    // mcontext's address less 16, rounded down to 16, less 8 for a return
    // address: 24 to 39 bytes below the mcontext. _sigtramp pushes rbp, its
    // frame record, right below.
    static_assert(offsetof(_STRUCT_MCONTEXT64_FULL, __ss) == offsetof(_STRUCT_MCONTEXT64, __ss) &&
                  offsetof(_STRUCT_MCONTEXT_AVX512_64, __ss) == offsetof(_STRUCT_MCONTEXT64, __ss));
    auto entry = record + sizeof(std::uintptr_t);
    for(std::size_t size: {sizeof(_STRUCT_MCONTEXT64),
                           sizeof(_STRUCT_MCONTEXT64_FULL),
                           sizeof(_STRUCT_MCONTEXT_AVX64),
                           sizeof(_STRUCT_MCONTEXT_AVX64_FULL),
                           sizeof(_STRUCT_MCONTEXT_AVX512_64),
                           sizeof(_STRUCT_MCONTEXT_AVX512_64_FULL)}) {
        for(auto mcontext = entry + 24; mcontext < entry + 40; ++mcontext) {
            auto at = mcontext + size + sizeof(siginfo_t);
            if(!stack.contains(mcontext, at + sizeof(ucontext_t) - mcontext)) {
                break;
            }
            // At no alignment the kernel promises.
            ucontext_t context;
            std::memcpy(&context, reinterpret_cast<const void*>(at), sizeof(context));
            if(reinterpret_cast<std::uintptr_t>(context.uc_stack.ss_sp) != entry ||
               reinterpret_cast<std::uintptr_t>(context.uc_mcontext) != mcontext ||
               context.uc_mcsize != size) {
                continue;
            }
            return interrupted_at(mcontext);
        }
    }
    return std::nullopt;
#endif
}

#ifdef __aarch64__
/// Whether the instruction before `address`, in an image's code, is a call:
/// BL, BLR, or one of BLR's authenticating forms.
bool follows_call(std::uintptr_t address) noexcept {
    std::uint32_t instruction = 0;
    std::memcpy(&instruction, reinterpret_cast<const void*>(address - 4), sizeof(instruction));
    return (instruction & 0xFC00'0000) == 0x9400'0000 || (instruction & 0xFEFF'F000) == 0xD63F'0000;
}
#endif

/// The return address of the frame `at` a signal interrupted, when the
/// frame has not saved it in a record of its own yet: 0 when it has, or
/// when that cannot be told.
std::uintptr_t unrecorded_return(const Interrupted& at,
                                 const Image* image,
                                 const Stacks& stacks) noexcept {
#ifdef __aarch64__
    // A function that calls none makes no record and keeps its return
    // address in lr, as does any function before its prologue, or an
    // address a call through a bad pointer went to. Once a function saved
    // lr, it may hold anything, which no call precedes.
    auto lr = return_address(at.lr);
    if(find_image(lr - 4) == nullptr || !follows_call(lr)) {
        return 0;
    }
    if(image == nullptr) {
        return lr;
    }
    // One that has made a call has made its record; until it does, the
    // record it may have made holds lr as it came.
    auto function = image->function_of(at.pc);
    if(!function || function->contains(lr) ||
       (stacks.holding(at.fp) != nullptr && return_address(record_at(at.fp)[1]) == lr)) {
        return 0;
    }
    return lr;
#else
    // At a function's first instruction, the push of its record, where a
    // stack overflow faults, as at an address a call through a bad pointer
    // went to, the call left the return address at the stack pointer.
    if(image != nullptr) {
        auto function = image->function_of(at.pc);
        if(!function || function->start != at.pc) {
            return 0;
        }
    }
    return stacks.holding(at.sp) != nullptr ? record_at(at.sp)[0] : 0;
#endif
}

/// Writes the frame a signal interrupted `at`, and its caller's when the
/// frame has no record of it: false once no more are to be written.
bool write_interrupted(const Interrupted& at, const Stacks& stacks, FrameWriter& writer) noexcept {
    const auto* image = find_image(at.pc);
    if(!writer.frame(at.pc, module_of(image), true)) {
        return false;
    }
    auto caller = unrecorded_return(at, image, stacks);
    return caller == 0 || writer.frame(caller, module_of(find_image(caller - 1)), false);
}

/// Writes the frames from the record at `record`, the caller's record and
/// the return address into it. Up the stack each one is, or from the
/// alternate stack to the thread's, where a signal handler's _sigtramp
/// records the frame the signal interrupted.
void walk(std::uintptr_t record, const Stacks& stacks, FrameWriter& writer) noexcept {
    while(const auto* stack = stacks.holding(record)) {
        const auto* words = record_at(record);
        // Swift's async frames tag the record they save in its top bits,
        // which no address on a stack has.
        auto next = words[0] & 0x0FFF'FFFF'FFFF'FFFF;
        auto address = return_address(words[1]);
        if(address == 0 || !writer.frame(address, module_of(find_image(address - 1)), false)) {
            return;
        }
        // A signal handler's return into _sigtramp, whose record is `next`:
        // on from the frame the signal interrupted.
        if(auto interrupted =
               sigtramp.contains(address - 1) ? signal_context(next, stacks) : std::nullopt) {
            if(!write_interrupted(*interrupted, stacks, writer)) {
                return;
            }
            record = interrupted->fp;
            continue;
        }
        if(stack->holds(next) && next <= record) {
            return;
        }
        record = next;
    }
}

}  // namespace

KOTATSU_NOINLINE std::error_code write_stacktrace(int fd) noexcept {
    FrameWriter writer(fd);
    walk(reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)), Stacks(), writer);
    return writer.error;
}

KOTATSU_NOINLINE std::error_code write_stacktrace(int fd, const Crash& crash) noexcept {
    FrameWriter writer(fd);
    const Stacks stacks;
    if(crash.context == nullptr) {
        walk(reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)), stacks, writer);
        return writer.error;
    }
    const auto* context = static_cast<const ucontext_t*>(crash.context);
    auto at = interrupted_at(reinterpret_cast<std::uintptr_t>(context->uc_mcontext));
    if(write_interrupted(at, stacks, writer)) {
        walk(at.fp, stacks, writer);
    }
    return writer.error;
}

}  // namespace kotatsu::sys

#endif
