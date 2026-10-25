#include <format>
#include <string>

#include "os.h"
#include "kota/sys/info.h"

#ifndef _WIN32
#include <sys/utsname.h>
#endif

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace kota::sys {

OS os() {
#ifdef _WIN32
    // GetVersionEx answers what the program's manifest says it knows of.
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    ::RtlGetVersion(&version);
    OS system{
        .name = "Windows",
        .version = std::format("{}.{}.{}",
                               version.dwMajorVersion,
                               version.dwMinorVersion,
                               version.dwBuildNumber),
    };
    SYSTEM_INFO info{};
    ::GetNativeSystemInfo(&info);
    switch(info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: system.machine = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: system.machine = "arm64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: system.machine = "i686"; break;
        case PROCESSOR_ARCHITECTURE_ARM: system.machine = "arm"; break;
        default: system.machine = "unknown"; break;
    }
    return system;
#else
    utsname buffer{};
    ::uname(&buffer);
#ifdef __APPLE__
    // The product's version rather than Darwin's ("15.7" for 24.6.0).
    char product[64] = {};
    auto size = sizeof(product);
    ::sysctlbyname("kern.osproductversion", product, &size, nullptr, 0);
    return OS{.name = "macOS", .version = product, .machine = buffer.machine};
#else
    return OS{.name = buffer.sysname, .version = buffer.release, .machine = buffer.machine};
#endif
#endif
}

}  // namespace kota::sys
