#pragma once

#include <bitset>
#include <expected>
#include <format>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>

#include "kota/support/small_vector.h"
#include "kota/support/string_ref.h"

namespace kota {

namespace detail {

using GlobCharSet = std::bitset<256>;

std::expected<GlobCharSet, std::string> parse_bracket_charset(std::string_view s);

std::expected<small_vector<std::string, 1>, std::string>
    glob_parse_brace_expansions(std::string_view s, size_t max_subpattern_num);

}  // namespace detail

/// Glob pattern matcher supporting VS Code-style glob syntax.
///
/// Supported syntax:
/// - `*` to match zero or more characters in a path segment
/// - `?` to match on one character in a path segment
/// - `**` to match any number of path segments, including none
/// - `{}` to group conditions (e.g. `**.{ts,js}`)
/// - `[]` to declare a range of characters (e.g., `example.[0-9]`)
/// - `[!...]` to negate a range (e.g., `example.[!0-9]`)
///
/// Note: Use only `/` for path segment separator
///
/// Only supports single-byte characters (ASCII/Latin-1). Multi-byte encodings
/// like UTF-8 are matched byte-by-byte.
class GlobPattern {
public:
    [[nodiscard]] static std::expected<GlobPattern, std::string>
        create(std::string_view s, size_t max_subpattern_num = 100);

    [[nodiscard]] bool isTrivialMatchAll() const {
        if(!prefix.empty()) {
            return false;
        }
        if(sub_globs.size() == 1) {
            auto pat = sub_globs[0].pattern();
            return pat == "*" || pat == "**";
        }
        return false;
    }

    [[nodiscard]] bool match(std::string_view s) const;

private:
    std::string prefix;
    bool prefix_at_seg_end = false;

    struct SubGlobPattern {
        [[nodiscard]] static std::expected<SubGlobPattern, std::string> create(std::string_view s);
        [[nodiscard]] bool match(std::string_view str) const;

        [[nodiscard]] std::string_view pattern() const {
            return std::string_view{pat.data(), pat.size()};
        }

        struct Bracket {
            size_t next_offset;
            detail::GlobCharSet bytes;
        };

        small_vector<Bracket, 0> brackets;

        struct GlobSegment {
            size_t start;
            size_t end;
        };

        small_vector<GlobSegment, 6> glob_segments;
        small_vector<char, 0> pat;

    private:
        struct BacktrackState {
            size_t b;
            size_t glob_seg;
            bool wild_mode;
            const char* p;
            const char* s;
            const char* seg_end;
            const char* seg_start;
        };
    };

    small_vector<SubGlobPattern, 1> sub_globs;
};

}  // namespace kota
