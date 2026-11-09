#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

#include "frames.h"
#include "os.h"
#include "kotatsu/support/config.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/support/utf16.h"
#include "kotatsu/sys/stacktrace.h"

// After windows.h, whose types they use.
#include <psapi.h>
#include <winternl.h>

namespace kotatsu::sys {

namespace {

/// LDR_DLL_NOTIFICATION_DATA, the same for a load and an unload.
struct DllNotification {
    ULONG flags;
    const UNICODE_STRING* full_name;
    const UNICODE_STRING* base_name;
    void* base;
    ULONG size;
};

using DllCallback = void(NTAPI*)(ULONG reason, const DllNotification* data, void* context);

/// ntdll's, which no SDK declares.
using RegisterDllNotification = LONG(NTAPI*)(ULONG flags,
                                             DllCallback callback,
                                             void* context,
                                             void** cookie);
using UnregisterDllNotification = LONG(NTAPI*)(void* cookie);
using LockLoaderLock = LONG(NTAPI*)(ULONG flags, ULONG* state, ULONG_PTR* cookie);
using UnlockLoaderLock = LONG(NTAPI*)(ULONG flags, ULONG_PTR cookie);

/// LDR_DLL_NOTIFICATION_REASON_LOADED and _UNLOADED.
constexpr ULONG dll_loaded = 1;
constexpr ULONG dll_unloaded = 2;

struct Module {
    std::uintptr_t base;
    std::uintptr_t size;
    std::uint32_t name_at;
    std::uint32_t name_size;
    std::atomic<bool> unloaded;
};

/// The modules loaded, their names in `names`: the ones loaded before the
/// registration below, and those the loader's notifications tell of after.
/// Their writers take `writing`, a lock of their own, and a crash handler
/// reads them without one, where GetModuleHandleExW and GetModuleFileNameW
/// take the loader lock. A module past their room goes without, its frames
/// in no module.
constinit Module modules[1024] = {};
constinit std::atomic<std::size_t> module_count = 0;
constinit char names[128 * 1024] = {};
constinit std::size_t names_used = 0;
constinit std::atomic_flag writing;

/// Holds `writing` while it lives: no loader call is made under it, so a
/// notification, which may come on a loader thread with the loader lock
/// held, never waits for one.
struct Writing {
    Writing() noexcept {
        while(writing.test_and_set(std::memory_order_acquire)) {
            YieldProcessor();
        }
    }

    ~Writing() {
        writing.clear(std::memory_order_release);
    }

    Writing(const Writing&) = delete;
    Writing& operator=(const Writing&) = delete;
};

/// The room of `names` from `names_used` on, a buffer for from_utf16, which
/// is asked for no more than it has.
struct NameRoom {
    std::size_t length = 0;

    char* data() noexcept {
        return names + names_used;
    }

    std::size_t size() const noexcept {
        return length;
    }

    void resize(std::size_t size) noexcept {
        length = size;
    }
};

/// Adds the module at `base`, converting its name as a notification must,
/// without calling into another module.
void add(std::uintptr_t base, std::uintptr_t size, std::wstring_view name) noexcept {
    Writing held;
    auto count = module_count.load(std::memory_order_relaxed);
    for(std::size_t index = 0; index < count; ++index) {
        if(modules[index].base == base &&
           !modules[index].unloaded.load(std::memory_order_relaxed)) {
            return;
        }
    }
    // Three bytes of UTF-8 at most for a unit of UTF-16.
    if(count == std::size(modules) || name.size() * 3 > sizeof(names) - names_used) {
        return;
    }
    NameRoom room;
    from_utf16(room, name);
    auto& module = modules[count];
    module.base = base;
    module.size = size;
    module.name_at = static_cast<std::uint32_t>(names_used);
    module.name_size = static_cast<std::uint32_t>(room.length);
    names_used += room.length;
    module_count.store(count + 1, std::memory_order_release);
}

void NTAPI notified(ULONG reason, const DllNotification* data, void*) noexcept {
    auto base = reinterpret_cast<std::uintptr_t>(data->base);
    if(reason == dll_loaded) {
        add(base,
            data->size,
            std::wstring_view(data->full_name->Buffer, data->full_name->Length / sizeof(wchar_t)));
    } else if(reason == dll_unloaded) {
        Writing held;
        auto count = module_count.load(std::memory_order_relaxed);
        for(std::size_t index = 0; index < count; ++index) {
            if(modules[index].base == base) {
                modules[index].unloaded.store(true, std::memory_order_relaxed);
            }
        }
    }
}

/// The function entry of KiUserExceptionDispatcher, whose caller's frame is
/// the context an exception interrupted.
constinit std::atomic<const RUNTIME_FUNCTION*> dispatcher = nullptr;

/// Registers for the loader's notifications before main and adds the
/// modules loaded by then, both under the loader lock, which keeps a module
/// from loading between them; unregisters as the program, or a DLL it links
/// into, is unloaded, whose code `notified` is.
struct Register {
    void* registration = nullptr;
    UnregisterDllNotification unregister = nullptr;

    Register() noexcept {
        auto* ntdll = ::GetModuleHandleW(L"ntdll.dll");
        auto entry = [&](const char* name) {
            return reinterpret_cast<void*>(::GetProcAddress(ntdll, name));
        };
        auto lock = reinterpret_cast<LockLoaderLock>(entry("LdrLockLoaderLock"));
        auto unlock = reinterpret_cast<UnlockLoaderLock>(entry("LdrUnlockLoaderLock"));
        auto register_notification =
            reinterpret_cast<RegisterDllNotification>(entry("LdrRegisterDllNotification"));
        unregister =
            reinterpret_cast<UnregisterDllNotification>(entry("LdrUnregisterDllNotification"));

        DWORD64 image_base = 0;
        dispatcher.store(
            ::RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entry("KiUserExceptionDispatcher")),
                                     &image_base,
                                     nullptr),
            std::memory_order_relaxed);

        ULONG_PTR cookie = 0;
        lock(0, nullptr, &cookie);
        register_notification(0, notified, nullptr, &registration);
        auto* process = ::GetCurrentProcess();
        HMODULE loaded[std::size(modules)];
        DWORD needed = 0;
        ::K32EnumProcessModules(process, loaded, sizeof(loaded), &needed);
        small_wstring<264> path;
        for(std::size_t index = 0;
            index < std::min<std::size_t>(needed / sizeof(HMODULE), std::size(loaded));
            ++index) {
            MODULEINFO info{};
            if(::K32GetModuleInformation(process, loaded[index], &info, sizeof(info))) {
                add(reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll),
                    info.SizeOfImage,
                    module_file_name(loaded[index], path));
            }
        }
        unlock(0, cookie);
    }

    ~Register() {
        unregister(registration);
    }

    Register(const Register&) = delete;
    Register& operator=(const Register&) = delete;
};

[[maybe_unused]] const Register registered;

std::optional<FrameModule> find_module(std::uintptr_t address) noexcept {
    auto count = module_count.load(std::memory_order_acquire);
    for(std::size_t index = 0; index < count; ++index) {
        const auto& candidate = modules[index];
        if(address - candidate.base < candidate.size &&
           !candidate.unloaded.load(std::memory_order_relaxed)) {
            return FrameModule{.path =
                                   std::string_view(names + candidate.name_at, candidate.name_size),
                               .base = candidate.base};
        }
    }
    return std::nullopt;
}

/// Writes the frames from `context`'s, the address an exception interrupted
/// when `interrupted`, or, `own`, from the caller of write_stacktrace's own.
void walk(CONTEXT& context, bool interrupted, bool own, FrameWriter& writer) noexcept {
    const auto* dispatching = dispatcher.load(std::memory_order_relaxed);
#if defined(_M_ARM64) || defined(__aarch64__)
    // Whether the frame before had a function entry.
    bool entered = true;
#endif
    for(;; own = false) {
#if defined(_M_ARM64) || defined(__aarch64__)
        auto pc = context.Pc;
#else
        auto pc = context.Rip;
#endif
        // The outermost frame's return address; the address a call through
        // a null pointer went to, which the frame below called from.
        if(pc == 0 && !interrupted) {
            return;
        }
        if(!own && !writer.frame(pc, find_module(pc - (interrupted ? 0 : 1)), interrupted)) {
            return;
        }
        DWORD64 image_base = 0;
        auto* function = ::RtlLookupFunctionEntry(pc, &image_base, nullptr);
        interrupted = function != nullptr && function == dispatching;
        if(function != nullptr) {
            void* handler_data = nullptr;
            DWORD64 establisher = 0;
            ::RtlVirtualUnwind(UNW_FLAG_NHANDLER,
                               image_base,
                               pc,
                               function,
                               &context,
                               &handler_data,
                               &establisher,
                               nullptr);
#if defined(_M_ARM64) || defined(__aarch64__)
            entered = true;
#endif
            continue;
        }
        // A leaf function, which has no entry: it moved neither the stack
        // pointer nor the return address.
#if defined(_M_ARM64) || defined(__aarch64__)
        // The return address stays in lr: one more frame without an entry
        // would take it again.
        if(!entered) {
            return;
        }
        context.Pc = context.Lr;
        entered = false;
#else
        context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
        context.Rsp += sizeof(DWORD64);
#endif
    }
}

}  // namespace

KOTATSU_NOINLINE std::error_code write_stacktrace(int fd) noexcept {
    CONTEXT context;
    ::RtlCaptureContext(&context);
    FrameWriter writer(fd);
    walk(context, false, true, writer);
    return writer.error;
}

KOTATSU_NOINLINE std::error_code write_stacktrace(int fd, const Crash& crash) noexcept {
    FrameWriter writer(fd);
    CONTEXT context;
    if(crash.context == nullptr) {
        ::RtlCaptureContext(&context);
        walk(context, false, true, writer);
    } else {
        // A copy, which the walk unwinds.
        context = *static_cast<const CONTEXT*>(crash.context);
        walk(context, true, false, writer);
    }
    return writer.error;
}

}  // namespace kotatsu::sys

#endif
