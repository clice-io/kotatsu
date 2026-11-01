#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace kotatsu::naming {

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
            // Within a word, an uppercase letter starts a new one after a lowercase letter, a
            // UTF-8 byte or a digit ("fooBar", "名前Value", "v2Go"), and ends a run of capitals
            // before a lowercase letter ("HTTPServer"); a UTF-8 byte has no case, and ends no
            // such run ("HTTP名前").
            const bool in_word = !out.empty() && out.back() != '_';
            if(in_word && (is_lower_like(text[i - 1]) || is_digit(text[i - 1]) ||
                           (i + 1 < text.size() && is_lower(text[i + 1])))) {
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

/// The rename policies: each spells a declared name as a document writes it, in a constant
/// expression, so the names a type travels under are computed once, at compile time. A
/// document is read by matching the names so spelled, never by spelling back.
namespace rename_policy {

struct identity {
    constexpr std::string operator()(std::string_view value) const {
        return std::string(value);
    }
};

struct lower_snake {
    constexpr std::string operator()(std::string_view value) const {
        return normalize_to_lower_snake(value);
    }
};

struct lower_camel {
    constexpr std::string operator()(std::string_view value) const {
        return snake_to_camel(value, false);
    }
};

struct upper_camel {
    constexpr std::string operator()(std::string_view value) const {
        return snake_to_camel(value, true);
    }
};

struct upper_snake {
    constexpr std::string operator()(std::string_view value) const {
        return snake_to_upper(value);
    }
};

}  // namespace rename_policy

/// A rename policy: a type whose call operator spells a name, in a constant expression.
template <typename Policy>
concept renames = std::default_initializable<Policy> && requires(std::string_view name) {
    { Policy{}(name) } -> std::convertible_to<std::string>;
};

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

/// A rename policy as a value, which an annotation's spec holds: the function spelling a name
/// as the policy does.
using rename_fn = std::string (*)(std::string_view);

/// Spells name as Policy does: Policy's rename_fn.
template <renames Policy>
constexpr std::string spell_with(std::string_view name) {
    return std::string(Policy{}(name));
}

/// The rename_fn of the policy a Casing value names.
constexpr rename_fn casing_fn(Casing casing) {
    switch(casing) {
        case Casing::Identity: return &spell_with<rename_policy::identity>;
        case Casing::LowerSnake: return &spell_with<rename_policy::lower_snake>;
        case Casing::LowerCamel: return &spell_with<rename_policy::lower_camel>;
        case Casing::UpperCamel: return &spell_with<rename_policy::upper_camel>;
        case Casing::UpperSnake: return &spell_with<rename_policy::upper_snake>;
    }
    return &spell_with<rename_policy::identity>;
}

namespace detail {

template <rename_fn Fn>
struct fn_policy {
    constexpr std::string operator()(std::string_view name) const {
        return Fn(name);
    }
};

template <rename_fn Fn>
consteval auto policy_of() {
    if constexpr(Fn == &spell_with<rename_policy::identity>) {
        return std::type_identity<rename_policy::identity>{};
    } else if constexpr(Fn == &spell_with<rename_policy::lower_snake>) {
        return std::type_identity<rename_policy::lower_snake>{};
    } else if constexpr(Fn == &spell_with<rename_policy::lower_camel>) {
        return std::type_identity<rename_policy::lower_camel>{};
    } else if constexpr(Fn == &spell_with<rename_policy::upper_camel>) {
        return std::type_identity<rename_policy::upper_camel>{};
    } else if constexpr(Fn == &spell_with<rename_policy::upper_snake>) {
        return std::type_identity<rename_policy::upper_snake>{};
    } else {
        return std::type_identity<fn_policy<Fn>>{};
    }
}

}  // namespace detail

/// The rename policy whose rename_fn Fn is: the policy itself for those above, which share
/// their name tables with a format naming them, else one calling Fn.
template <rename_fn Fn>
using rename_policy_of_t = typename decltype(detail::policy_of<Fn>())::type;

namespace detail {

template <typename Policy, const auto& Names>
struct renamed_table {
    constexpr static std::size_t count = std::size(Names);

    constexpr static std::size_t total = [] {
        std::size_t size = 0;
        for(std::string_view name: Names) {
            size += std::string(Policy{}(name)).size();
        }
        return size;
    }();

    constexpr static std::array<char, total> chars = [] {
        std::array<char, total> out{};
        std::size_t at = 0;
        for(std::string_view name: Names) {
            for(char c: std::string(Policy{}(name))) {
                out[at++] = c;
            }
        }
        return out;
    }();

    constexpr static std::array<std::string_view, count> value = [] {
        std::array<std::string_view, count> out{};
        std::size_t at = 0;
        for(std::size_t i = 0; i < count; ++i) {
            const std::size_t size = std::string(Policy{}(Names[i])).size();
            out[i] = std::string_view(chars.data() + at, size);
            at += size;
        }
        return out;
    }();
};

template <const auto& Names>
struct renamed_table<rename_policy::identity, Names> {
    constexpr static std::array<std::string_view, std::size(Names)> value = [] {
        std::array<std::string_view, std::size(Names)> out{};
        for(std::size_t i = 0; i < out.size(); ++i) {
            out[i] = Names[i];
        }
        return out;
    }();
};

}  // namespace detail

/// Names, an array of std::string_view in static storage, each spelled by Policy: computed at
/// compile time, an array of views into static storage.
template <renames Policy, const auto& Names>
constexpr const inline auto& renamed = detail::renamed_table<Policy, Names>::value;

/// Whether `names` lists no name twice.
constexpr bool distinct(std::span<const std::string_view> names) {
    for(std::size_t i = 0; i < names.size(); ++i) {
        for(std::size_t j = i + 1; j < names.size(); ++j) {
            if(names[i] == names[j]) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace kotatsu::naming
