#pragma once

#include <cstdint>
#include <cstring>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

#include "kota/support/small_vector.h"

namespace kota {

struct GlobError {
    enum Kind : std::uint8_t {
        UnmatchedBracket,
        StrayBackslash,
        NestedBrace,
        EmptyBrace,
        IncompleteBrace,
        InvalidRange,
        MultipleSlash,
        MultipleStar,
        TooManyExpansions,
        InvalidUtf8,
        InvalidEscape,
    };

    Kind kind;
    /// The offending bytes, [begin, end) of the pattern given to GlobPattern::create(),
    /// whichever arm of a brace expansion they were found in; both 0 for TooManyExpansions.
    std::uint32_t begin;
    std::uint32_t end;
    std::string message;
};

/// A pattern cut at its literal root (GlobPattern::split_root()): the
/// directory every path it matches is in, or is, and the rest.
struct GlobRoot {
    /// The directory, its escapes resolved, up to and including its last `/`;
    /// empty when no `/` comes before the pattern's first wildcard.
    std::string directory;

    /// The rest of the pattern, as written: a view of the pattern given to
    /// split_root().
    std::string_view rest;
};

/// Glob pattern matcher supporting VS Code-style glob syntax.
/// Case-sensitive, whole-string matching. Callers normalize path separators
/// to `/`; backslashes in inputs, pattern whitespace and trailing `/` are literal.
///
/// Supported syntax:
/// - `*` to match zero or more characters in a path segment
/// - `?` to match on one character in a path segment
/// - `**` as a whole path segment to match any number of segments, including none
/// - `{}` to group conditions (e.g. `**.{ts,js}`)
/// - `[]` to declare a range of characters (e.g., `example.[0-9]`)
/// - `[!...]` to negate a range (e.g., `example.[!0-9]`)
///
/// Note: Use only `/` for path segment separator. `/` cannot be escaped:
/// outside a bracket expression `\/` is rejected at create() with
/// InvalidEscape; inside one it parses as a class member that never
/// matches, because a bracket never matches `/`.
///
/// Only a whole-segment `**` crosses `/`. Embedded `**` behaves like `*`.
/// A brace-expanded arm of exactly `**` makes the whole pattern match-all.
///
/// Patterns must be valid UTF-8; create() rejects anything else with
/// InvalidUtf8. Matching is Unicode-aware: `?`, `[]` and escaped literals
/// consume one decoded code point at a time, while `*`, `**` and literal
/// runs stay byte-level (UTF-8 is self-synchronizing, so this cannot
/// change which code points they cover). Matched paths need not be valid
/// UTF-8: an input byte that does not decode counts as one character that
/// `?`, `*`, `**` and negated classes can cover, but no literal or
/// positive range can name it.
/// Character ranges use code-point order, without locale collation or Unicode normalization.
///
/// A pattern compiles once into a literal prefix shared by every arm of its
/// brace expansion plus one segment program per arm. Matching never
/// allocates and never truncates its search.
class GlobPattern {
public:
    /// A pattern is at most 4 GiB, brace expansion included: every offset
    /// the compiled form keeps is 32-bit, as are the error positions.
    /// Brace expansion may produce at most `max_arms` arms, one for each
    /// choice of the braces' terms; 0 turns it off, and braces then match themselves.
    [[nodiscard]] static std::expected<GlobPattern, GlobError> create(std::string_view s,
                                                                      size_t max_arms = 100);

    /// `literal` as a pattern that matches it and nothing else, each of
    /// `\ ? * [ ] { } ,` escaped, so that it also stands as a term of a brace
    /// expression. `/` stays a separator, as it cannot be escaped. A literal
    /// that is not UTF-8, or that holds `//`, has no pattern: create() rejects
    /// its escape with InvalidUtf8 or MultipleSlash.
    [[nodiscard]] static std::string escape(std::string_view literal);

    /// `pattern` cut at the last `/` before its first unescaped `?`, `*`, `[`
    /// or `{`, so that every path it matches is in, or is, the directory
    /// before the cut: `src/a{b,c}/*.cpp` cuts into `src/` and `a{b,c}/*.cpp`,
    /// `*.cpp` into no directory and itself, and a pattern without wildcards
    /// at its last `/`. A `\` that create() rejects, escaping a `/` or
    /// nothing, ends the root too. The pattern is not validated, and the rest
    /// may compile where the pattern does not (`a//b/*`): compile the pattern
    /// itself to learn what is wrong with it.
    [[nodiscard]] static GlobRoot split_root(std::string_view pattern);

    [[nodiscard]] bool is_trivial_match_all() const {
        return mode == Mode::Any;
    }

    [[nodiscard]] bool match(std::string_view s) const {
        switch(mode) {
            case Mode::Literal: return s == prefix;
            case Mode::Any: return true;
            case Mode::PrefixTree:
                return s.starts_with(prefix) &&
                       (s.size() == prefix.size() || s[prefix.size()] == '/');
            case Mode::Suffix: return arms.front().literal.matches(s, literals);
            case Mode::PathSuffix: {
                const auto& literal = arms.front().literal;
                return literal.matches(s, literals) &&
                       (s.size() == literal.size || s[s.size() - literal.size - 1] == '/');
            }
            case Mode::SegmentSuffix:
                return arms.front().literal.matches(s, literals) && !s.contains('/');
            case Mode::Arms:
            case Mode::SuffixArms:
            case Mode::SegmentSuffixArms:
            case Mode::ExtensionArms: return match_arms(s);
        }
        std::unreachable();
    }

private:
    /// A literal the input must end with, packed for a word-sized compare
    /// when it is short enough. Bytes are right-aligned so the test is
    /// endianness-independent.
    struct PackedLiteral {
        std::uint32_t begin = 0;
        std::uint32_t size = 0;
        std::uint64_t word = 0;
        std::uint64_t mask = 0;
        std::uint64_t lead_word = 0;
        std::uint64_t lead_mask = 0;

        [[nodiscard]] bool matches(std::string_view input, std::string_view literals) const {
            if(input.size() < size) {
                return false;
            }
            if(size <= sizeof(word) && input.size() >= sizeof(word)) {
                std::uint64_t tail;
                std::memcpy(&tail, input.data() + input.size() - sizeof(tail), sizeof(tail));
                return (tail & mask) == word;
            }
            if(size <= 16 && input.size() >= 16) {
                std::uint64_t first, last;
                std::memcpy(&first, input.data() + input.size() - 16, 8);
                std::memcpy(&last, input.data() + input.size() - 8, 8);
                return (last & mask) == word && (first & lead_mask) == lead_word;
            }
            return input.ends_with(literals.substr(begin, size));
        }
    };

    struct Token {
        enum class Kind : std::uint8_t {
            /// `begin`/`size` index `literals`.
            Literal,
            /// Zero or more characters within the segment.
            Star,
            /// `?`: exactly one character.
            Any,
            /// `begin` indexes `classes`.
            Class,
        };
        Kind kind;
        std::uint32_t begin = 0;
        std::uint32_t size = 0;
    };

    struct CharClass {
        bool negated;
        /// Inclusive code-point ranges, sorted and non-overlapping.
        small_vector<std::pair<char32_t, char32_t>, 2> ranges;

        [[nodiscard]] bool contains(char32_t cp) const;
    };

    struct Segment {
        enum class Kind : std::uint8_t {
            /// A whole-segment `**`: zero or more input segments.
            Recursive,
            /// At most one literal token.
            Literal,
            /// Optional literal, one star, optional literal.
            Affix,
            /// Any other token sequence.
            General,
        };
        Kind kind = Kind::General;
        /// Segments from this one on match an input that has ended.
        bool nullable = false;
        /// Segments after this one can match nothing, without a separator.
        bool optional_rest = false;
        /// The first token is a literal starting with `first`, letting a
        /// mismatching input segment be rejected before it is delimited.
        bool has_first = false;
        char first = 0;
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
    };

    /// One arm of the brace expansion: the pattern past the prefix, each brace replaced
    /// by one of its terms. The plan is read off the compiled segments,
    /// so equivalent spellings always execute the same way.
    struct Arm {
        enum class Plan : std::uint8_t {
            /// `dir/**`: `head` is the directory.
            PrefixTree,
            /// `**/dir/**`: `head` is the directory.
            DirectoryTree,
            /// `**/*suffix`.
            Suffix,
            /// `**/name`.
            PathSuffix,
            /// `*suffix`.
            SegmentSuffix,
            /// `**/head*suffix` or `head*suffix`.
            Affix,
            /// `**/segment` or a lone segment with `?`, a class or several
            /// stars: only the final input segment can match.
            Final,
            /// The segment program.
            General,
        };
        Plan plan = Plan::General;
        /// `Affix`/`Final` arms: the segment follows `**`, so it matches
        /// the basename rather than the whole input.
        bool basename = false;
        /// With the prefix's separator missing (input equals the prefix),
        /// a leading `**` absorbs it when what follows can vanish too.
        bool absorbs_separator = false;
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
        /// The literal a `PrefixTree`, `DirectoryTree` or `Affix` plan
        /// starts with.
        std::uint32_t head_begin = 0;
        std::uint32_t head_size = 0;
        /// Trailing literal of the last segment: a necessary condition
        /// checked before anything else, and the whole check for the
        /// suffix plans.
        PackedLiteral literal;
    };

    enum class Mode : std::uint8_t {
        Literal,
        Any,
        PrefixTree,
        /// Single-arm suffix plans without a prefix, decided inline.
        Suffix,
        PathSuffix,
        SegmentSuffix,
        Arms,
        /// Every arm is a `Suffix` or `SegmentSuffix` plan.
        SuffixArms,
        SegmentSuffixArms,
        /// At least 16 simple extensions, arms sorted for binary search.
        ExtensionArms,
    };

    [[nodiscard]] std::expected<void, GlobError> compile_arm(std::string_view s,
                                                             bool at_segment_start);

    [[nodiscard]] std::string_view literal(const Token& token) const {
        return std::string_view(literals).substr(token.begin, token.size);
    }

    [[nodiscard]] std::string_view head(const Arm& arm) const {
        return std::string_view(literals).substr(arm.head_begin, arm.head_size);
    }

    [[nodiscard]] bool match_arms(std::string_view str) const;
    [[nodiscard]] bool match_extension_set(std::string_view str) const;
    [[nodiscard]] bool match_final(const Arm& arm, std::string_view str) const;
    [[nodiscard]] bool execute(const Arm& arm, std::string_view str) const;
    [[nodiscard]] bool match_affix(const Segment& segment, std::string_view input) const;
    [[nodiscard]] bool match_tokens(const Segment& segment, std::string_view input) const;

    std::string prefix;
    bool prefix_at_seg_end = false;
    Mode mode = Mode::Literal;
    std::string literals;
    small_vector<Token, 4> tokens;
    small_vector<Segment, 3> segments;
    small_vector<CharClass, 0> classes;
    small_vector<Arm, 1> arms;
};

}  // namespace kota
