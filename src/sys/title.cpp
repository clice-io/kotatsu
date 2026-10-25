#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include "os.h"
#include "proc.h"
#include "kota/sys/process.h"

#ifdef __APPLE__
#include <crt_externs.h>
#endif

namespace kota::sys {

#ifndef _WIN32

namespace {

/// The memory of this process's arguments, which `ps` reads.
struct Area {
    char* start = nullptr;
    std::size_t size = 0;
};

#ifdef __APPLE__

/// Found once: a title written over the arguments leaves argv's pointers
/// past its end pointing at its padding.
constinit Area arguments_area;

std::expected<Area, std::error_code> find_area() {
    if(arguments_area.start == nullptr) {
        int argc = *::_NSGetArgc();
        char** argv = *::_NSGetArgv();
        // The strings lie on the stack above the array of pointers to them,
        // unless the program pointed argv[0] elsewhere, as at a literal.
        if(argc == 0 || argv[0] <= reinterpret_cast<char*>(argv + argc)) {
            return std::unexpected(std::make_error_code(std::errc::not_supported));
        }
        // One after another; a program may have pointed the others elsewhere
        // too, which ends the area there.
        char* end = argv[0] + std::strlen(argv[0]) + 1;
        for(int i = 1; i < argc && argv[i] == end; ++i) {
            end += std::strlen(argv[i]) + 1;
        }
        arguments_area = {.start = argv[0], .size = static_cast<std::size_t>(end - argv[0])};
    }
    return arguments_area;
}

#else

std::expected<Area, std::error_code> find_area() {
    ProcText stat;
    if(auto error = fs::read_file(stat, "/proc/self/stat")) {
        return std::unexpected(error);
    }
    auto start = leading_number(stat_field(stat, 48));
    auto end = leading_number(stat_field(stat, 49));
    // Linux has them from 3.5 on. A program started without arguments has
    // an empty area, and some emulations of /proc give zeros.
    if(!start || !end || *end <= *start) {
        return std::unexpected(std::make_error_code(std::errc::not_supported));
    }
    return Area{.start = reinterpret_cast<char*>(*start), .size = *end - *start};
}

#endif

}  // namespace

#endif

std::error_code set_process_title([[maybe_unused]] std::string_view given) {
#ifdef _WIN32
    return std::make_error_code(std::errc::not_supported);
#else
    // Read before the title goes over them.
    arguments();
    auto area = find_area();
    if(!area) {
        return area.error();
    }
    // The title may lie in the area, as an argument.
    std::string title(given);
    // The area ends with a NUL: Linux reads past the arguments, into the
    // environment, when it does not.
    auto length = std::min(title.size(), area->size - 1);
    std::memcpy(area->start, title.data(), length);
    std::memset(area->start + length, '\0', area->size - length);
#ifdef __linux__
    return set_process_name(title);
#else
    return {};
#endif
#endif
}

std::error_code set_process_name([[maybe_unused]] std::string_view name) {
#ifdef __linux__
    // The name of the main thread, which is the process's in top and pgrep.
    return fs::write_file("/proc/self/comm", name.substr(0, 15));
#else
    return std::make_error_code(std::errc::not_supported);
#endif
}

}  // namespace kota::sys
