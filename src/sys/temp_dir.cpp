#include <string>

#include "os.h"
#include "kota/fs/path.h"
#include "kota/sys/dirs.h"

#ifdef _WIN32
#include "kota/support/utf16.h"
#else
#include "kota/sys/env.h"
#endif

namespace kota::sys {

std::expected<std::string, std::error_code> temp_dir() {
#ifdef _WIN32
    using GetTempPath = DWORD(WINAPI*)(DWORD, wchar_t*);
    // GetTempPath2W is there from Windows 11 and Server 2022 on. Cast through
    // a function pointer of no type, which any other converts to.
    auto get_temp_path = reinterpret_cast<GetTempPath>(reinterpret_cast<void (*)()>(
        ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "GetTempPath2W")));
    if(get_temp_path == nullptr) {
        get_temp_path = ::GetTempPathW;
    }
    std::wstring path(MAX_PATH + 1, L'\0');
    while(true) {
        auto size = get_temp_path(static_cast<DWORD>(path.size()), path.data());
        if(size == 0) {
            return std::unexpected(fs::windows::last_error());
        }
        // Too small, it asks for the size with the NUL.
        if(size < path.size()) {
            std::string temp;
            from_utf16(temp, std::wstring_view(path.data(), size));
            fs::path::normalize(temp, fs::path::DotDot::Keep);
            return temp;
        }
        path.resize(size);
    }
#else
    for(auto name: {"TMPDIR", "TMP", "TEMP", "TEMPDIR"}) {
        if(auto value = get_env(name); value && !value->empty()) {
            fs::path::normalize(*value, fs::path::DotDot::Keep);
            return std::move(*value);
        }
    }
    return "/tmp";
#endif
}

}  // namespace kota::sys
