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

}  // namespace detail

/// Glob pattern matcher supporting VS Code-style glob syntax.
///
/// Supported syntax:
/// - `*` to match one or more characters in a path segment
/// - `?` to match on one character in a path segment
/// - `**` to match any number of path segments, including none
/// - `{}` to group conditions (e.g. `**.{ts,js}`)
/// - `[]` to declare a range of characters (e.g., `example.[0-9]`)
/// - `[!...]` to negate a range (e.g., `example.[!0-9]`)
///
/// Note: Use only `/` for path segment separator
class GlobPattern {
public:
    static std::expected<GlobPattern, std::string> create(std::string_view s,
                                                          size_t max_subpattern_num = 100);

    bool isTrivialMatchAll() const {
        if(!prefix.empty()) {
            return false;
        }
        if(sub_globs.size() == 1) {
            auto pat = sub_globs[0].getPat();
            return pat == "*" || pat == "**";
        }
        return false;
    }

    bool match(std::string_view s) const;

private:
    std::string prefix;
    bool prefix_at_seg_end = false;

    struct SubGlobPattern {
        static std::expected<SubGlobPattern, std::string> create(std::string_view s);
        bool match(std::string_view str) const;

        std::string_view getPat() const {
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
    };

    small_vector<SubGlobPattern, 1> sub_globs;
};

// --- Implementation ---

namespace detail {

inline std::expected<GlobCharSet, std::string> glob_expand(std::string_view s) {
    GlobCharSet bv{false};

    for(size_t i = 0, e = s.size(); i < e; ++i) {
        switch(s[i]) {
            case '\\': {
                ++i;
                if(i == e) {
                    return std::unexpected{"Invalid expansions: stray `\\`"};
                }
                if(s[i] != '/') {
                    bv.set(static_cast<uint8_t>(s[i]), true);
                }
                break;
            }

            case '-': {
                if(i == 0 || i + 1 == e) {
                    bv.set('-', true);
                    break;
                }
                char c_begin = s[i - 1];
                char c_end = s[i + 1];
                ++i;
                if(c_end == '\\') {
                    ++i;
                    if(i == e) {
                        return std::unexpected{"Invalid expansions: stray `\\`"};
                    }
                    c_end = s[i];
                }
                if(c_begin > c_end) {
                    return std::unexpected{
                        std::format("Invalid expansion: `{}` is larger than `{}`", c_begin, c_end)};
                }
                for(int c = static_cast<uint8_t>(c_begin); c <= static_cast<uint8_t>(c_end); ++c) {
                    if(c != '/') {
                        bv.set(static_cast<uint8_t>(c), true);
                    }
                }
                break;
            }

            default: {
                if(s[i] != '/') {
                    bv.set(static_cast<uint8_t>(s[i]), true);
                }
            }
        }
    }

    return bv;
}

inline std::expected<small_vector<std::string, 1>, std::string>
    glob_parse_brace_expansions(std::string_view s, size_t max_subpattern_num) {
    small_vector<std::string, 1> subpatterns;
    subpatterns.emplace_back(s);
    if(max_subpattern_num == 0 || !s.contains('{')) {
        return subpatterns;
    }

    struct BraceExpansion {
        size_t start;
        size_t length;
        small_vector<std::string_view, 2> terms;
    };

    small_vector<BraceExpansion, 0> brace_expansions;

    BraceExpansion* current_be = nullptr;
    size_t term_begin = 0;
    for(size_t i = 0, e = s.size(); i != e; ++i) {
        if(s[i] == '[') {
            ++i;
            if(i == e) {
                return std::unexpected{"Invalid glob pattern, unmatched '['"};
            }
            if(s[i] == ']') {
                ++i;
            }
            while(i != e && s[i] != ']') {
                if(s[i] == '\\') {
                    ++i;
                    if(i == e) {
                        return std::unexpected{
                            "Invalid glob pattern, unmatched '[', with stray '\\' inside"};
                    }
                }
                ++i;
            }
            if(i == e) {
                return std::unexpected{"Invalid glob pattern, unmatched '['"};
            }
        } else if(s[i] == '{') {
            if(current_be) {
                return std::unexpected{"Nested brace expansions are not supported"};
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
            if(current_be->terms.empty() && i - term_begin == 0) {
                return std::unexpected{"Empty brace expression is not supported"};
            }
            current_be->terms.push_back(s.substr(term_begin, i - term_begin));
            current_be->length = i - current_be->start + 1;
            current_be = nullptr;
        } else if(s[i] == '\\') {
            ++i;
            if(i == e) {
                return std::unexpected{"Invalid glob pattern, stray '\\'"};
            }
        }
    }

    if(current_be) {
        return std::unexpected{"Incomplete brace expansion"};
    }

    size_t subpattern_num = 1;
    for(auto& be: brace_expansions) {
        if(subpattern_num > std::numeric_limits<size_t>::max() / be.terms.size()) {
            subpattern_num = std::numeric_limits<size_t>::max();
            break;
        }
        subpattern_num *= be.terms.size();
    }

    if(subpattern_num > max_subpattern_num) {
        return std::unexpected{"Too many brace expansions"};
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

}  // namespace detail

inline std::expected<GlobPattern, std::string> GlobPattern::create(std::string_view s,
                                                                   size_t max_subpattern_num) {
    GlobPattern pat;
    size_t prefix_size = s.find_first_of("?*[{\\");
    auto check_consecutive_slashes = [](std::string_view str) -> bool {
        bool prev_was_slash = false;
        for(char c: str) {
            if(c == '/') {
                if(prev_was_slash) {
                    return true;
                }
                prev_was_slash = true;
            } else {
                prev_was_slash = false;
            }
        }
        return false;
    };

    if(prefix_size == std::string_view::npos) {
        pat.prefix = std::string(s);
        if(check_consecutive_slashes(pat.prefix)) {
            return std::unexpected{"Multiple `/` is not allowed"};
        }
        return pat;
    }
    if(prefix_size != 0 && s[prefix_size - 1] == '/') {
        pat.prefix_at_seg_end = true;
        --prefix_size;
    }
    pat.prefix = std::string(s.substr(0, prefix_size));
    if(check_consecutive_slashes(pat.prefix)) {
        return std::unexpected{"Multiple `/` is not allowed"};
    }
    s = s.substr(pat.prefix_at_seg_end ? prefix_size + 1 : prefix_size);

    auto sub_pats = detail::glob_parse_brace_expansions(s, max_subpattern_num);
    if(!sub_pats.has_value()) {
        return std::unexpected{std::move(sub_pats.error())};
    }

    for(auto& sub_pat: *sub_pats) {
        auto res = SubGlobPattern::create(sub_pat);
        if(!res.has_value()) {
            return std::unexpected{std::move(res.error())};
        }
        pat.sub_globs.push_back(std::move(*res));
    }

    return pat;
}

inline std::expected<GlobPattern::SubGlobPattern, std::string>
    GlobPattern::SubGlobPattern::create(std::string_view s) {
    SubGlobPattern pat;
    small_vector<GlobSegment, 6> glob_segments;
    GlobSegment* current_gs = &glob_segments.emplace_back();
    current_gs->start = 0;
    pat.pat.assign(s);

    size_t e = s.size();
    for(size_t i = 0; i < e; ++i) {
        if(!current_gs) {
            current_gs = &glob_segments.emplace_back();
            current_gs->start = i;
        }
        if(s[i] == '[') {
            ++i;
            size_t j = i;
            if(j == e) {
                return std::unexpected{"Invalid glob pattern, unmatched '['"};
            }
            if(s[j] == ']') {
                ++j;
            }
            while(j != e && s[j] != ']') {
                ++j;
                if(s[j - 1] == '\\') {
                    if(j == e) {
                        return std::unexpected{
                            "Invalid glob pattern, unmatched '[' with stray '\\' inside"};
                    }
                    ++j;
                }
            }
            if(j == e) {
                return std::unexpected{"Invalid glob pattern, unmatched '['"};
            }
            std::string_view chars = s.substr(i, j - i);
            bool invert = s[i] == '^' || s[i] == '!';
            auto bv = invert ? detail::glob_expand(chars.substr(1)) : detail::glob_expand(chars);
            if(!bv.has_value()) {
                return std::unexpected{std::move(bv.error())};
            }
            if(invert) {
                bv->flip();
                bv->set('/', false);
            }
            pat.brackets.push_back(Bracket{j + 1, std::move(*bv)});
            i = j;
        } else if(s[i] == '\\') {
            if(++i == e) {
                return std::unexpected{"Invalid glob pattern, stray '\\'"};
            }
        } else if(s[i] == '/') {
            if(i > 0 && s[i - 1] == '/') {
                return std::unexpected{"Multiple `/` is not allowed"};
            }
            current_gs->end = i;
            current_gs = nullptr;
        } else if(s[i] == '*') {
            if(i + 2 < e && s[i + 1] == '*' && s[i + 2] == '*') {
                return std::unexpected{"Multiple `*` is not allowed"};
            }
        }
    }

    if(current_gs) {
        current_gs->end = e;
    }

    pat.glob_segments.assign(std::move(glob_segments));
    return pat;
}

inline bool GlobPattern::match(std::string_view sv) const {
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

inline bool GlobPattern::SubGlobPattern::match(std::string_view str) const {
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

    struct BacktrackState {
        size_t b;
        size_t glob_seg;
        bool wild_mode;
        const char* p;
        const char* s;
        const char* seg_end;
        const char* seg_start;
    };

    small_vector<BacktrackState, 6> backtrack_stack;
    const size_t seg_num = glob_segments.size();

    auto save_stat =
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
            return getPat().find_first_not_of("*/", p - pat.data()) == std::string_view::npos;
        }

        if(p != seg_end) {
            switch(*p) {
                case '*': {
                    if(p + 1 != p_end && *(p + 1) == '*') {
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
                            p = p_start + glob_segments[current_glob_seg].start;
                            seg_start = p;
                            seg_end = p_start + glob_segments[current_glob_seg].end;
                        }
                        save_stat();
                    } else {
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
                            p = p_start + glob_segments[current_glob_seg].start;
                            seg_start = p;
                            seg_end = p_start + glob_segments[current_glob_seg].end;
                        }
                        save_stat();
                    }
                    continue;
                }

                case '?': {
                    if(p + 1 != seg_end && *(p + 1) == '*') {
                        ++s;
                        save_stat();
                        p += 2;
                        continue;
                    }
                    if(s != s_end && *s != '/') {
                        ++p;
                        ++s;
                        continue;
                    }
                    break;
                }

                case '[': {
                    if(b < brackets.size() && brackets[b].bytes[uint8_t(*s)]) {
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

        } else {
            if(seg_end != p_end) {
                if(wild_mode) {
                    ++current_glob_seg;
                    while(s != s_end && *s != '/') {
                        ++s;
                    }
                    if(s != s_end && *s == '/') {
                        ++s;
                    }
                    if(current_glob_seg >= seg_num) {
                        return s == s_end;
                    }
                    p = p_start + glob_segments[current_glob_seg].start;
                    seg_start = p;
                    seg_end = p_start + glob_segments[current_glob_seg].end;
                    continue;
                } else {
                    if(*seg_end != *s) {
                        return false;
                    }
                    while(s != s_end && *s == '/') {
                        ++s;
                    }
                    ++current_glob_seg;
                    if(current_glob_seg >= seg_num) {
                        break;
                    }
                    p = p_start + glob_segments[current_glob_seg].start;
                    seg_start = p;
                    seg_end = p_start + glob_segments[current_glob_seg].end;
                    continue;
                }
            }
        }

        if(backtrack_stack.empty()) {
            return false;
        }

        auto& state = backtrack_stack.back();

        p = state.p;
        s = ++state.s;
        b = state.b;
        current_glob_seg = state.glob_seg;
        wild_mode = state.wild_mode;
        seg_start = state.seg_start;
        seg_end = state.seg_end;

        if(s > s_end) {
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
