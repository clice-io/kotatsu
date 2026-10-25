#pragma once

// entries_of(), what kota::fs::read_dir() lists, for the tests of what made
// the entries.

#include <string_view>
#include <utility>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/fs/directory.h"

namespace kota::test {

/// The entries of `dir`, each read without an error.
inline std::vector<fs::DirEntry> entries_of(std::string_view dir) {
    ZEST_CONTEXT("{}", dir);
    auto stream = fs::read_dir(dir);
    ZASSERT(stream.has_value());
    std::vector<fs::DirEntry> entries;
    for(auto& entry: *stream) {
        ZASSERT(entry.has_value());
        entries.push_back(std::move(*entry));
    }
    return entries;
}

}  // namespace kota::test
