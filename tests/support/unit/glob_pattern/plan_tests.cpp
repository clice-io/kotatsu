#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "support/harness/glob.h"
#include "kota/zest/zest.h"
#include "kota/support/glob_pattern.h"

namespace kota {

namespace {

// A pattern compiles into whichever plan its shape allows: a literal, a prefix tree, suffix
// checks, extension lookup, or the general segment program. Every plan matches exactly what
// the pattern means.

/// The patterns and paths of the randomized reference check: atoms that are ASCII, CJK, an
/// astral code point and a byte that is not UTF-8.
constexpr std::array<std::string_view, 5> atoms = {"a", "b", "中", "🚀", "\xFF"};

struct Token {
    std::string_view text;
    /// The atoms it matches, one bit each.
    unsigned mask;
    bool star = false;
};

constexpr std::array<Token, 9> tokens = {
    {{"a", 1},
     {"b", 2},
     {"中", 4},
     {"🚀", 8},
     {"?", 31},
     {"[ab]", 3},
     {"[!中]", 27},
     {R"(\中)", 4},
     {"*", 31, true}}
};

struct Segment {
    bool globstar;
    std::vector<std::size_t> members;
};

/// Whether `segment` matches the path segment of atoms `word`, by dynamic programming over
/// both: nothing of the matcher under test.
bool reference_segment(const Segment& segment, const std::vector<std::size_t>& word) {
    std::vector<bool> previous(word.size() + 1);
    previous[0] = true;
    for(auto member: segment.members) {
        const auto& token = tokens[member];
        std::vector<bool> next(word.size() + 1);
        next[0] = token.star && previous[0];
        for(std::size_t j = 1; j <= word.size(); ++j) {
            next[j] = token.star ? previous[j] || next[j - 1]
                                 : previous[j - 1] && (token.mask & (1u << word[j - 1])) != 0;
        }
        previous = std::move(next);
    }
    return previous.back();
}

/// Whether `segments` match the path of `words`, `**` taking any number of whole segments.
bool reference_path(const std::vector<Segment>& segments,
                    const std::vector<std::vector<std::size_t>>& words) {
    std::vector<bool> previous(words.size() + 1);
    previous[0] = true;
    for(const auto& segment: segments) {
        std::vector<bool> next(words.size() + 1);
        next[0] = segment.globstar && previous[0];
        for(std::size_t j = 1; j <= words.size(); ++j) {
            next[j] = segment.globstar
                          ? previous[j] || next[j - 1]
                          : previous[j - 1] && reference_segment(segment, words[j - 1]);
        }
        previous = std::move(next);
    }
    return previous.back();
}

ZEST_SUITE(support_glob_pattern_plan) {

ZEST_CASE(only_whole_globstars_match_everything_trivially) {
    for(auto pattern: {"**", "{foo,**}", "**/"}) {
        ZEST_CONTEXT("glob `{}`", pattern);
        auto compiled = GlobPattern::create(pattern);
        ZASSERT(compiled.has_value());
        ZEXPECT(compiled->is_trivial_match_all());
    }
    // A leading `/` still constrains, though it leaves no literal prefix behind.
    for(auto pattern:
        {"*", "**/*", "foo/**", "*.js", "{a,b}", "/*", "/**", "{*,foo}", "a{*,foo}"}) {
        ZEST_CONTEXT("glob `{}`", pattern);
        auto compiled = GlobPattern::create(pattern);
        ZASSERT(compiled.has_value());
        ZEXPECT(!compiled->is_trivial_match_all());
    }
}

ZEST_CASE(randomized_patterns_agree_with_a_reference) {
    constexpr unsigned seed = 205;
    std::mt19937 random(seed);
    for(int trial = 0; trial < 4000; ++trial) {
        std::vector<Segment> segments;
        std::string source;
        const auto segment_count = 1 + random() % 5;
        for(std::size_t i = 0; i < segment_count; ++i) {
            if(i != 0) {
                source += '/';
            }
            auto& segment = segments.emplace_back(random() % 4 == 0, std::vector<std::size_t>{});
            if(segment.globstar) {
                source += "**";
                continue;
            }
            const auto count = 1 + random() % 5;
            for(std::size_t j = 0; j < count; ++j) {
                auto member = random() % tokens.size();
                // Two single stars in a row would spell another operator.
                if(!segment.members.empty() && tokens[segment.members.back()].star &&
                   tokens[member].star) {
                    member = 0;
                }
                segment.members.push_back(member);
                source += tokens[member].text;
            }
        }
        ZEST_CONTEXT("seed {}, trial {}, glob `{}`", seed, trial, source);
        auto compiled = GlobPattern::create(source);
        ZASSERT(compiled.has_value());
        for(int sample = 0; sample < 16; ++sample) {
            std::vector<std::vector<std::size_t>> words(1 + random() % 5);
            std::string path;
            for(auto& word: words) {
                if(!path.empty()) {
                    path += '/';
                }
                const auto count = 1 + random() % 6;
                for(std::size_t j = 0; j < count; ++j) {
                    const auto atom = random() % atoms.size();
                    word.push_back(atom);
                    path += atoms[atom];
                }
            }
            const bool expected = reference_path(segments, words);
            if(compiled->match(path) != expected) {
                ZEST_CONTEXT("path `{}`", path);
                ZEXPECT(compiled->match(path) == expected);
                return;
            }
        }
    }
}

ZEST_CASE(suffix_checks_read_no_byte_outside_the_path) {
    std::vector<std::string> literals = {"", "说明.cpp", "🚀", std::string("\0z", 2)};
    for(std::size_t length: {1, 7, 8, 9, 15, 16, 17, 24}) {
        literals.emplace_back(length, 'x');
    }
    for(const auto& literal: literals) {
        ZEST_CONTEXT("literal of {} bytes", literal.size());
        auto recursive = GlobPattern::create("**/*" + literal);
        auto segment = GlobPattern::create("*" + literal);
        ZASSERT(recursive.has_value());
        ZASSERT(segment.has_value());
        const auto copied = *recursive;
        for(std::size_t padding = 0; padding < 25; ++padding) {
            const std::string filler(padding, 'q');
            for(const auto& text:
                {filler, filler + literal, "/" + filler + literal, filler + literal + 'q'}) {
                ZEST_CONTEXT("path of {} bytes", text.size());
                // An allocation of the path's exact size puts ASan's redzones right around it,
                // shorter than a word included.
                auto exact = std::make_unique<char[]>(text.size());
                std::ranges::copy(text, exact.get());
                const std::string_view path(exact.get(), text.size());
                ZEXPECT(recursive->match(path) == path.ends_with(literal));
                ZEXPECT(copied.match(path) == path.ends_with(literal));
                ZEXPECT(segment->match(path) == (path.ends_with(literal) && !path.contains('/')));
            }
        }
    }
}

ZEST_CASE(extension_sets_agree_with_a_linear_check) {
    for(std::size_t count: {15, 16, 17, 50, 100}) {
        std::vector<std::string> extensions = {".", ".中", ".🚀", ".abcdefg"};
        while(extensions.size() < count) {
            extensions.push_back(std::format(".e{}", extensions.size()));
        }
        // The last variant has an extension of two dots and one of 9 bytes, neither of which
        // a lookup by the last dot finds.
        for(auto last: {extensions.back(), std::string(".d.ts"), std::string(".abcdefgh")}) {
            extensions.back() = last;
            std::string source = "**/*.{";
            for(std::size_t i = 0; i < extensions.size(); ++i) {
                source += i == 0 ? "" : ",";
                source += extensions[i].substr(1);
            }
            source += '}';
            ZEST_CONTEXT("{} extensions, the last `{}`", count, last);
            auto compiled = GlobPattern::create(source);
            ZASSERT(compiled.has_value());
            const auto moved = std::move(*compiled);
            for(const auto& extension: extensions) {
                for(const auto& path: {extension,
                                       "file" + extension,
                                       "路径/file" + extension,
                                       "dir" + extension + "/file",
                                       "file" + extension + 'q'}) {
                    ZEST_CONTEXT("path `{}`", path);
                    const bool expected = std::ranges::any_of(extensions, [&](const auto& suffix) {
                        return path.ends_with(suffix);
                    });
                    ZEXPECT(moved.match(path) == expected);
                }
            }
            ZEXPECT(!moved.match("no_extension"));
        }
    }
}

ZEST_CASE(affix_plans_match_the_head_and_the_tail) {
    test::expect_glob("**/foo*foo",
                      {"foofoo", "目录/foo中文foo"},
                      {"foo", "foo/foo", "目录/foofoo/"});
    test::expect_glob("**/test_*", {"目录/test_"}, {"目录/test_/child"});
    test::expect_glob(R"(**/\*.cpp)", {"x/*.cpp"}, {"x/a.cpp"});
}

ZEST_CASE(compiled_patterns_copy_and_move) {
    auto pattern = GlobPattern::create(R"(src/*/test_\[中\]*.cpp)");
    ZASSERT(pattern.has_value());
    auto copy = *pattern;
    auto moved = std::move(copy);
    auto other = GlobPattern::create("other");
    ZASSERT(other.has_value());
    *pattern = *other;
    ZEXPECT(moved.match("src/x/test_[中].cpp"));
    ZEXPECT(!moved.match("other"));
    ZEXPECT(pattern->match("other"));

    auto tree = GlobPattern::create(R"(/work/项目\[demo\]/src/**/*.cpp)");
    ZASSERT(tree.has_value());
    const auto tree_copy = *tree;
    const auto tree_moved = std::move(*tree);
    ZEXPECT(tree_copy.match("/work/项目[demo]/src/test.cpp"));
    ZEXPECT(tree_moved.match("/work/项目[demo]/src/test.cpp"));
}

ZEST_CASE(patterns_too_large_to_keep_inline_copy_and_move) {
    // Enough arms, segments, tokens and classes that none of them fits inline.
    auto pattern = GlobPattern::create("{a,b,c}/x*/[0-9]?/**/y/*.{c,h}");
    ZASSERT(pattern.has_value());
    const auto copy = *pattern;
    auto assigned = GlobPattern::create("other");
    ZASSERT(assigned.has_value());
    *assigned = std::move(*pattern);
    const GlobPattern& moved_in = *assigned;
    for(const auto* compiled: {&copy, &moved_in}) {
        ZEST_CONTEXT("the {}", compiled == &copy ? "copy" : "pattern moved in");
        ZEXPECT(compiled->match("b/xz/5q/deep/down/y/main.c"));
        ZEXPECT(compiled->match("c/x/0!/y/main.h"));
        ZEXPECT(!compiled->match("d/xz/5q/y/main.c"));
        ZEXPECT(!compiled->match("other"));
    }
}

};  // ZEST_SUITE(support_glob_pattern_plan)

}  // namespace

}  // namespace kota
