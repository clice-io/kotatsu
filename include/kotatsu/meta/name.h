#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <source_location>
#include <string_view>
#include <type_traits>

#include "kotatsu/support/string_ref.h"

namespace kotatsu::meta::detail {

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
    if(auto pos = find_last_top_level_scope(expression); pos != string_ref::npos) {
        expression = expression.substr(pos + 2);
    } else if(auto colon = expression.rfind(':'); colon != string_ref::npos) {
        // MSVC may format pointer NTTPs like `int*:symbol`.
        expression = expression.substr(colon + 1);
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

/// Prints a whole pack of values into one signature, so that reflecting the members of an
/// enum or a struct takes one instantiation for many of them rather than one each.
///
/// Two things here are load-bearing: GCC and clang print the parameter's name ahead of the
/// values, and split_pack looks for it, so it stays `Vs`; and the return type stays free of
/// angle brackets, as MSVC prints it ahead of the argument list split_pack finds by its first
/// `<`.
template <auto... Vs>
consteval const char* pack_signature() {
    return std::source_location::current().function_name();
}

/// The N values printed in a pack_signature, each from its last top-level `::`. An element the
/// compiler spelled as an expression rather than a name, as the cast it prints for an enum
/// value no enumerator has, comes back empty.
///
/// Constant evaluation costs microseconds a character, so the signature is walked once, its
/// lowercase letters, most of it, passed over first, and its length never measured.
template <std::size_t N>
constexpr std::array<string_ref, N> split_pack(const char* signature) {
#if defined(__clang__)
    constexpr string_ref prefix = "Vs = <";
#elif defined(__GNUC__)
    constexpr string_ref prefix = "Vs = {";
#elif defined(_MSC_VER)
    constexpr string_ref prefix = "<";
#else
    static_assert(false, "Not supported compiler");
#endif
    std::array<string_ref, N> parts{};

    std::size_t start = 0;
    while(signature[start] != '\0') {
        std::size_t matched = 0;
        while(matched < prefix.size() && signature[start + matched] == prefix[matched]) {
            ++matched;
        }
        if(matched == prefix.size()) {
            start += matched;
            break;
        }
        ++start;
    }

    std::size_t index = 0;
    std::size_t name = start;
    int depth = 0;
    bool spelled_out = false;

    for(std::size_t i = start; index < N && signature[i] != '\0'; ++i) {
        const char c = signature[i];
        if(c >= 'a' && c <= 'z') {
            continue;
        }
        if(c == '\'' || c == '`') {
            // A char literal, '<' say, holds no delimiter, and neither does a name MSVC quotes,
            // `anonymous-namespace'.
            for(++i; signature[i] != '\''; ++i) {
                i += signature[i] == '\\';
            }
        } else if(c == '-' && signature[i + 1] == '>') {
            ++i;
        } else if(c == '<' || c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if(c == '>' || c == ')' || c == ']' || c == '}') {
            if(depth == 0) {
                parts[index] = spelled_out ? string_ref{} : string_ref(signature + name, i - name);
                break;
            }
            --depth;
            spelled_out = spelled_out || (depth == 0 && c == ')');
        } else if(depth == 0 && c == ',') {
            parts[index++] = spelled_out ? string_ref{} : string_ref(signature + name, i - name);
            name = i + (signature[i + 1] == ' ' ? 2 : 1);
            spelled_out = false;
        } else if(depth == 0 && c == ':' && signature[i + 1] == ':') {
            name = i + 2;
            spelled_out = false;
            ++i;
        }
    }
    return parts;
}

}  // namespace kotatsu::meta::detail

namespace kotatsu::meta {

template <typename T>
constexpr auto type_name(bool qualified = false) {
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
    // MSVC names a class type with its key, `struct Foo`; `unsigned int` is
    // a name of its own.
    for(std::string_view key: {"class ", "struct ", "enum ", "union "}) {
        if(name.starts_with(key)) {
            name = name.drop_front(key.size());
            break;
        }
    }
#endif
    if(!qualified) {
        name = detail::unqualify_type_name(name);
    }
    return name;
}

template <auto value>
    requires std::is_enum_v<decltype(value)>
constexpr auto enum_name() {
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

}  // namespace kotatsu::meta
