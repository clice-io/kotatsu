#pragma once

// Reading the text of /proc and of the cgroup hierarchy: the numbers of its
// fields and words, and on Linux the files themselves.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

#include "kota/support/string_ref.h"

#ifdef __linux__
#include <expected>
#include <format>
#include <system_error>

#include "kota/support/small_string.h"
#include "kota/fs/file.h"
#endif

namespace kota::sys {

#ifdef __linux__
/// The path of `file` in the /proc directory of process `pid`, this one for 0.
inline std::string proc_path(int pid, std::string_view file) {
    if(pid == 0) {
        return std::format("/proc/self/{}", file);
    }
    return std::format("/proc/{}/{}", pid, file);
}

/// The text of a file of /proc, on the stack while it is as short as most
/// are.
using ProcText = small_string<4096>;

/// The text of the file at `path`.
inline std::expected<ProcText, std::error_code> read_text(std::string_view path) {
    ProcText text;
    if(auto error = fs::read_file(text, path)) {
        return std::unexpected(error);
    }
    return text;
}

/// The text of the file at `path`, for a cgroup::Reader.
inline std::optional<std::string> read_cgroup_file(std::string_view path) {
    std::string text;
    if(fs::read_file(text, path)) {
        return std::nullopt;
    }
    return text;
}

#endif

/// The leading decimal number of `text`, after any blanks.
inline std::optional<std::uint64_t> leading_number(std::string_view text) {
    auto digits = string_ref(text).ltrim(" \t");
    std::uint64_t value = 0;
    auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if(error != std::errc{}) {
        return std::nullopt;
    }
    return value;
}

/// The number after `key` at the start of a line of `text`, as /proc and the
/// cgroup files write their fields ("MemTotal:  16 kB", "active_file 4096").
inline std::optional<std::uint64_t> field(std::string_view text, std::string_view key) {
    for(auto range: std::views::split(text, '\n')) {
        std::string_view line(range.begin(), range.end());
        if(line.starts_with(key)) {
            return leading_number(line.substr(key.size()));
        }
    }
    return std::nullopt;
}

/// Word `index` of `text`, from 0, words being separated by spaces; empty
/// past the last.
inline std::string_view word(std::string_view text, std::size_t index) {
    for(auto range: std::views::split(text, ' ')) {
        if(range.empty()) {
            continue;
        }
        if(index-- == 0) {
            return std::string_view(range.begin(), range.end());
        }
    }
    return {};
}

/// Field `number` of a /proc/<pid>/stat text, numbered from 1 as proc(5)
/// does, from the third on; empty past the last.
inline std::string_view stat_field(std::string_view stat, std::size_t number) {
    // The command, the second, is in parentheses and may hold spaces and
    // parentheses itself.
    return word(stat.substr(stat.rfind(')') + 1), number - 3);
}

}  // namespace kota::sys
