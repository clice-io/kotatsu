#include "kotatsu/cli/commands.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace kotatsu::cli::detail {

void CommandRegistry::add(CommandEntry entry) {
    const auto at = std::ranges::lower_bound(entries, entry.name, {}, &CommandEntry::name);
    if(at != entries.end() && at->name == entry.name) {
        std::fprintf(stderr, "cli: two subcommands are named %s\n", entry.name.c_str());
        std::abort();
    }
    entries.insert(at, std::move(entry));
}

}  // namespace kotatsu::cli::detail
