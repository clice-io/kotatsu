#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
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
/// Only the holder of the loader lock adds to them, which every notification
/// has, and a crash handler reads them without a lock, where
/// GetModuleHandleExW and GetModuleFileNameW take the loader lock. A module
/// past their room goes without, its frames in no module.
constinit Module modules[1024] = {};
constinit std::atomic<std::size_t> module_count = 0;
constinit char names[128 * 1024] = {};
constinit std::size_t names_used = 0;

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
/// from loading between them.
struct Register {
    Register() noexcept {
        auto* ntdll = ::GetModuleHandleW(L"ntdll.dll");
        auto entry = [&](const char* name) {
            return reinterpret_cast<void*>(::GetProcAddress(ntdll, name));
        };
        auto lock = reinterpret_cast<LockLoaderLock>(entry("LdrLockLoaderLock"));
        auto unlock = reinterpret_cast<UnlockLoaderLock>(entry("LdrUnlockLoaderLock"));
        auto register_notification =
            reinterpret_cast<RegisterDllNotification>(entry("LdrRegisterDllNotification"));

        DWORD64 image_base = 0;
        dispatcher.store(
            ::RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entry("KiUserExceptionDispatcher")),
                                     &image_base,
                                     nullptr),
            std::memory_order_relaxed);

        ULONG_PTR cookie = 0;
        lock(0, nullptr, &cookie);
        void* registration = nullptr;
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
            if(!::K32GetModuleInformation(process, loaded[index], &info, sizeof(info))) {
                continue;
            }
            path.resize(MAX_PATH);
            DWORD size = 0;
            while((size = ::GetModuleFileNameW(loaded[index],
                                               path.data(),
                                               static_cast<DWORD>(path.size()))) == path.size()) {
                path.resize(path.size() * 2);
            }
            add(reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll),
                info.SizeOfImage,
                std::wstring_view(path.data(), size));
        }
        unlock(0, cookie);
    }
};

[[maybe_unused]] const Register registered;

const FrameModule* find_module(std::uintptr_t address, FrameModule& module) noexcept {
    auto count = module_count.load(std::memory_order_acquire);
    for(std::size_t index = 0; index < count; ++index) {
        const auto& candidate = modules[index];
        if(address - candidate.base < candidate.size &&
           !candidate.unloaded.load(std::memory_order_relaxed)) {
            module = {.path = std::string_view(names + candidate.name_at, candidate.name_size),
                      .base = candidate.base};
            return &module;
        }
    }
    return nullptr;
}

}  // namespace

KOTATSU_NOINLINE std::error_code write_stacktrace(int fd) noexcept {
    CONTEXT context;
    ::RtlCaptureContext(&context);
    FrameWriter writer(fd);
    FrameModule module;
    const auto* dispatching = dispatcher.load(std::memory_order_relaxed);
    // Whether pc is the address an exception interrupted.
    bool interrupted = false;
#if defined(_M_ARM64) || defined(__aarch64__)
    // Whether the frame before had a function entry.
    bool entered = true;
#endif
    for(bool own = true;; own = false) {
#if defined(_M_ARM64) || defined(__aarch64__)
        auto pc = context.Pc;
#else
        auto pc = context.Rip;
#endif
        if(pc == 0) {
            break;
        }
        if(!own &&
           !writer.frame(pc, find_module(pc - (interrupted ? 0 : 1), module), interrupted)) {
            break;
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
            break;
        }
        context.Pc = context.Lr;
        entered = false;
#else
        context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
        context.Rsp += sizeof(DWORD64);
#endif
    }
    return writer.error;
}

}  // namespace kotatsu::sys

#endif
