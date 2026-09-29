#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kota::naming {

constexpr bool is_lower(char c) {
    return c >= 'a' && c <= 'z';
}

constexpr bool is_upper(char c) {
    return c >= 'A' && c <= 'Z';
}

constexpr bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

constexpr bool is_alpha(char c) {
    return is_lower(c) || is_upper(c);
}

constexpr bool is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

constexpr char to_lower(char c) {
    return is_upper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr char to_upper(char c) {
    return is_lower(c) ? static_cast<char>(c - 'a' + 'A') : c;
}

/// A byte of a UTF-8 sequence, which renaming keeps as it is: part of a word, without case.
constexpr bool is_non_ascii(char c) {
    return static_cast<unsigned char>(c) >= 0x80;
}

/// `text` in lower snake case: words split at separators (anything not a letter, digit or
/// UTF-8 byte) and at case changes ("HTTPServer2Go" is "http_server2_go"), lowered and joined
/// by one `_`, without leading or trailing ones.
constexpr std::string normalize_to_lower_snake(std::string_view text) {
    // A UTF-8 byte continues a word like a lowercase letter.
    auto is_word = [](char c) {
        return is_alnum(c) || is_non_ascii(c);
    };
    auto is_lower_like = [](char c) {
        return is_lower(c) || is_non_ascii(c);
    };
    std::string out;
    for(std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if(is_upper(c)) {
            // Within a word, an uppercase letter starts a new one after a lowercase letter or
            // a digit ("fooBar", "v2Go"), and ends a run of capitals before a lowercase letter
            // ("HTTPServer").
            const bool in_word = !out.empty() && out.back() != '_';
            if(in_word && (is_lower_like(text[i - 1]) || is_digit(text[i - 1]) ||
                           (i + 1 < text.size() && is_lower_like(text[i + 1])))) {
                out += '_';
            }
            out += to_lower(c);
        } else if(is_word(c)) {
            out += c;
        } else if(!out.empty() && out.back() != '_') {
            out += '_';
        }
    }
    // A separator only ever follows a word, so the one left to drop is a trailing one.
    if(!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

constexpr std::string snake_to_camel(std::string_view text, bool upper_first) {
    auto snake = normalize_to_lower_snake(text);
    std::string out;
    bool capitalize_next = upper_first;
    bool seen_output = false;
    for(auto c: snake) {
        if(c == '_') {
            capitalize_next = true;
            continue;
        }
        if(capitalize_next && is_alpha(c)) {
            out += to_upper(c);
        } else if(!seen_output) {
            out += upper_first ? to_upper(c) : to_lower(c);
        } else {
            out += c;
        }
        capitalize_next = false;
        seen_output = true;
    }
    return out;
}

constexpr std::string normalize_identifier(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for(char c: text) {
        if(is_alnum(c)) {
            out.push_back(c);
        } else if(out.empty() || out.back() != '_') {
            out.push_back('_');
        }
    }
    while(!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if(out.empty()) {
        return "unnamed";
    }
    if(is_digit(out.front())) {
        out.insert(out.begin(), '_');
    }
    return out;
}

constexpr std::string snake_to_upper(std::string_view text) {
    auto snake = normalize_to_lower_snake(text);
    for(auto& c: snake)
        c = to_upper(c);
    return snake;
}

namespace rename_policy {

struct identity {
    std::string operator()(bool, std::string_view value) const {
        return std::string(value);
    }
};

struct lower_snake {
    std::string operator()(bool, std::string_view value) const {
        return normalize_to_lower_snake(value);
    }
};

struct lower_camel {
    std::string operator()(bool is_serialize, std::string_view value) const {
        if(is_serialize) {
            return snake_to_camel(value, false);
        }
        return normalize_to_lower_snake(value);
    }
};

struct upper_camel {
    std::string operator()(bool is_serialize, std::string_view value) const {
        if(is_serialize) {
            return snake_to_camel(value, true);
        }
        return normalize_to_lower_snake(value);
    }
};

struct upper_snake {
    std::string operator()(bool is_serialize, std::string_view value) const {
        if(is_serialize) {
            return snake_to_upper(value);
        }
        return normalize_to_lower_snake(value);
    }
};

}  // namespace rename_policy

/// The rename policies as values, for use in annotation specs
/// (`rename_all = Casing::LowerCamel`).
enum class Casing : std::uint8_t {
    Identity,
    LowerSnake,
    LowerCamel,
    UpperCamel,
    UpperSnake,
};

namespace detail {

template <Casing C>
struct casing_policy;

// clang-format off
template <> struct casing_policy<Casing::Identity> { using type = rename_policy::identity; };
template <> struct casing_policy<Casing::LowerSnake> { using type = rename_policy::lower_snake; };
template <> struct casing_policy<Casing::LowerCamel> { using type = rename_policy::lower_camel; };
template <> struct casing_policy<Casing::UpperCamel> { using type = rename_policy::upper_camel; };
template <> struct casing_policy<Casing::UpperSnake> { using type = rename_policy::upper_snake; };

// clang-format on

}  // namespace detail

/// The rename_policy type behind a Casing value.
template <Casing C>
using rename_policy_t = typename detail::casing_policy<C>::type;

}  // namespace kota::naming
