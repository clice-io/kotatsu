#include <format>
#include <string>

#include "os.h"
#include "kota/sys/info.h"

#ifdef _WIN32
#include "kota/support/utf16.h"
#else
#include <sys/utsname.h>
#endif

namespace kota::sys {

Uname uname() {
#ifdef _WIN32
    // GetVersionEx answers what the program's manifest says it knows of.
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    ::RtlGetVersion(&version);
    Uname name;
    name.sysname = "Windows_NT";
    name.release = std::format("{}.{}.{}",
                               version.dwMajorVersion,
                               version.dwMinorVersion,
                               version.dwBuildNumber);
    wchar_t product[256];
    DWORD size = sizeof(product);
    if(::RegGetValueW(HKEY_LOCAL_MACHINE,
                      LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)",
                      L"ProductName",
                      RRF_RT_REG_SZ,
                      nullptr,
                      product,
                      &size) == ERROR_SUCCESS) {
        name.version = from_utf16(std::wstring_view(product));
    }
    if(version.szCSDVersion[0] != L'\0') {
        name.version += ' ';
        name.version += from_utf16(std::wstring_view(version.szCSDVersion));
    }
    SYSTEM_INFO system{};
    ::GetNativeSystemInfo(&system);
    switch(system.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: name.machine = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: name.machine = "arm64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: name.machine = "i686"; break;
        case PROCESSOR_ARCHITECTURE_ARM: name.machine = "arm"; break;
        default: name.machine = "unknown"; break;
    }
    return name;
#else
    utsname buffer{};
    ::uname(&buffer);
    return Uname{
        .sysname = buffer.sysname,
        .release = buffer.release,
        .version = buffer.version,
        .machine = buffer.machine,
    };
#endif
}

}  // namespace kota::sys
