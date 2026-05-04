#include "kota/support/glob_pattern.h"

#include <format>
#include <limits>
#include <optional>
#include <ranges>

#include "kota/support/expected_try.h"

namespace kota {

namespace {

using GlobCharSet = std::bitset<256>;

std::expected<GlobCharSet, GlobError> parse_bracket_charset(std::string_view s) {
    GlobCharSet bv{};

    for(std::uint32_t i = 0, e = static_cast<std::uint32_t>(s.size()); i < e; ++i) {
        switch(s[i]) {
            case '\\': {
                auto backslash_pos = i;
                ++i;
                if(i == e) [[unlikely]] {
                    return std::unexpected{
                        GlobError{GlobError::StrayBackslash,
                                  backslash_pos, backslash_pos + 1,
                                  "stray `\\`"}
                    };
                }
                if(s[i] != '/') {
                    bv.set(static_cast<std::uint8_t>(s[i]), true);
                }
                break;
            }

            case '-': {
                if(i == 0 || i + 1 == e) {
                    bv.set('-', true);
                    break;
                }
                auto dash_pos = i;
                auto c_begin = static_cast<std::uint8_t>(s[i - 1]);
                auto c_end = static_cast<std::uint8_t>(s[i + 1]);
                ++i;
                if(c_end == '\\') {
                    auto backslash_pos = i;
                    ++i;
                    if(i == e) [[unlikely]] {
                        return std::unexpected{
                            GlobError{GlobError::StrayBackslash,
                                      backslash_pos, backslash_pos + 1,
                                      "stray `\\`"}
                        };
                    }
                    c_end = static_cast<std::uint8_t>(s[i]);
                }
                if(c_begin > c_end) [[unlikely]] {
                    return std::unexpected{
                        GlobError{GlobError::InvalidRange,
                                  dash_pos - 1,
                                  dash_pos + 2,
                                  std::format("`{}` is larger than `{}`", c_begin, c_end)}
                    };
                }
                for(std::uint32_t c = c_begin; c <= c_end; ++c) {
                    if(c != '/') {
                        bv.set(static_cast<std::uint8_t>(c), true);
                    }
                }
                break;
            }

            default: {
                if(s[i] != '/') {
                    bv.set(static_cast<std::uint8_t>(s[i]), true);
                }
            }
        }
    }

    return bv;
}

std::expected<small_vector<std::string, 1>, GlobError>
    glob_parse_brace_expansions(std::string_view s, size_t max_subpattern_num) {
    small_vector<std::string, 1> subpatterns;
    subpatterns.emplace_back(s);
    if(max_subpattern_num == 0 || !s.contains('{')) {
        return subpatterns;
    }

    struct BraceExpansion {
        std::uint32_t start;
        std::uint32_t length;
        small_vector<std::string_view, 2> terms;
    };

    small_vector<BraceExpansion, 0> brace_expansions;

    BraceExpansion* current_be = nullptr;
    std::uint32_t term_begin = 0;
    for(std::uint32_t i = 0, e = static_cast<std::uint32_t>(s.size()); i != e; ++i) {
        if(s[i] == '[') {
            auto bracket_pos = i;
            ++i;
            if(i == e) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::UnmatchedBracket,
                              bracket_pos, bracket_pos + 1,
                              "unmatched `[`"}
                };
            }
            if(s[i] == ']') {
                ++i;
            }
            while(i != e && s[i] != ']') {
                if(s[i] == '\\') {
                    auto backslash_pos = i;
                    ++i;
                    if(i == e) [[unlikely]] {
                        return std::unexpected{
                            GlobError{GlobError::StrayBackslash,
                                      backslash_pos, backslash_pos + 1,
                                      "unmatched `[` with stray `\\` inside"}
                        };
                    }
                }
                ++i;
            }
            if(i == e) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::UnmatchedBracket,
                              bracket_pos, bracket_pos + 1,
                              "unmatched `[`"}
                };
            }
        } else if(s[i] == '{') {
            if(current_be) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::NestedBrace,
                              i, i + 1,
                              "nested brace expansions are not supported"}
                };
            }
            current_be = &brace_expansions.emplace_back();
            current_be->start = i;
            term_begin = i + 1;
        } else if(s[i] == ',') {
            if(!current_be) {
                continue;
            }
            current_be->terms.push_back(s.substr(term_begin, i - term_begin));
            term_begin = i + 1;
        } else if(s[i] == '}') {
            if(!current_be) {
                continue;
            }
            if(current_be->terms.empty() && i - term_begin == 0) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::EmptyBrace,
                              current_be->start,
                              i + 1,
                              "empty brace expression"}
                };
            }
            current_be->terms.push_back(s.substr(term_begin, i - term_begin));
            current_be->length = i - current_be->start + 1;
            current_be = nullptr;
        } else if(s[i] == '\\') {
            auto backslash_pos = i;
            ++i;
            if(i == e) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::StrayBackslash,
                              backslash_pos, backslash_pos + 1,
                              "stray `\\`"}
                };
            }
        }
    }

    if(current_be) [[unlikely]] {
        return std::unexpected{
            GlobError{GlobError::IncompleteBrace,
                      current_be->start,
                      current_be->start + 1,
                      "incomplete brace expansion"}
        };
    }

    size_t subpattern_num = 1;
    for(auto& be: brace_expansions) {
        if(subpattern_num > std::numeric_limits<size_t>::max() / be.terms.size()) {
            subpattern_num = std::numeric_limits<size_t>::max();
            break;
        }
        subpattern_num *= be.terms.size();
    }

    if(subpattern_num > max_subpattern_num) [[unlikely]] {
        return std::unexpected{
            GlobError{GlobError::TooManyExpansions, 0, 0, "too many brace expansions"}
        };
    }

    for(auto& be: brace_expansions | std::views::reverse) {
        small_vector<std::string, 1> orig_sub_patterns;
        std::swap(subpatterns, orig_sub_patterns);
        for(std::string_view term: be.terms) {
            for(std::string_view orig: orig_sub_patterns) {
                subpatterns.emplace_back(orig).replace(be.start, be.length, term);
            }
        }
    }

    return subpatterns;
}

}  // namespace

std::expected<GlobPattern, GlobError> GlobPattern::create(std::string_view s,
                                                          size_t max_subpattern_num) {
    GlobPattern pat;
    size_t prefix_size = s.find_first_of("?*[{\\");
    auto check_consecutive_slashes = [](std::string_view str) -> std::optional<std::uint32_t> {
        bool prev_was_slash = false;
        for(std::uint32_t i = 0, e = static_cast<std::uint32_t>(str.size()); i < e; ++i) {
            if(str[i] == '/') {
                if(prev_was_slash) {
                    return i;
                }
                prev_was_slash = true;
            } else {
                prev_was_slash = false;
            }
        }
        return std::nullopt;
    };

    if(prefix_size == std::string_view::npos) {
        pat.prefix = std::string(s);
        if(auto pos = check_consecutive_slashes(pat.prefix)) [[unlikely]] {
            return std::unexpected{
                GlobError{GlobError::MultipleSlash,
                          *pos - 1,
                          *pos + 1,
                          "multiple `/` is not allowed"}
            };
        }
        return pat;
    }
    if(auto pos = check_consecutive_slashes(s.substr(0, prefix_size))) [[unlikely]] {
        return std::unexpected{
            GlobError{GlobError::MultipleSlash, *pos - 1, *pos + 1, "multiple `/` is not allowed"}
        };
    }
    if(prefix_size != 0 && s[prefix_size - 1] == '/') {
        pat.prefix_at_seg_end = true;
        --prefix_size;
    }
    pat.prefix = std::string(s.substr(0, prefix_size));
    s = s.substr(pat.prefix_at_seg_end ? prefix_size + 1 : prefix_size);

    KOTA_EXPECTED_TRY_V(auto sub_pats, glob_parse_brace_expansions(s, max_subpattern_num));

    for(auto& sub_pat: sub_pats) {
        KOTA_EXPECTED_TRY_V(auto res, SubGlobPattern::create(sub_pat));
        pat.sub_globs.push_back(std::move(res));
    }

    return pat;
}

std::expected<GlobPattern::SubGlobPattern, GlobError>
    GlobPattern::SubGlobPattern::create(std::string_view s) {
    SubGlobPattern pat;
    small_vector<GlobSegment, 6> glob_segments;
    GlobSegment* current_gs = &glob_segments.emplace_back();
    current_gs->start = 0;
    pat.pat.assign(s);

    std::uint32_t e = static_cast<std::uint32_t>(s.size());

    auto parse_bracket = [&](std::uint32_t i) -> std::expected<std::uint32_t, GlobError> {
        auto bracket_pos = i - 1;
        std::uint32_t j = i;
        if(j == e) [[unlikely]] {
            return std::unexpected{
                GlobError{GlobError::UnmatchedBracket,
                          bracket_pos, bracket_pos + 1,
                          "unmatched `[`"}
            };
        }
        if(s[j] == ']') {
            ++j;
        }
        while(j != e && s[j] != ']') {
            ++j;
            if(s[j - 1] == '\\') {
                if(j == e) [[unlikely]] {
                    return std::unexpected{
                        GlobError{GlobError::StrayBackslash,
                                  j - 1,
                                  j, "unmatched `[` with stray `\\` inside"}
                    };
                }
                ++j;
            }
        }
        if(j == e) [[unlikely]] {
            return std::unexpected{
                GlobError{GlobError::UnmatchedBracket,
                          bracket_pos, bracket_pos + 1,
                          "unmatched `[`"}
            };
        }

        std::string_view chars = s.substr(i, j - i);
        bool invert = s[i] == '^' || s[i] == '!';
        auto bv = invert ? parse_bracket_charset(chars.substr(1)) : parse_bracket_charset(chars);
        if(!bv.has_value()) [[unlikely]] {
            return std::unexpected{std::move(bv.error())};
        }
        if(invert) {
            bv->flip();
            bv->set('/', false);
        }
        pat.brackets.push_back(Bracket{j + 1, std::move(*bv)});
        return j;
    };

    for(std::uint32_t i = 0; i < e; ++i) {
        if(!current_gs) {
            current_gs = &glob_segments.emplace_back();
            current_gs->start = i;
        }
        if(s[i] == '[') {
            auto result = parse_bracket(i + 1);
            if(!result.has_value()) [[unlikely]] {
                return std::unexpected{std::move(result.error())};
            }
            i = *result;
        } else if(s[i] == '\\') {
            auto backslash_pos = i;
            if(++i == e) [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::StrayBackslash,
                              backslash_pos, backslash_pos + 1,
                              "stray `\\`"}
                };
            }
        } else if(s[i] == '/') {
            if(i > 0 && s[i - 1] == '/') [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::MultipleSlash,
                              i - 1,
                              i + 1,
                              "multiple `/` is not allowed"}
                };
            }
            current_gs->end = i;
            current_gs = nullptr;
        } else if(s[i] == '*') {
            if(i + 2 < e && s[i + 1] == '*' && s[i + 2] == '*') [[unlikely]] {
                return std::unexpected{
                    GlobError{GlobError::MultipleStar, i, i + 3, "multiple `*` is not allowed"}
                };
            }
        }
    }

    if(current_gs) {
        current_gs->end = e;
    }

    pat.glob_segments.assign(std::move(glob_segments));
    return pat;
}

bool GlobPattern::match(std::string_view sv) const {
    string_ref str(sv);
    if(!str.consume_front(prefix)) {
        return false;
    }

    if(str.empty() && sub_globs.empty()) {
        return true;
    }

    if(!str.empty() && prefix_at_seg_end) {
        if(str[0] != '/') {
            return false;
        }
        str = str.substr(1);
    }

    for(auto& glob: sub_globs) {
        if(glob.match(str)) {
            return true;
        }
    }
    return false;
}

/// Maximum number of backtrack iterations before aborting a match.
/// This provides protection against ReDoS (Regular expression Denial of Service)
/// attacks where crafted patterns and inputs could cause exponential backtracking.
constexpr static size_t max_backtrack_iterations = 65536;

bool GlobPattern::SubGlobPattern::match(std::string_view str) const {
    const char* s = str.data();
    const char* const s_start = s;
    const char* const s_end = s + str.size();
    const char* p = pat.data();
    const char* seg_start = p;
    const char* const p_start = p;
    const char* const p_end = p + pat.size();
    const char* seg_end = p + glob_segments[0].end;
    size_t b = 0;
    size_t current_glob_seg = 0;
    bool wild_mode = false;

    small_vector<BacktrackState, 6> backtrack_stack;
    const size_t seg_num = glob_segments.size();
    size_t backtrack_iterations = 0;

    auto segment_range = [&](size_t idx) -> std::pair<const char*, const char*> {
        return {p_start + glob_segments[idx].start, p_start + glob_segments[idx].end};
    };

    auto push_backtrack =
        [&backtrack_stack, &b, &current_glob_seg, &wild_mode, &p, &s, &seg_end, &seg_start]() {
            backtrack_stack.push_back({.b = b,
                                       .glob_seg = current_glob_seg,
                                       .wild_mode = wild_mode,
                                       .p = p,
                                       .s = s,
                                       .seg_end = seg_end,
                                       .seg_start = seg_start});
        };

    while(current_glob_seg < seg_num) {
        if(s == s_end) {
            return pattern().find_first_not_of("*/", p - pat.data()) == std::string_view::npos;
        }

        // Handle segment boundary first (early-continue)
        if(p == seg_end && seg_end != p_end) {
            if(wild_mode) {
                ++current_glob_seg;
                while(s != s_end && *s != '/') {
                    ++s;
                }
                if(s != s_end && *s == '/') {
                    ++s;
                }
                if(current_glob_seg >= seg_num) [[unlikely]] {
                    return s == s_end;
                }
                auto [new_start, new_end] = segment_range(current_glob_seg);
                p = new_start;
                seg_start = new_start;
                seg_end = new_end;
                continue;
            }
            // non-wild segment transition
            if(*seg_end != *s) {
                return false;
            }
            while(s != s_end && *s == '/') {
                ++s;
            }
            ++current_glob_seg;
            if(current_glob_seg >= seg_num) [[unlikely]] {
                break;
            }
            auto [new_start, new_end] = segment_range(current_glob_seg);
            p = new_start;
            seg_start = new_start;
            seg_end = new_end;
            continue;
        }

        if(p != seg_end) {
            switch(*p) {
                case '*': {
                    // Handle single `*` first (simpler case)
                    if(p + 1 == p_end || *(p + 1) != '*') {
                        ++p;
                        wild_mode = false;
                        if(p == seg_end) {
                            while(s != s_end && *s != '/') {
                                ++s;
                            }
                            if(s == s_end) {
                                continue;
                            }
                            if(s + 1 != s_end) {
                                ++s;
                            }
                            if(current_glob_seg + 1 == seg_num) {
                                return true;
                            }
                            ++current_glob_seg;
                            auto [new_start, new_end] = segment_range(current_glob_seg);
                            p = new_start;
                            seg_start = new_start;
                            seg_end = new_end;
                        }
                        push_backtrack();
                        continue;
                    }
                    // Handle `**` case
                    p += 2;
                    wild_mode = true;
                    // Consume additional stars within this segment only
                    while(p != seg_end && *p == '*') {
                        ++p;
                    }
                    if(p == seg_end) {
                        if(current_glob_seg + 1 == seg_num) {
                            return true;
                        }
                        ++current_glob_seg;
                        while(s != s_end && *s == '/') {
                            ++s;
                        }
                        auto [new_start, new_end] = segment_range(current_glob_seg);
                        p = new_start;
                        seg_start = new_start;
                        seg_end = new_end;
                    }
                    push_backtrack();
                    continue;
                }

                case '?': {
                    if(s != s_end && *s != '/') {
                        ++p;
                        ++s;
                        continue;
                    }
                    break;
                }

                case '[': {
                    if(b < brackets.size() && brackets[b].bytes[std::uint8_t(*s)]) {
                        if(p == seg_start && !(s == s_start || *(s - 1) == '/')) {
                            break;
                        }
                        p = pat.data() + brackets[b].next_offset;
                        ++b;
                        ++s;
                        continue;
                    }
                    break;
                }

                case '\\': {
                    if(p + 1 != seg_end && *(p + 1) == *s) {
                        if(p == seg_start && !(s == s_start || *(s - 1) == '/')) {
                            break;
                        }
                        p += 2;
                        ++s;
                        continue;
                    }
                    break;
                }

                default: {
                    if(*p == *s) {
                        if(p == seg_start && !(s == s_start || *(s - 1) == '/')) {
                            break;
                        }
                        ++p;
                        ++s;
                        continue;
                    }
                    break;
                }
            }
        }
        // p == seg_end == p_end, or switch fell through: backtrack

        if(backtrack_stack.empty()) [[unlikely]] {
            return false;
        }

        if(++backtrack_iterations > max_backtrack_iterations) [[unlikely]] {
            return false;
        }

        auto& state = backtrack_stack.back();

        if(state.s >= s_end) [[unlikely]] {
            backtrack_stack.pop_back();
            continue;
        }
        s = ++state.s;
        p = state.p;
        b = state.b;
        current_glob_seg = state.glob_seg;
        wild_mode = state.wild_mode;
        seg_start = state.seg_start;
        seg_end = state.seg_end;

        if(s > s_end) [[unlikely]] {
            backtrack_stack.pop_back();
            continue;
        }

        if(!wild_mode && (s == s_end || *s == '/')) {
            backtrack_stack.pop_back();
            continue;
        }
    }

    return s == s_end;
}

}  // namespace kota
