#include "kota/support/glob_pattern.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <utility>

#include "kota/support/expected_try.h"

namespace kota {

namespace {

/// The bytes that start a wildcard, unless a `\` escapes them.
constexpr std::string_view wildcards = "?*[{";

/// One matching unit: a decoded Unicode scalar value, or a byte that is
/// not valid UTF-8 mapped above the Unicode range so it only compares
/// equal to the same byte. Patterns are validated to be UTF-8 at create(),
/// so invalid atoms can only come from the matched path.
struct Utf8Atom {
    char32_t cp;
    std::uint32_t len;
};

constexpr char32_t invalid_atom_base = 0x110000;

Utf8Atom decode_utf8_atom(std::string_view s, size_t at) {
    const auto lead = static_cast<std::uint8_t>(s[at]);
    if(lead < 0x80) [[likely]] {
        return {lead, 1};
    }

    const auto invalid = Utf8Atom{invalid_atom_base + lead, 1};

    std::uint32_t len;
    char32_t cp;
    if((lead & 0xE0) == 0xC0) {
        len = 2;
        cp = lead & 0x1F;
    } else if((lead & 0xF0) == 0xE0) {
        len = 3;
        cp = lead & 0x0F;
    } else if((lead & 0xF8) == 0xF0) {
        len = 4;
        cp = lead & 0x07;
    } else {
        return invalid;
    }

    if(s.size() - at < len) {
        return invalid;
    }
    for(std::uint32_t k = 1; k < len; ++k) {
        const auto cont = static_cast<std::uint8_t>(s[at + k]);
        if((cont & 0xC0) != 0x80) {
            return invalid;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }

    // Reject overlong encodings, surrogates and out-of-range values so a
    // decoded atom never aliases a differently-spelled byte sequence.
    constexpr char32_t min_for_len[] = {0, 0, 0x80, 0x800, 0x10000};
    if(cp < min_for_len[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return invalid;
    }
    return {cp, len};
}

/// Byte offset of the first ill-formed UTF-8 subsequence, or nullopt.
std::optional<std::uint32_t> find_invalid_utf8(std::string_view s) {
    for(size_t at = 0; at != s.size();) {
        const auto atom = decode_utf8_atom(s, at);
        if(atom.cp >= invalid_atom_base) {
            return static_cast<std::uint32_t>(at);
        }
        at += atom.len;
    }
    return std::nullopt;
}

/// Index of the `]` closing the bracket expression opened at `open`. A
/// leading `]`, right after the optional negation, is a member.
std::expected<std::uint32_t, GlobError> scan_bracket(std::string_view s, std::uint32_t open) {
    const auto e = static_cast<std::uint32_t>(s.size());
    auto unmatched = [&] {
        return std::unexpected{
            GlobError{GlobError::UnmatchedBracket, open, open + 1, "unmatched `[`"}
        };
    };
    auto j = open + 1;
    if(j == e) [[unlikely]] {
        return unmatched();
    }
    if(s[j] == '!' || s[j] == '^') {
        ++j;
    }
    if(j != e && s[j] == ']') {
        ++j;
    }
    while(j != e && s[j] != ']') {
        if(s[j] == '\\' && ++j == e) [[unlikely]] {
            return std::unexpected{
                GlobError{GlobError::StrayBackslash,
                          j - 1,
                          j, "unmatched `[` with stray `\\` inside"}
            };
        }
        ++j;
    }
    if(j == e) [[unlikely]] {
        return unmatched();
    }
    return j;
}

using CharClassRanges = small_vector<std::pair<char32_t, char32_t>, 2>;

std::expected<CharClassRanges, GlobError> parse_bracket_charset(std::string_view s) {
    CharClassRanges ranges;
    size_t it = 0;

    // Decode one class member, resolving a leading `\` escape; the bracket
    // scan already rejected an escape with nothing after it.
    auto next_member = [&] {
        if(s[it] == '\\') {
            ++it;
        }
        const auto atom = decode_utf8_atom(s, it);
        it += atom.len;
        return atom.cp;
    };

    // A member is held back one step so a following `-` can turn it into
    // the lower bound of a range. `-` first or last in the class stays a
    // literal member, and so does the `-` right after a completed range:
    // `[a-c-e]` reads as `a-c`, literal `-`, `e`.
    std::optional<char32_t> pending;
    std::uint32_t pending_begin = 0;

    while(it != s.size()) {
        if(s[it] == '-' && pending.has_value() && it + 1 != s.size()) {
            ++it;
            auto hi = next_member();
            if(*pending > hi) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::InvalidRange,
                              pending_begin, static_cast<std::uint32_t>(it),
                              std::format("`U+{:04X}` is larger than `U+{:04X}`",
                              static_cast<std::uint32_t>(*pending),
                              static_cast<std::uint32_t>(hi))}
                };
            }
            ranges.push_back({*pending, hi});
            pending.reset();
            continue;
        }

        auto member_begin = static_cast<std::uint32_t>(it);
        auto cp = next_member();
        if(pending.has_value()) {
            ranges.push_back({*pending, *pending});
        }
        pending = cp;
        pending_begin = member_begin;
    }
    if(pending.has_value()) {
        ranges.push_back({*pending, *pending});
    }

    // Sort and coalesce so lookup can binary-search and the per-test cost
    // is bounded by the distinct span count, not the written member count.
    std::ranges::sort(ranges);
    CharClassRanges merged;
    for(auto range: ranges) {
        if(!merged.empty() && range.first <= merged.back().second + 1) {
            merged.back().second = std::max(merged.back().second, range.second);
        } else {
            merged.push_back(range);
        }
    }

    return merged;
}

/// A brace expression: where it spans in the pattern, and its terms, which view the
/// pattern.
struct Brace {
    std::uint32_t begin;
    std::uint32_t end;
    small_vector<std::string_view, 2> terms;
};

/// The brace expressions of `pattern` from `from` on. A brace in a bracket expression or
/// after `\` is literal.
std::expected<small_vector<Brace, 0>, GlobError> parse_braces(std::string_view pattern,
                                                              std::uint32_t from) {
    small_vector<Brace, 0> braces;
    Brace* open = nullptr;
    std::uint32_t term_begin = 0;
    for(std::uint32_t i = from, e = static_cast<std::uint32_t>(pattern.size()); i != e; ++i) {
        if(pattern[i] == '[') {
            KOTA_EXPECTED_TRY_V(i, scan_bracket(pattern, i));
        } else if(pattern[i] == '{') {
            if(open) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::NestedBrace,
                              i, i + 1,
                              "nested brace expansions are not supported"}
                };
            }
            open = &braces.emplace_back();
            open->begin = i;
            term_begin = i + 1;
        } else if(pattern[i] == ',') {
            if(!open) {
                continue;
            }
            open->terms.push_back(pattern.substr(term_begin, i - term_begin));
            term_begin = i + 1;
        } else if(pattern[i] == '}') {
            if(!open) {
                continue;
            }
            if(open->terms.empty() && i == term_begin) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::EmptyBrace, open->begin, i + 1, "empty brace expression"}
                };
            }
            open->terms.push_back(pattern.substr(term_begin, i - term_begin));
            open->end = i + 1;
            open = nullptr;
        } else if(pattern[i] == '\\') {
            if(++i == e) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::StrayBackslash, i - 1, i, "stray `\\`"}
                };
            }
        }
    }

    if(open) [[unlikely]] {
        return std::unexpected{
            GlobError{GlobError::IncompleteBrace,
                      open->begin,
                      open->begin + 1,
                      "incomplete brace expansion"}
        };
    }
    return braces;
}

/// The text of one arm: the pattern from `from` on, each brace replaced by one of its terms.
/// It keeps the pieces of the pattern it is made of, which map an offset into it back to one
/// into the pattern.
struct ArmText {
    small_vector<std::string_view, 4> pieces;
    std::string text;

    /// Builds the arm that takes term `choice[k]` of brace `k`.
    void build(std::string_view pattern,
               std::uint32_t from,
               std::span<const Brace> braces,
               std::span<const std::uint32_t> choice) {
        pieces.clear();
        text.clear();
        auto cursor = from;
        for(size_t k = 0; k < braces.size(); ++k) {
            pieces.push_back(pattern.substr(cursor, braces[k].begin - cursor));
            pieces.push_back(braces[k].terms[choice[k]]);
            cursor = braces[k].end;
        }
        pieces.push_back(pattern.substr(cursor));
        for(auto piece: pieces) {
            text += piece;
        }
    }

    /// The offset in `pattern` of the arm's byte at `offset`.
    std::uint32_t origin(std::string_view pattern, std::uint32_t offset) const {
        for(auto piece: pieces) {
            if(offset < piece.size()) {
                return static_cast<std::uint32_t>(piece.data() - pattern.data()) + offset;
            }
            offset -= static_cast<std::uint32_t>(piece.size());
        }
        std::unreachable();
    }

    /// `error`, found in the arm, spanning the same bytes of `pattern`.
    GlobError rebase(std::string_view pattern, GlobError error) const {
        error.end = origin(pattern, error.end - 1) + 1;
        error.begin = origin(pattern, error.begin);
        return error;
    }
};

/// Steps `choice` to the next arm, the last brace's term changing fastest; false after the
/// last one.
bool next_choice(std::span<std::uint32_t> choice, std::span<const Brace> braces) {
    for(auto k = choice.size(); k-- > 0;) {
        if(++choice[k] != braces[k].terms.size()) {
            return true;
        }
        choice[k] = 0;
    }
    return false;
}

std::string_view basename(std::string_view path) {
    auto slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

/// First segment start at or after `from` whose segment begins with
/// `first` and, when `text` is given, equals it as a whole; `first` is then
/// the first byte of `text`. Jumps between candidates with memchr.
size_t find_segment(std::string_view str, size_t from, char first, std::string_view text) {
    for(auto at = str.find(first, from); at != std::string_view::npos;
        at = str.find(first, at + 1)) {
        auto end = at + text.size();
        if(end > str.size()) {
            break;
        }
        if((at == 0 || str[at - 1] == '/') &&
           (text.empty() ||
            ((end == str.size() || str[end] == '/') && str.compare(at, text.size(), text) == 0))) {
            return at;
        }
    }
    return std::string_view::npos;
}

}  // namespace

std::expected<GlobPattern, GlobError> GlobPattern::create(std::string_view s, size_t max_arms) {
    // Offsets into the pattern, its literals and its tokens are 32-bit.
    assert(s.size() <= std::numeric_limits<std::uint32_t>::max());

    // Validate the whole pattern before any processing. Prefix extraction
    // and brace expansion only cut at ASCII bytes, so every derived
    // sub-pattern of a valid string is itself valid — matching can then
    // decode pattern bytes without ever seeing an ill-formed sequence.
    if(auto pos = find_invalid_utf8(s)) [[unlikely]] {
        return std::unexpected{
            GlobError{GlobError::InvalidUtf8, *pos, *pos + 1, "pattern is not valid UTF-8"}
        };
    }

    GlobPattern pat;
    size_t prefix_size = s.find_first_of("?*[{\\");
    if(prefix_size == std::string_view::npos) {
        prefix_size = s.size();
    }
    pat.prefix.assign(s.substr(0, prefix_size));
    if(prefix_size < s.size() && s[prefix_size] == '\\') {
        // Escapes name literal characters, not a dynamic pattern. Decode the
        // literal prefix once, retaining raw offsets for validation/errors.
        size_t i = prefix_size;
        while(i < s.size()) {
            if(s[i] == '\\') {
                auto backslash = i++;
                if(i == s.size()) {
                    return std::unexpected{
                        GlobError{GlobError::StrayBackslash,
                                  static_cast<std::uint32_t>(backslash),
                                  static_cast<std::uint32_t>(i),
                                  "stray `\\`"}
                    };
                }
                if(s[i] == '/') {
                    return std::unexpected{
                        GlobError{GlobError::InvalidEscape,
                                  static_cast<std::uint32_t>(backslash),
                                  static_cast<std::uint32_t>(i + 1),
                                  "`/` cannot be escaped"}
                    };
                }
            } else if(wildcards.contains(s[i])) {
                break;
            }
            pat.prefix += s[i++];
        }
        prefix_size = i;
    }
    for(size_t i = 1; i < prefix_size; ++i) {
        if(s[i] == '/' && s[i - 1] == '/') [[unlikely]] {
            return std::unexpected{
                GlobError{GlobError::MultipleSlash,
                          static_cast<std::uint32_t>(i - 1),
                          static_cast<std::uint32_t>(i + 1),
                          "multiple `/` is not allowed"}
            };
        }
    }
    if(prefix_size == s.size()) {
        return pat;
    }
    if(!pat.prefix.empty() && pat.prefix.back() == '/') {
        pat.prefix_at_seg_end = true;
        pat.prefix.pop_back();
    }
    const auto from = static_cast<std::uint32_t>(prefix_size);

    small_vector<Brace, 0> braces;
    size_t arm_count = 1;
    if(max_arms != 0 && s.substr(from).contains('{')) {
        KOTA_EXPECTED_TRY_V(braces, parse_braces(s, from));
        for(const auto& brace: braces) {
            if(arm_count > std::numeric_limits<size_t>::max() / brace.terms.size()) {
                arm_count = std::numeric_limits<size_t>::max();
                break;
            }
            arm_count *= brace.terms.size();
        }
        if(arm_count > max_arms) [[unlikely]] {
            return std::unexpected{
                GlobError{GlobError::TooManyExpansions, 0, 0, "too many brace expansions"}
            };
        }
    }
    // Every arm adds at most its own size in literal bytes and
    // tokens, and one more segment; the pools index with 32 bits too.
    assert(arm_count * (s.size() - from + 1) <= std::numeric_limits<std::uint32_t>::max());

    const bool root = pat.prefix.empty() && !pat.prefix_at_seg_end;
    const bool at_segment_start = pat.prefix.empty() || pat.prefix_at_seg_end;
    bool match_all = false;
    small_vector<std::uint32_t, 4> choice(braces.size(), 0U);
    ArmText arm_text;
    do {
        arm_text.build(s, from, braces, choice);
        const auto& text = arm_text.text;
        // Expansion is textual: an arm starting with `/` right after the prefix's
        // separator spells `//`, which neither scan sees.
        if(pat.prefix_at_seg_end && text.starts_with('/')) [[unlikely]] {
            return std::unexpected{
                GlobError{GlobError::MultipleSlash,
                          from - 1,
                          arm_text.origin(s, 0) + 1,
                          "multiple `/` is not allowed"}
            };
        }
        if(auto compiled = pat.compile_arm(text, at_segment_start); !compiled) [[unlikely]] {
            return std::unexpected{arm_text.rebase(s, std::move(compiled.error()))};
        }
        match_all |= root && (text == "**" || text == "**/");
    } while(next_choice(choice, braces));

    pat.mode = Mode::Arms;
    auto all = [&](Arm::Plan plan) {
        return std::ranges::all_of(pat.arms, [plan](const Arm& arm) { return arm.plan == plan; });
    };
    const auto& arm = pat.arms.front();
    if(match_all) {
        pat.mode = Mode::Any;
    } else if(pat.arms.size() == 1 && pat.prefix_at_seg_end && arm.end == arm.begin + 1 &&
              pat.segments[arm.begin].kind == Segment::Kind::Recursive) {
        pat.mode = Mode::PrefixTree;
    } else if(pat.arms.size() == 1 && root && arm.plan == Arm::Plan::Suffix) {
        pat.mode = Mode::Suffix;
    } else if(pat.arms.size() == 1 && root && arm.plan == Arm::Plan::PathSuffix) {
        pat.mode = Mode::PathSuffix;
    } else if(pat.arms.size() == 1 && root && arm.plan == Arm::Plan::SegmentSuffix) {
        pat.mode = Mode::SegmentSuffix;
    } else if(all(Arm::Plan::Suffix)) {
        pat.mode = Mode::SuffixArms;
        // A large set of simple extensions benefits from one extension
        // extraction and integer binary search; small sets are faster inline.
        if(pat.arms.size() >= 16 && std::ranges::all_of(pat.arms, [&](const Arm& arm) {
               auto text =
                   std::string_view(pat.literals).substr(arm.literal.begin, arm.literal.size);
               return text.size() <= 8 && text.starts_with('.') && !text.substr(1).contains('.');
           })) {
            std::ranges::sort(pat.arms, {}, [](const Arm& arm) {
                return std::pair(arm.literal.size, arm.literal.word);
            });
            pat.mode = Mode::ExtensionArms;
        }
    } else if(all(Arm::Plan::SegmentSuffix)) {
        pat.mode = Mode::SegmentSuffixArms;
    }

    return pat;
}

std::string GlobPattern::escape(std::string_view literal) {
    std::string escaped;
    escaped.reserve(literal.size());
    for(char c: literal) {
        if(std::string_view(R"(\?*[{},)").contains(c)) {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

std::string GlobPattern::unescape(std::string_view pattern) {
    std::string literal;
    literal.reserve(pattern.size());
    for(size_t i = 0; i < pattern.size(); ++i) {
        if(pattern[i] == '\\' && i + 1 != pattern.size()) {
            ++i;
        }
        literal += pattern[i];
    }
    return literal;
}

std::pair<std::string_view, std::string_view>
    GlobPattern::split_literal_root(std::string_view pattern) {
    size_t root = 0;
    for(size_t i = 0; i < pattern.size() && !wildcards.contains(pattern[i]); ++i) {
        if(pattern[i] == '\\') {
            ++i;
        } else if(pattern[i] == '/') {
            root = i + 1;
        }
    }
    return {pattern.substr(0, root), pattern.substr(root)};
}

std::expected<void, GlobError> GlobPattern::compile_arm(std::string_view s, bool at_segment_start) {
    const auto e = static_cast<std::uint32_t>(s.size());
    Arm arm;
    arm.begin = static_cast<std::uint32_t>(segments.size());

    std::uint32_t i = 0;
    while(true) {
        const auto raw_begin = i;
        const auto token_begin = static_cast<std::uint32_t>(tokens.size());
        auto push_literal = [&](char c) {
            if(tokens.size() == token_begin || tokens.back().kind != Token::Kind::Literal) {
                tokens.push_back(
                    {Token::Kind::Literal, static_cast<std::uint32_t>(literals.size())});
            }
            tokens.back().size += 1;
            literals += c;
        };
        while(i != e && s[i] != '/') {
            switch(s[i]) {
                case '[': {
                    KOTA_EXPECTED_TRY_V(auto close, scan_bracket(s, i));
                    auto body = s.substr(i + 1, close - i - 1);
                    const bool negated = body[0] == '!' || body[0] == '^';
                    auto ranges = parse_bracket_charset(negated ? body.substr(1) : body);
                    if(!ranges.has_value()) [[unlikely]] {
                        // The class parser reports offsets relative to the
                        // class body; rebase onto this sub-pattern.
                        auto base = i + 1 + negated;
                        ranges.error().begin += base;
                        ranges.error().end += base;
                        return std::unexpected{std::move(ranges.error())};
                    }
                    tokens.push_back(
                        {Token::Kind::Class, static_cast<std::uint32_t>(classes.size())});
                    classes.push_back({negated, std::move(*ranges)});
                    i = close + 1;
                    break;
                }
                case '*': {
                    const auto run = i;
                    while(i != e && s[i] == '*') {
                        ++i;
                    }
                    if(i - run > 2) [[unlikely]] {
                        return std::unexpected{
                            GlobError{GlobError::MultipleStar,
                                      run, run + 3,
                                      "multiple `*` is not allowed"}
                        };
                    }
                    // Embedded ** has exactly the same operation as *.
                    if(tokens.size() == token_begin || tokens.back().kind != Token::Kind::Star) {
                        tokens.push_back({Token::Kind::Star});
                    }
                    break;
                }
                case '?': {
                    tokens.push_back({Token::Kind::Any});
                    ++i;
                    break;
                }
                case '\\': {
                    if(++i == e) [[unlikely]] {
                        return std::unexpected{
                            GlobError{GlobError::StrayBackslash, i - 1, i, "stray `\\`"}
                        };
                    }
                    // An escaped `/` would bypass separator matching and let
                    // `\/\/` match `//` while a bare `//` is rejected; the
                    // separator is structure, not a matchable character.
                    if(s[i] == '/') [[unlikely]] {
                        return std::unexpected{
                            GlobError{GlobError::InvalidEscape,
                                      i - 1,
                                      i + 1,
                                      "`/` cannot be escaped"}
                        };
                    }
                    [[fallthrough]];
                }
                default: push_literal(s[i++]);
            }
        }

        Segment segment;
        segment.begin = token_begin;
        segment.end = static_cast<std::uint32_t>(tokens.size());
        if((raw_begin != 0 || at_segment_start) && s.substr(raw_begin, i - raw_begin) == "**") {
            segment.kind = Segment::Kind::Recursive;
            tokens.pop_back();
            segment.end = segment.begin;
        } else {
            // Literal runs merge, so a segment without `?`/classes holds at
            // most one literal on each side of a star.
            auto ops = std::span(tokens).subspan(segment.begin, segment.end - segment.begin);
            auto stars = std::ranges::count(ops, Token::Kind::Star, &Token::kind);
            bool simple = std::ranges::none_of(ops, [](const Token& op) {
                return op.kind == Token::Kind::Any || op.kind == Token::Kind::Class;
            });
            segment.kind = !simple || stars > 1 ? Segment::Kind::General
                           : stars == 1         ? Segment::Kind::Affix
                                                : Segment::Kind::Literal;
            if(!ops.empty() && ops.front().kind == Token::Kind::Literal) {
                segment.has_first = true;
                segment.first = literals[ops.front().begin];
            }
        }
        segments.push_back(segment);
        if(i == e) {
            break;
        }
        if(i + 1 != e && s[i + 1] == '/') [[unlikely]] {
            return std::unexpected{
                GlobError{GlobError::MultipleSlash, i, i + 2, "multiple `/` is not allowed"}
            };
        }
        ++i;
    }
    arm.end = static_cast<std::uint32_t>(segments.size());

    // A trailing `**/` matches exactly like `**/*`: the separator before the
    // globstar is required, and then anything goes.
    if(arm.end - arm.begin >= 2 && segments[arm.end - 2].kind == Segment::Kind::Recursive &&
       segments.back().kind == Segment::Kind::Literal &&
       segments.back().begin == segments.back().end) {
        tokens.push_back({Token::Kind::Star});
        segments.back().end = static_cast<std::uint32_t>(tokens.size());
        segments.back().kind = Segment::Kind::Affix;
    }

    // A recursive segment can absorb the separator before or after it, but
    // not both. Compute both nullable states backwards once, in linear time.
    bool following_nullable = true, following_absorbs = false;
    for(auto index = arm.end; index-- > arm.begin;) {
        auto& segment = segments[index];
        const bool last = index + 1 == arm.end;
        const bool recursive = segment.kind == Segment::Kind::Recursive;
        segment.optional_rest = last || following_absorbs;
        if(recursive) {
            segment.nullable = last || following_nullable;
        } else {
            auto ops = std::span(tokens).subspan(segment.begin, segment.end - segment.begin);
            segment.nullable =
                segment.optional_rest && std::ranges::all_of(ops, [](const Token& op) {
                    return op.kind == Token::Kind::Star;
                });
        }
        following_absorbs = recursive && segment.optional_rest;
        following_nullable = segment.nullable;
    }
    arm.absorbs_separator = following_absorbs;

    const auto count = arm.end - arm.begin;
    const auto& first = segments[arm.begin];
    const auto& last = segments[arm.end - 1];
    const bool prefix_tree =
        count == 2 && first.kind == Segment::Kind::Literal && last.kind == Segment::Kind::Recursive;
    const bool directory_tree = count == 3 && first.kind == Segment::Kind::Recursive &&
                                segments[arm.begin + 1].kind == Segment::Kind::Literal &&
                                last.kind == Segment::Kind::Recursive;
    arm.basename = count == 2 && first.kind == Segment::Kind::Recursive &&
                   last.kind != Segment::Kind::Recursive;
    auto head = [&](const Token& token) {
        arm.head_begin = token.begin;
        arm.head_size = token.size;
    };
    if(prefix_tree) {
        arm.plan = Arm::Plan::PrefixTree;
        if(first.begin != first.end) {
            head(tokens[first.begin]);
        }
    } else if(directory_tree) {
        // `**//**` is rejected, so the directory literal is never empty.
        arm.plan = Arm::Plan::DirectoryTree;
        head(tokens[segments[arm.begin + 1].begin]);
    } else if(arm.basename || (count == 1 && first.kind != Segment::Kind::Recursive)) {
        if(last.kind == Segment::Kind::Affix && tokens[last.begin].kind == Token::Kind::Star) {
            arm.plan = arm.basename ? Arm::Plan::Suffix : Arm::Plan::SegmentSuffix;
        } else if(last.kind == Segment::Kind::Affix) {
            arm.plan = Arm::Plan::Affix;
            head(tokens[last.begin]);
        } else if(arm.basename && last.kind == Segment::Kind::Literal) {
            arm.plan = Arm::Plan::PathSuffix;
        } else {
            arm.plan = Arm::Plan::Final;
        }
    }
    if(last.kind != Segment::Kind::Recursive && last.begin != last.end &&
       tokens[last.end - 1].kind == Token::Kind::Literal) {
        const auto& text = tokens[last.end - 1];
        arm.literal.begin = text.begin;
        arm.literal.size = text.size;
        if(text.size <= 16) {
            std::array<unsigned char, 16> value{}, mask{};
            std::memcpy(value.data() + value.size() - text.size,
                        literals.data() + text.begin,
                        text.size);
            std::fill(mask.end() - text.size, mask.end(), 0xff);
            std::memcpy(&arm.literal.word, value.data() + 8, 8);
            std::memcpy(&arm.literal.mask, mask.data() + 8, 8);
            std::memcpy(&arm.literal.lead_word, value.data(), 8);
            std::memcpy(&arm.literal.lead_mask, mask.data(), 8);
        }
    }
    arms.push_back(arm);
    return {};
}

bool GlobPattern::CharClass::contains(char32_t cp) const {
    // Ranges are sorted and disjoint: the candidate range is the first one
    // whose upper bound reaches cp.
    auto it = std::ranges::lower_bound(ranges, cp, {}, &std::pair<char32_t, char32_t>::second);
    const bool hit = it != ranges.end() && it->first <= cp;
    return negated ? !hit : hit;
}

bool GlobPattern::match_arms(std::string_view str) const {
    if(!prefix.empty()) {
        if(!str.starts_with(prefix)) {
            return false;
        }
        str.remove_prefix(prefix.size());
    }
    if(prefix_at_seg_end) {
        if(str.empty()) {
            // The `/` after the prefix went unmatched; an arm absorbs it
            // only when its whole pattern matches empty behind a separator,
            // exactly as if the prefix had never been split off.
            return std::ranges::any_of(arms, &Arm::absorbs_separator);
        }
        if(str[0] != '/') {
            return false;
        }
        str.remove_prefix(1);
    }
    switch(mode) {
        case Mode::ExtensionArms: return match_extension_set(str);
        case Mode::SegmentSuffixArms:
            if(str.contains('/')) {
                return false;
            }
            [[fallthrough]];
        case Mode::SuffixArms:
            for(const auto& arm: arms) {
                if(arm.literal.matches(str, literals)) {
                    return true;
                }
            }
            return false;
        default: break;
    }
    for(const auto& arm: arms) {
        bool hit = false;
        switch(arm.plan) {
            case Arm::Plan::PrefixTree: {
                auto text = head(arm);
                hit =
                    str.starts_with(text) && (str.size() == text.size() || str[text.size()] == '/');
                break;
            }
            case Arm::Plan::DirectoryTree: {
                auto text = head(arm);
                hit = find_segment(str, 0, text.front(), text) != std::string_view::npos;
                break;
            }
            case Arm::Plan::Suffix: hit = arm.literal.matches(str, literals); break;
            case Arm::Plan::PathSuffix:
                hit = arm.literal.matches(str, literals) &&
                      (str.size() == arm.literal.size ||
                       str[str.size() - arm.literal.size - 1] == '/');
                break;
            case Arm::Plan::SegmentSuffix:
                hit = arm.literal.matches(str, literals) && !str.contains('/');
                break;
            case Arm::Plan::Affix:
            case Arm::Plan::Final: hit = match_final(arm, str); break;
            case Arm::Plan::General:
                hit = arm.literal.matches(str, literals) && execute(arm, str);
                break;
        }
        if(hit) {
            return true;
        }
    }
    return false;
}

bool GlobPattern::match_extension_set(std::string_view str) const {
    auto dot = str.find_last_of('.');
    if(dot == std::string_view::npos || str.size() - dot > 8) {
        return false;
    }
    auto length = static_cast<std::uint32_t>(str.size() - dot);
    std::uint64_t word = 0;
    if(str.size() >= sizeof(word)) {
        std::memcpy(&word, str.data() + str.size() - sizeof(word), sizeof(word));
    } else {
        std::memcpy(reinterpret_cast<char*>(&word) + sizeof(word) - str.size(),
                    str.data(),
                    str.size());
    }
    size_t first = 0, last = arms.size();
    while(first < last) {
        auto middle = first + (last - first) / 2;
        const auto& literal = arms[middle].literal;
        auto candidate = std::pair(length, word & literal.mask);
        auto key = std::pair(literal.size, literal.word);
        if(candidate == key) {
            return true;
        }
        if(candidate < key) {
            last = middle;
        } else {
            first = middle + 1;
        }
    }
    return false;
}

/// The arm's last segment must match the input's final segment: the whole
/// input for a single-segment arm, the basename after a leading `**`.
bool GlobPattern::match_final(const Arm& arm, std::string_view str) const {
    if(!arm.literal.matches(str, literals)) {
        return false;
    }
    auto name = arm.basename ? basename(str) : str;
    if(!arm.basename && name.contains('/')) {
        return false;
    }
    if(arm.plan == Arm::Plan::Affix) {
        return name.size() >= arm.head_size + arm.literal.size && name.starts_with(head(arm));
    }
    const auto& last = segments[arm.end - 1];
    if(last.kind == Segment::Kind::Literal) {
        // Verified as a suffix, and a literal never contains `/`.
        return name.size() == arm.literal.size;
    }
    return match_tokens(last, name);
}

bool GlobPattern::execute(const Arm& arm, std::string_view str) const {
    size_t segment = arm.begin, offset = 0;
    size_t retry_segment = arm.end, retry_offset = 0;
    size_t seen_offset = std::string_view::npos, seen_end = 0;
    auto segment_end = [&](size_t begin) {
        if(seen_offset != begin) {
            seen_offset = begin;
            seen_end = str.find('/', begin);
            if(seen_end == std::string_view::npos) {
                seen_end = str.size();
            }
        }
        return seen_end;
    };
    while(true) {
        if(offset == str.size()) {
            return segments[segment].nullable;
        }
        const auto& part = segments[segment];
        // Where to look for the retry segment next: right here after a
        // globstar, past the failed candidate otherwise.
        size_t from;
        if(part.kind == Segment::Kind::Recursive) {
            if(++segment == arm.end) {
                return true;
            }
            retry_segment = segment;
            retry_offset = offset;
            if(!segments[segment].has_first) {
                continue;
            }
            from = offset;
        } else {
            if(!part.has_first || str[offset] == part.first) {
                size_t end;
                bool matched;
                if(part.kind == Segment::Kind::Literal) {
                    // A literal fixes where the segment ends: no separator search.
                    auto text =
                        part.begin == part.end ? std::string_view{} : literal(tokens[part.begin]);
                    end = offset + text.size();
                    matched = end <= str.size() && (end == str.size() || str[end] == '/') &&
                              str.compare(offset, text.size(), text) == 0;
                } else {
                    end = segment_end(offset);
                    auto input = str.substr(offset, end - offset);
                    matched = part.kind == Segment::Kind::Affix ? match_affix(part, input)
                                                                : match_tokens(part, input);
                }
                if(matched) {
                    if(end == str.size()) {
                        if(part.optional_rest) {
                            return true;
                        }
                    } else if(segment + 1 != arm.end) {
                        ++segment;
                        offset = end + 1;
                        continue;
                    }
                }
            }
            if(retry_segment == arm.end) {
                return false;
            }
            // The latest globstar subsumes earlier ones. Its retry cursor
            // moves strictly forward by whole segments; fixed-depth plans
            // never retry.
            if(!segments[retry_segment].has_first) {
                auto end = segment_end(retry_offset);
                retry_offset = end == str.size() ? end : end + 1;
                offset = retry_offset;
                segment = retry_segment;
                continue;
            }
            from = retry_offset + 1;
        }
        // Only a segment starting with the retry segment's literal head can
        // match, so jump between candidates with memchr; such a segment is
        // never nullable, so running out of candidates is a mismatch.
        retry_offset = find_segment(str, from, segments[retry_segment].first, {});
        if(retry_offset == std::string_view::npos) {
            return false;
        }
        offset = retry_offset;
        segment = retry_segment;
    }
}

bool GlobPattern::match_affix(const Segment& segment, std::string_view input) const {
    const auto& head = tokens[segment.begin];
    const auto& tail = tokens[segment.end - 1];
    auto prefix = head.kind == Token::Kind::Literal ? literal(head) : std::string_view{};
    auto suffix = tail.kind == Token::Kind::Literal ? literal(tail) : std::string_view{};
    return input.size() >= prefix.size() + suffix.size() && input.starts_with(prefix) &&
           input.ends_with(suffix);
}

bool GlobPattern::match_tokens(const Segment& segment, std::string_view input) const {
    size_t token = segment.begin, offset = 0;
    // Where the latest star resumes: the token after it, with the star
    // absorbing everything before `retry_offset`.
    size_t retry_token = segment.end, retry_offset = 0;
    while(true) {
        if(token == segment.end) {
            if(offset == input.size()) {
                return true;
            }
        } else {
            const auto& op = tokens[token];
            switch(op.kind) {
                case Token::Kind::Star: {
                    retry_token = ++token;
                    retry_offset = offset;
                    if(token == segment.end) {
                        return true;
                    }
                    continue;
                }
                case Token::Kind::Literal: {
                    auto text = literal(op);
                    if(token == retry_token) {
                        // Right after the latest star: the star absorbs up to
                        // the next occurrence. Searching bytes is exact since
                        // a valid UTF-8 literal cannot start inside a
                        // multi-byte character of the input.
                        if(token + 1 == segment.end) {
                            return input.size() - offset >= text.size() && input.ends_with(text);
                        }
                        auto at = input.find(text, offset);
                        if(at == std::string_view::npos) {
                            return false;
                        }
                        retry_offset = at;
                        offset = at + text.size();
                        ++token;
                        continue;
                    }
                    if(input.substr(offset).starts_with(text)) {
                        offset += text.size();
                        ++token;
                        continue;
                    }
                    break;
                }
                case Token::Kind::Any:
                case Token::Kind::Class: {
                    if(offset != input.size()) {
                        auto atom = decode_utf8_atom(input, offset);
                        if(op.kind == Token::Kind::Any || classes[op.begin].contains(atom.cp)) {
                            offset += atom.len;
                            ++token;
                            continue;
                        }
                    }
                    break;
                }
            }
        }
        if(retry_token == segment.end || retry_offset == input.size()) {
            return false;
        }
        retry_offset += decode_utf8_atom(input, retry_offset).len;
        offset = retry_offset;
        token = retry_token;
    }
}

}  // namespace kota
