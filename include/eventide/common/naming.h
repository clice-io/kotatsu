#pragma once

#include <string>
#include <string_view>

namespace eventide::naming {

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

constexpr std::string normalize_to_lower_snake(std::string_view text) {
    std::string out;
    for(std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if(is_alnum(c)) {
            if(is_upper(c)) {
                const bool prev_alnum = i > 0 && is_alnum(text[i - 1]);
                const bool prev_lower_or_digit =
                    i > 0 && (is_lower(text[i - 1]) || is_digit(text[i - 1]));
                const bool next_lower = i + 1 < text.size() && is_lower(text[i + 1]);
                if(!out.empty() && out.back() != '_' && prev_alnum &&
                   (prev_lower_or_digit || next_lower)) {
                    out += '_';
                }
                out += to_lower(c);
            } else {
                out += to_lower(c);
            }
        } else if(!out.empty() && out.back() != '_') {
            out += '_';
        }
    }
    auto start = out.find_first_not_of('_');
    if(start == std::string::npos)
        return {};
    auto end = out.find_last_not_of('_');
    return out.substr(start, end - start + 1);
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

using upper_case = upper_snake;

}  // namespace rename_policy

}  // namespace eventide::naming
