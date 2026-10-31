#pragma once

// entries_of(), what a kotatsu::fs::Directory lists, for the tests of what made
// the entries.

#include <string_view>
#include <utility>
#include <vector>

#include "kotatsu/zest/zest.h"
#include "kotatsu/fs/directory.h"

namespace kotatsu::test {

/// The entries of `dir`, each read without an error.
inline std::vector<fs::Directory::Entry> entries_of(std::string_view dir) {
    ZEST_CONTEXT("{}", dir);
    auto directory = fs::Directory::open(dir);
    ZASSERT(directory.has_value());
    std::vector<fs::Directory::Entry> entries;
    for(auto& entry: *directory) {
        ZASSERT(entry.has_value());
        entries.push_back(std::move(*entry));
    }
    return entries;
}

}  // namespace kotatsu::test
