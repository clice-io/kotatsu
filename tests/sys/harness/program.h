#pragma once

// make_program(), a file for find_program to find.

#include <filesystem>
#include <string>
#include <string_view>

#include "fs/harness/files.h"
#include "kotatsu/fs/path.h"

namespace kotatsu::test {

/// Creates file `name` in directory `directory` of `dir`, executable on POSIX
/// unless `executable` says otherwise, and gives its path as find_program
/// spells it.
inline std::string make_program(const TempDir& dir,
                                std::string_view directory,
                                std::string_view name,
                                bool executable = true) {
    auto path = dir.file(directory);
    fs::path::push(path, name);
    std::filesystem::create_directories(std_path(path).parent_path());
    write_text(path, "#!/bin/sh\n");
    auto permissions = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
    if(executable) {
        permissions |= std::filesystem::perms::owner_exec;
    }
    std::filesystem::permissions(std_path(path), permissions);
    return path;
}

}  // namespace kotatsu::test
