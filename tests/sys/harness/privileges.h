#pragma once

// ScopedNoPrivilege, on Windows the calling thread without a privilege the
// process may hold, such as the debug privilege, with which an administrator
// opens any process.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "kota/zest/zest.h"

namespace kota::test {

/// The calling thread without privilege `name`, such as L"SeDebugPrivilege",
/// until this goes.
struct ScopedNoPrivilege {
    explicit ScopedNoPrivilege(const wchar_t* name) {
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
        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_REMOVED;
        ZASSERT(::LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid));
        // Succeeds, saying ERROR_NOT_ALL_ASSIGNED, for a token without it.
        ZASSERT(::AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr));
        ZASSERT(::SetThreadToken(nullptr, token));
        ::CloseHandle(token);
    }

    ScopedNoPrivilege(const ScopedNoPrivilege&) = delete;
    ScopedNoPrivilege& operator=(const ScopedNoPrivilege&) = delete;

    ~ScopedNoPrivilege() {
        ::RevertToSelf();
    }
};

}  // namespace kota::test

#endif
