#pragma once

#include <cstdint>
#include <initializer_list>
#include <source_location>
#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota::test {

/// `text` `times` times over.
inline std::string repeat(std::string_view text, int times) {
    std::string result;
    for(int i = 0; i < times; ++i) {
        result += text;
    }
    return result;
}

/// Checks that `pattern` compiles, matches each of `hits` and none of `misses`.
inline void expect_glob(std::string_view pattern,
                        std::initializer_list<std::string_view> hits,
                        std::initializer_list<std::string_view> misses = {},
                        std::source_location location = std::source_location::current()) {
    ZEST_CONTEXT("glob `{}`, checked at line {}", pattern, location.line());
    auto compiled = GlobPattern::create(pattern);
    ZASSERT(compiled.has_value());
    for(auto path: hits) {
        ZEST_CONTEXT("path `{}`", path);
        ZEXPECT(compiled->match(path));
    }
    for(auto path: misses) {
        ZEST_CONTEXT("path `{}`", path);
        ZEXPECT(!compiled->match(path));
    }
}

/// Checks that `pattern` fails to compile with an error of `kind` spanning [begin, end).
inline void expect_glob_error(std::string_view pattern,
                              GlobError::Kind kind,
                              std::uint32_t begin,
                              std::uint32_t end,
                              std::source_location location = std::source_location::current()) {
    ZEST_CONTEXT("glob `{}`, checked at line {}", pattern, location.line());
    auto compiled = GlobPattern::create(pattern);
    ZASSERT(!compiled.has_value());
    const auto& error = compiled.error();
    ZEXPECT(error.kind == kind);
    ZEXPECT(error.begin == begin);
    ZEXPECT(error.end == end);
    ZEXPECT(!error.message.empty());
}

}  // namespace kota::test
