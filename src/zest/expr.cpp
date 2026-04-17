#include "kota/zest/detail/check.h"

namespace {

std::string_view trim_expr(std::string_view sv) {
    while(!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\n' ||
                          sv.front() == '\v' || sv.front() == '\f' || sv.front() == '\r')) {
        sv.remove_prefix(1);
    }
    while(!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\n' ||
                          sv.back() == '\v' || sv.back() == '\f' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }
    return sv;
}

}  // namespace

namespace kota::zest {

binary_expr_pair parse_binary_exprs(std::string_view exprs) {
    int angle = 0;
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for(std::size_t i = 0; i < exprs.size(); ++i) {
        const auto ch = exprs[i];

        if(escaped) {
            escaped = false;
            continue;
        }

        if(in_single_quote) {
            if(ch == '\\') {
                escaped = true;
            } else if(ch == '\'') {
                in_single_quote = false;
            }
            continue;
        }

        if(in_double_quote) {
            if(ch == '\\') {
                escaped = true;
            } else if(ch == '"') {
                in_double_quote = false;
            }
            continue;
        }

        switch(ch) {
            case '\'': in_single_quote = true; break;
            case '"': in_double_quote = true; break;
            case '<': ++angle; break;
            case '>':
                if(angle > 0) {
                    --angle;
                }
                break;
            case '(': ++paren; break;
            case ')':
                if(paren > 0) {
                    --paren;
                }
                break;
            case '[': ++bracket; break;
            case ']':
                if(bracket > 0) {
                    --bracket;
                }
                break;
            case '{': ++brace; break;
            case '}':
                if(brace > 0) {
                    --brace;
                }
                break;
            case ',':
                if(angle == 0 && paren == 0 && bracket == 0 && brace == 0) {
                    return {trim_expr(exprs.substr(0, i)), trim_expr(exprs.substr(i + 1))};
                }
                break;
            default: break;
        }
    }

    return {trim_expr(exprs), "<unknown>"};
}

}  // namespace kota::zest
