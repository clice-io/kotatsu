#pragma once

// ScopedNoPrivileges, on Windows the calling thread without privileges the
// process holds, such as the debug privilege, with which an administrator
// opens any process.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <initializer_list>
#include <windows.h>

#include "kota/zest/zest.h"

namespace kota::test {

/// The calling thread without privileges `names`, such as L"SeDebugPrivilege",
/// until this goes.
struct ScopedNoPrivileges {
    explicit ScopedNoPrivileges(std::initializer_list<const wchar_t*> names) {
        HANDLE own = nullptr;
        ZASSERT(::OpenProcessToken(::GetCurrentProcess(), TOKEN_DUPLICATE, &own));
        HANDLE token = nullptr;
        auto duplicated = ::DuplicateTokenEx(own,
                                             TOKEN_ADJUST_PRIVILEGES | TOKEN_IMPERSONATE,
                                             nullptr,
                                             SecurityImpersonation,
                                             TokenImpersonation,
                                             &token);
        ::CloseHandle(own);
        ZASSERT(duplicated);
        for(const wchar_t* name: names) {
            TOKEN_PRIVILEGES privileges{};
            privileges.PrivilegeCount = 1;
            privileges.Privileges[0].Attributes = SE_PRIVILEGE_REMOVED;
            ZASSERT(::LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid));
            // Succeeds, saying ERROR_NOT_ALL_ASSIGNED, for a token without it.
            ZASSERT(::AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr));
        }
        ZASSERT(::SetThreadToken(nullptr, token));
        ::CloseHandle(token);
    }

    ScopedNoPrivileges(const ScopedNoPrivileges&) = delete;
    ScopedNoPrivileges& operator=(const ScopedNoPrivileges&) = delete;

    ~ScopedNoPrivileges() {
        ::RevertToSelf();
    }
};

}  // namespace kota::test

#endif
