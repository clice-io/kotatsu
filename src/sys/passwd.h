#pragma once

#ifndef _WIN32

#include <cerrno>
#include <pwd.h>
#include <string>
#include <system_error>

#include "kotatsu/support/buffer_ref.h"

namespace kotatsu::sys {

/// Writes into `out` field `field` (pw_dir, pw_name) of the entry of the
/// password database that `find` gives, called as getpwuid_r and getpwnam_r
/// are after the key; no_such_file_or_directory when there is none.
template <typename Find>
std::error_code read_passwd(buffer_ref out, char* passwd::* field, Find find) {
    passwd entry{};
    passwd* found = nullptr;
    std::string buffer(1024, '\0');
    while(true) {
        auto error = find(&entry, buffer.data(), buffer.size(), &found);
        if(error == ERANGE) {
            buffer.resize(buffer.size() * 2);
            continue;
        }
        if(error != 0) {
            return std::error_code(error, std::generic_category());
        }
        if(found == nullptr) {
            return std::make_error_code(std::errc::no_such_file_or_directory);
        }
        out.assign(found->*field);
        return {};
    }
}

}  // namespace kotatsu::sys

#endif
