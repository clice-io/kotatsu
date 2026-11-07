#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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

namespace detail {

/// A rename policy that writes a name into a buffer with room for twice its size (Policy::write),
/// and so spells one as a string: one pass over a buffer sized once, which is what constant
/// evaluation, slow to grow a string, needs.
template <typename Policy>
struct writes_names {
    constexpr std::string operator()(std::string_view name) const {
        std::string out(name.size() * 2, '\0');
        out.resize(Policy::write(name, out.data()));
        return out;
    }
};

}  // namespace detail

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

/// The rename policies: each spells a declared name as a document writes it, in a constant
/// expression, so the names a type travels under are computed once, at compile time. A
/// document is read by matching the names so spelled, never by spelling back. Each but
/// identity also writes a name into a buffer with room for twice its size, which a table of
/// names renamed at compile time fills without growing a string (naming::renamed).
namespace rename_policy {

struct identity {
    constexpr std::string operator()(std::string_view value) const {
        return std::string(value);
    }
};

/// Words split at separators (anything not a letter, digit or UTF-8 byte) and at case changes
/// ("HTTPServer2Go" is "http_server2_go"), lowered and joined by one `_`, without leading or
/// trailing ones.
struct lower_snake : detail::writes_names<lower_snake> {
    constexpr static std::size_t write(std::string_view text, char* out) {
        std::size_t n = 0;
        for(std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if(is_upper(c)) {
                // Within a word, an uppercase letter starts a new one after a lowercase letter,
                // a UTF-8 byte or a digit ("fooBar", "名前Value", "v2Go"), and ends a run of
                // capitals before a lowercase letter ("HTTPServer"); a UTF-8 byte has no case,
                // and ends no such run ("HTTP名前").
                const bool in_word = n != 0 && out[n - 1] != '_';
                if(in_word &&
                   (is_lower(text[i - 1]) || is_non_ascii(text[i - 1]) || is_digit(text[i - 1]) ||
                    (i + 1 < text.size() && is_lower(text[i + 1])))) {
                    out[n++] = '_';
                }
                out[n++] = to_lower(c);
            } else if(is_lower(c) || is_digit(c) || is_non_ascii(c)) {
                // A UTF-8 byte continues a word like a lowercase letter.
                out[n++] = c;
            } else if(n != 0 && out[n - 1] != '_') {
                out[n++] = '_';
            }
        }
        // A separator only ever follows a word, so the one left to drop is a trailing one.
        if(n != 0 && out[n - 1] == '_') {
            --n;
        }
        return n;
    }
};

/// The words lower_snake splits, the first letter of each but the first upper, joined.
template <bool UpperFirst>
struct camel : detail::writes_names<camel<UpperFirst>> {
    constexpr static std::size_t write(std::string_view text, char* out) {
        const std::size_t snake = lower_snake::write(text, out);
        // The camel case name is no longer than the snake case one it is written over.
        std::size_t n = 0;
        bool capitalize_next = UpperFirst;
        bool seen_output = false;
        for(std::size_t i = 0; i < snake; ++i) {
            const char c = out[i];
            if(c == '_') {
                capitalize_next = true;
                continue;
            }
            if(capitalize_next && is_alpha(c)) {
                out[n++] = to_upper(c);
            } else if(!seen_output) {
                out[n++] = UpperFirst ? to_upper(c) : to_lower(c);
            } else {
                out[n++] = c;
            }
            capitalize_next = false;
            seen_output = true;
        }
        return n;
    }
};

using lower_camel = camel<false>;
using upper_camel = camel<true>;

/// The words lower_snake splits, upper.
struct upper_snake : detail::writes_names<upper_snake> {
    constexpr static std::size_t write(std::string_view text, char* out) {
        const std::size_t n = lower_snake::write(text, out);
        for(std::size_t i = 0; i < n; ++i) {
            out[i] = to_upper(out[i]);
        }
        return n;
    }
};

}  // namespace rename_policy

/// `text` in lower snake case: words split at separators (anything not a letter, digit or
/// UTF-8 byte) and at case changes ("HTTPServer2Go" is "http_server2_go"), lowered and joined
/// by one `_`, without leading or trailing ones.
constexpr std::string normalize_to_lower_snake(std::string_view text) {
    return rename_policy::lower_snake{}(text);
}

/// `text` in camel case, its words as normalize_to_lower_snake splits them, its first letter
/// upper or lower as upper_first says.
constexpr std::string snake_to_camel(std::string_view text, bool upper_first) {
    return upper_first ? rename_policy::upper_camel{}(text) : rename_policy::lower_camel{}(text);
}

/// `text` in upper snake case, its words as normalize_to_lower_snake splits them.
constexpr std::string snake_to_upper(std::string_view text) {
    return rename_policy::upper_snake{}(text);
}

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
    std::unreachable();
}

namespace detail {

template <rename_fn Fn>
struct fn_policy {
    constexpr std::string operator()(std::string_view name) const {
        return Fn(name);
    }
};

// Matched as template arguments rather than compared: GCC evaluates no
// comparison of a function's address where sanitizers check null pointers.

template <rename_fn Fn>
struct policy_of {
    using type = fn_policy<Fn>;
};

template <>
struct policy_of<&spell_with<rename_policy::identity>> {
    using type = rename_policy::identity;
};

template <>
struct policy_of<&spell_with<rename_policy::lower_snake>> {
    using type = rename_policy::lower_snake;
};

template <>
struct policy_of<&spell_with<rename_policy::lower_camel>> {
    using type = rename_policy::lower_camel;
};

template <>
struct policy_of<&spell_with<rename_policy::upper_camel>> {
    using type = rename_policy::upper_camel;
};

template <>
struct policy_of<&spell_with<rename_policy::upper_snake>> {
    using type = rename_policy::upper_snake;
};

}  // namespace detail

/// The rename policy whose rename_fn Fn is: the policy itself for those above, which share
/// their name tables with a format naming them, else one calling Fn.
template <rename_fn Fn>
using rename_policy_of_t = typename detail::policy_of<Fn>::type;

namespace detail {

/// Names renamed by Policy, kept in one buffer: each name is renamed twice, once to size the
/// buffer and once to fill it.
template <typename Policy, const auto& Names>
struct renamed_table {
    constexpr static std::size_t count = std::size(Names);

    /// Where each name ends in chars, after a 0 where the first begins.
    constexpr static std::array<std::size_t, count + 1> ends = [] {
        std::array<std::size_t, count + 1> out{};
        for(std::size_t i = 0; i < count; ++i) {
            out[i + 1] = out[i] + std::string(Policy{}(Names[i])).size();
        }
        return out;
    }();

    constexpr static std::array<char, ends[count]> chars = [] {
        std::array<char, ends[count]> out{};
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
        for(std::size_t i = 0; i < count; ++i) {
            out[i] = std::string_view(chars.data() + ends[i], ends[i + 1] - ends[i]);
        }
        return out;
    }();
};

/// Names renamed by a built-in Policy, which writes each into a buffer: each is renamed once,
/// into a draft with room for twice their size, which the exact buffer is copied from.
template <typename Policy, const auto& Names>
    requires std::derived_from<Policy, detail::writes_names<Policy>>
struct renamed_table<Policy, Names> {
    constexpr static std::size_t count = std::size(Names);

    constexpr static std::size_t room = [] {
        std::size_t size = 0;
        for(std::string_view name: Names) {
            size += name.size() * 2;
        }
        return size;
    }();

    struct draft_t {
        std::array<char, room> chars;
        /// Where each name ends in chars, after a 0 where the first begins.
        std::array<std::size_t, count + 1> ends;
    };

    constexpr static draft_t draft = [] {
        draft_t out{};
        for(std::size_t i = 0; i < count; ++i) {
            out.ends[i + 1] = out.ends[i] + Policy::write(Names[i], out.chars.data() + out.ends[i]);
        }
        return out;
    }();

    constexpr static std::array<char, draft.ends[count]> chars = [] {
        std::array<char, draft.ends[count]> out{};
        for(std::size_t i = 0; i < out.size(); ++i) {
            out[i] = draft.chars[i];
        }
        return out;
    }();

    constexpr static std::array<std::string_view, count> value = [] {
        std::array<std::string_view, count> out{};
        for(std::size_t i = 0; i < count; ++i) {
            out[i] =
                std::string_view(chars.data() + draft.ends[i], draft.ends[i + 1] - draft.ends[i]);
        }
        return out;
    }();
};

/// The identity policy spells the names as they are.
template <const auto& Names>
struct renamed_table<rename_policy::identity, Names> {
    constexpr const static auto& value = Names;
};

}  // namespace detail

/// Names, a std::array of names in static storage, each spelled by Policy: computed at compile
/// time, an array of views into static storage.
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
