#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include "os.h"
#include "proc.h"
#include "kotatsu/sys/process.h"

#ifdef __APPLE__
#include <crt_externs.h>
#endif

namespace kotatsu::sys {

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

#ifdef __linux__

/// What Linux keeps of `name` for a thread's: its first 15 bytes, cut back
/// to a character's start when they would cut one.
std::string_view kept_name(std::string_view name) {
    if(name.size() > 15) {
        std::size_t end = 15;
        while(end > 0 && (static_cast<unsigned char>(name[end]) & 0xC0) == 0x80) {
            --end;
        }
        name = name.substr(0, end);
    }
    return name;
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
    // One of which nothing is left for the name, as an empty one, leaves the
    // name as it was.
    if(!kept_name(title).empty()) {
        return set_process_name(title);
    }
#endif
    return {};
#endif
}

std::error_code set_process_name([[maybe_unused]] std::string_view name) {
#ifdef __linux__
    auto kept = kept_name(name);
    if(kept.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    // The name of the main thread, which is the process's in top and pgrep.
    return fs::write_file("/proc/self/comm", kept);
#else
    return std::make_error_code(std::errc::not_supported);
#endif
}

}  // namespace kotatsu::sys
