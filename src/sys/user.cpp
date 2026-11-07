#include "os.h"
#include "kotatsu/sys/env.h"
#include "kotatsu/sys/info.h"

#ifdef _WIN32
#include <lmcons.h>

#include "kotatsu/support/utf16.h"
#else
#include "passwd.h"
#endif

namespace kotatsu::sys {

std::error_code user_name(buffer_ref out) {
#ifdef _WIN32
    if(get_env(out, "USERNAME") && !out.empty()) {
        return {};
    }
    wchar_t name[UNLEN + 1];
    DWORD size = UNLEN + 1;
    if(!::GetUserNameW(name, &size)) {
        return fs::windows::last_error();
    }
    // The size counts the NUL.
    from_utf16(out, std::wstring_view(name, size - 1));
    return {};
#else
    if(get_env(out, "USER") && !out.empty()) {
        return {};
    }
    return read_passwd(out, &passwd::pw_name, [](auto... args) {
        return ::getpwuid_r(::getuid(), args...);
    });
#endif
}

}  // namespace kotatsu::sys
