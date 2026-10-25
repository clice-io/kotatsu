#include "os.h"
#include "kota/fs/path.h"
#include "kota/sys/dirs.h"

#ifdef _WIN32
#include "kota/support/small_string.h"
#include "kota/support/utf16.h"
#else
#include "kota/sys/env.h"
#endif

namespace kota::sys {

std::error_code temp_dir(buffer_ref out) {
#ifdef _WIN32
    using GetTempPath = DWORD(WINAPI*)(DWORD, wchar_t*);
    // GetTempPath2W is there from Windows 11 and Server 2022 on. Cast through
    // a function pointer of no type, which any other converts to.
    auto get_temp_path = reinterpret_cast<GetTempPath>(reinterpret_cast<void (*)()>(
        ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "GetTempPath2W")));
    if(get_temp_path == nullptr) {
        get_temp_path = ::GetTempPathW;
    }
    small_wstring<264> path;
    path.resize(MAX_PATH + 1);
    while(true) {
        auto size = get_temp_path(static_cast<DWORD>(path.size()), path.data());
        if(size == 0) {
            return fs::windows::last_error();
        }
        // Too small, it asks for the size with the NUL.
        if(size < path.size()) {
            from_utf16(out, std::wstring_view(path.data(), size));
            fs::path::normalize(out, fs::path::DotDot::Keep);
            return {};
        }
        path.resize(size);
    }
#else
    for(auto name: {"TMPDIR", "TMP", "TEMP", "TEMPDIR"}) {
        if(get_env(out, name) && !out.empty()) {
            fs::path::normalize(out, fs::path::DotDot::Keep);
            return {};
        }
    }
    out.assign("/tmp");
    return {};
#endif
}

}  // namespace kota::sys
