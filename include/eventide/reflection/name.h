#pragma once

#include <source_location>
#include <type_traits>

#include "../common/string_ref.h"

namespace eventide::refl::detail {

constexpr std::size_t find_last_top_level_scope(string_ref sv) {
    std::size_t pos = string_ref::npos;
    int angle = 0;
    int paren = 0;
    int bracket = 0;
    int brace = 0;

    for(std::size_t i = 0; i + 1 < sv.size(); ++i) {
        const auto ch = sv[i];
        switch(ch) {
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
            default: break;
        }

        if(ch == ':' && sv[i + 1] == ':' && angle == 0 && paren == 0 && bracket == 0 &&
           brace == 0) {
            pos = i;
            ++i;
        }
    }
    return pos;
}

constexpr string_ref unqualify_type_name(string_ref sv) {
    if(auto pos = find_last_top_level_scope(sv); pos != string_ref::npos) {
        return sv.substr(pos + 2);
    }
    return sv;
}

constexpr string_ref unwrap_wrapper_value(string_ref sv) {
    auto open = sv.rfind('{');
    auto close = sv.rfind('}');
    if(open != string_ref::npos && close != string_ref::npos && open < close) {
        sv = sv.substr(open + 1, close - open - 1);
    }
    return sv.trim();
}

constexpr string_ref extract_identifier(string_ref expression) {
    expression = expression.trim();
    while(true) {
        bool changed = false;
        while(!expression.empty() && expression.front() == '&') {
            expression = expression.drop_front();
            expression = expression.trim();
            changed = true;
        }
        if(expression.size() >= 2 && expression.front() == '(' && expression.back() == ')') {
            expression = expression.drop_front().drop_back();
            expression = expression.trim();
            changed = true;
        }
        if(!changed) {
            break;
        }
    }

    while(!expression.empty() && (expression.back() == ')' || expression.back() == ']' ||
                                  expression.back() == '}' || expression.back() == ';')) {
        expression = expression.drop_back();
        expression = expression.trim();
    }

    if(expression.starts_with("::")) {
        expression = expression.drop_front(2);
    }
    if(auto pos = expression.rfind("::"); pos != string_ref::npos) {
        expression = expression.substr(pos + 2);
    } else if(auto pos = expression.rfind(':'); pos != string_ref::npos) {
        // MSVC may format pointer NTTPs like `int*:symbol`.
        expression = expression.substr(pos + 1);
    }
    if(auto pos = expression.rfind("->"); pos != string_ref::npos) {
        expression = expression.substr(pos + 2);
    }
    if(auto pos = expression.rfind('.'); pos != string_ref::npos) {
        expression = expression.substr(pos + 1);
    }
    if(auto pos = expression.find('<'); pos != string_ref::npos) {
        expression = expression.substr(0, pos);
    }
    if(auto pos = expression.find('('); pos != string_ref::npos) {
        expression = expression.substr(0, pos);
    }

    return expression.trim();
}

/// workaround for msvc, if no such wrapper, msvc cannot print the member name.
template <typename T>
struct wrapper {
    T value;

    constexpr wrapper(T value) : value(value) {}
};

}  // namespace eventide::refl::detail

namespace eventide::refl {

template <typename T>
consteval auto type_name(bool qualified = false) {
    string_ref name = std::source_location::current().function_name();
#if __GNUC__ || __clang__
    std::size_t start = name.rfind("T =") + 3;
    std::size_t end = name.rfind("]");
    end = end == string_ref::npos ? name.size() : end;
    name = name.substr(start, end - start).trim();
#elif _MSC_VER
    std::size_t start = name.find("type_name<") + 10;
    std::size_t end = name.rfind(">(");
    name = name.slice(start, end);
    start = name.find(' ');
    name = start == string_ref::npos ? name : name.drop_front(start + 1);
#endif
    if(!qualified) {
        name = detail::unqualify_type_name(name);
    }
    return name;
}

template <auto value>
    requires std::is_enum_v<decltype(value)>
consteval auto enum_name() {
    string_ref name = std::source_location::current().function_name();
#if __GNUC__ || __clang__
    std::size_t start = name.find('=') + 2;
    std::size_t end = name.size() - 1;
#elif _MSC_VER
    std::size_t start = name.find('<') + 1;
    std::size_t end = name.rfind(">(");
#else
    static_assert(false, "Not supported compiler");
#endif
    name = name.substr(start, end - start).trim();
    auto pos = name.rfind("::");
    return pos == string_ref::npos ? name : name.substr(pos + 2);
}

template <detail::wrapper ptr>
    requires std::is_pointer_v<decltype(ptr.value)>
consteval auto pointer_name() {
    string_ref name = std::source_location::current().function_name();
    name = detail::unwrap_wrapper_value(name);
    return detail::extract_identifier(name);
}

template <detail::wrapper ptr>
    requires std::is_member_pointer_v<decltype(ptr.value)>
consteval auto member_name() {
    string_ref name = std::source_location::current().function_name();
    name = detail::unwrap_wrapper_value(name);
    return detail::extract_identifier(name);
}

}  // namespace eventide::refl
