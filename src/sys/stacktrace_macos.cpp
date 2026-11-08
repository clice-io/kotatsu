#ifdef __APPLE__

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>

#include "frames.h"
#include "os.h"
#include "kotatsu/support/config.h"
#include "kotatsu/sys/stacktrace.h"

namespace kotatsu::sys {

namespace {

/// An image dyld loaded: its __TEXT segment, slide and path.
struct Image {
    std::uintptr_t start;
    std::uintptr_t end;
    std::uintptr_t slide;
    const mach_header* header;
    const char* path;
    std::atomic<bool> unloaded = false;
    Image* next;
};

/// The images, the last loaded first, each added once and never taken out:
/// read without a lock, where dladdr and dyld's unwinding lookups take the
/// lock of its list of images, which dlopen and dlclose hold while they
/// allocate and run initializers and terminators.
constinit std::atomic<Image*> images = nullptr;

void add(const mach_header* header, std::intptr_t slide, const char* path) noexcept {
    auto text = text_segment(header);
    auto start = text.address + static_cast<std::uintptr_t>(slide);
    auto* image = new Image{.start = start,
                            .end = start + text.size,
                            .slide = static_cast<std::uintptr_t>(slide),
                            .header = header,
                            .path = ::strdup(path),
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
                    static_cast<std::intptr_t>(text_segment(header).address),
                infos->dyldPath);
        }
    }
};

[[maybe_unused]] const Register registered;

const FrameModule* find_module(std::uintptr_t address, FrameModule& module) noexcept {
    for(auto* image = images.load(std::memory_order_acquire); image != nullptr;
        image = image->next) {
        if(address >= image->start && address < image->end &&
           !image->unloaded.load(std::memory_order_relaxed)) {
            module = {.path = image->path, .base = image->slide};
            return &module;
        }
    }
    return nullptr;
}

/// A stack frames may be on: the thread's, or the alternate one a signal
/// handler runs on.
struct Stack {
    std::uintptr_t low = 0;
    std::uintptr_t high = 0;

    /// Whether a frame record, two words, at `address` is on it.
    bool holds(std::uintptr_t address) const noexcept {
        return address >= low && address + 2 * sizeof(std::uintptr_t) <= high &&
               address % sizeof(std::uintptr_t) == 0;
    }
};

}  // namespace

KOTATSU_NOINLINE std::error_code write_stacktrace(int fd) noexcept {
    auto self = ::pthread_self();
    auto top = reinterpret_cast<std::uintptr_t>(::pthread_get_stackaddr_np(self));
    const Stack thread{.low = top - ::pthread_get_stacksize_np(self), .high = top};
    Stack alternate;
    if(stack_t stack; ::sigaltstack(nullptr, &stack) == 0 && !(stack.ss_flags & SS_DISABLE)) {
        alternate.low = reinterpret_cast<std::uintptr_t>(stack.ss_sp);
        alternate.high = alternate.low + stack.ss_size;
    }

    // Frame records, from this function's: the caller's record, then the
    // return address into it. Up the stack each one is, or from the
    // alternate stack to the thread's, where a signal handler's _sigtramp
    // records the frame the signal interrupted.
    FrameWriter writer(fd);
    FrameModule module;
    auto record = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    while(thread.holds(record) || alternate.holds(record)) {
        const auto* words = reinterpret_cast<const std::uintptr_t*>(record);
        auto next = words[0];
        auto address = words[1];
#ifdef __aarch64__
        address = strip_return_address(address);
#endif
        if(address == 0 || !writer.frame(address, find_module(address - 1, module), false)) {
            break;
        }
        const auto& stack = thread.holds(record) ? thread : alternate;
        if(stack.holds(next) && next <= record) {
            break;
        }
        record = next;
    }
    return writer.error;
}

}  // namespace kotatsu::sys

#endif
