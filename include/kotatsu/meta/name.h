#pragma once

#include <cstddef>
#include <source_location>
#include <string_view>

#include "kotatsu/support/config.h"

/// Names as the compiler prints them in a function's signature: of a type, of a member, of the
/// values of a pack. A signature is read in constant evaluation, which pays for every step: the
/// helpers below search it through the compiler's own string builtins, a step each, and walk
/// character by character only a name they take apart.
namespace kotatsu::meta::detail {

constexpr bool identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}

/// A signature, its length counted by the compiler.
constexpr std::string_view signature_text(const char* signature) {
    return {signature, __builtin_strlen(signature)};
}

/// Where c first appears in text from `from` on; text.size() when it does not.
constexpr std::size_t find_char(std::string_view text, char c, std::size_t from) {
    if(from >= text.size()) {
        return text.size();
    }
#if defined(__clang__) || KOTATSU_COMPILER_MSVC
    const char* found = __builtin_char_memchr(text.data() + from, c, text.size() - from);
    return found == nullptr ? text.size() : static_cast<std::size_t>(found - text.data());
#else
    while(from < text.size() && text[from] != c) {
        ++from;
    }
    return from;
#endif
}

/// Whether text holds `part` at `at`.
constexpr bool holds_at(std::string_view text, std::size_t at, std::string_view part) {
    return at + part.size() <= text.size() &&
           __builtin_memcmp(text.data() + at, part.data(), part.size()) == 0;
}

/// How many times c appears in text.
constexpr std::size_t count_char(std::string_view text, char c) {
    std::size_t count = 0;
    for(std::size_t at = find_char(text, c, 0); at < text.size(); at = find_char(text, c, at + 1)) {
        ++count;
    }
    return count;
}

/// Where `part` first appears in text from `from` on; text.size() when it does not.
constexpr std::size_t find_text(std::string_view text, std::string_view part, std::size_t from) {
    for(from = find_char(text, part[0], from); from < text.size();
        from = find_char(text, part[0], from + 1)) {
        if(holds_at(text, from, part)) {
            return from;
        }
    }
    return text.size();
}

/// Where the literal that opens with the quote at `open` closes: at the next quote no backslash
/// escapes, or, of a name MSVC quotes, `anonymous namespace', at the `'` that matches the
/// backtick; at text's end when none does.
constexpr std::size_t literal_end(std::string_view text, std::size_t open) {
    const bool backtick = text[open] == '`';
    const char quote = backtick ? '\'' : text[open];
    std::size_t nested = 0;
    for(std::size_t at = open + 1; at < text.size(); ++at) {
        if(text[at] == '\\') {
            ++at;
        } else if(backtick && text[at] == '`') {
            ++nested;
        } else if(text[at] == quote) {
            if(nested == 0) {
                return at;
            }
            --nested;
        }
    }
    return text.size();
}

/// Where the identifier that starts at `at` ends.
constexpr std::size_t identifier_end(std::string_view text, std::size_t at) {
    while(at < text.size() && identifier_char(text[at])) {
        ++at;
    }
    return at;
}

constexpr bool operator_char(char c) {
    return std::string_view("<>=!+-*/%^&|~,").find(c) != std::string_view::npos;
}

/// How long the operator is that `operator`'s name goes on with at `at`: `()`, `[]`, or the
/// longest of its symbols that is one, so that `operator<` in `S<&T::operator<>` takes the one
/// `<`, and, unless `shifts`, the shorter one where those are `>>`; 0 for a conversion's, whose
/// name goes on with a type.
constexpr std::size_t operator_length(std::string_view text, std::size_t at, bool shifts = true) {
    constexpr std::string_view bracketed[] =
        {"<=>", "<<=", ">>=", "->*", "<<", ">>", "<=", ">=", "->", "()", "[]", "<", ">"};
    for(const std::string_view symbols: bracketed) {
        if(holds_at(text, at, symbols) && (shifts || !symbols.starts_with(">>"))) {
            return symbols.size();
        }
    }
    std::size_t end = at;
    while(end < text.size() && operator_char(text[end]) && text[end] != '<' && text[end] != '>') {
        ++end;
    }
    return end - at;
}

/// Where a name's last scope ends in text, and how many brackets are left open after it.
struct scopes {
    std::size_t end = 0;
    std::size_t open = 0;
};

constexpr scopes scan_scopes(std::string_view text, bool shifts) {
    scopes found;
    for(std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if(identifier_char(c)) {
            const std::size_t end = identifier_end(text, i);
            if(text.substr(i, end - i) == "operator") {
                // MSVC writes a space after `operator`.
                const std::size_t symbols = text.find_first_not_of(' ', end);
                i = symbols == std::string_view::npos
                        ? text.size()
                        : symbols + operator_length(text, symbols, shifts);
            } else {
                i = end;
            }
            --i;
        } else if(c == '"' || c == '\'' || c == '`') {
            i = literal_end(text, i);
        } else if(c == '-' && i + 1 < text.size() && text[i + 1] == '>') {
            ++i;
        } else if(c == '<' || c == '(' || c == '[' || c == '{') {
            ++found.open;
        } else if(c == '>' || c == ')' || c == ']' || c == '}') {
            found.open = found.open > 0 ? found.open - 1 : 0;
        } else if(c == ':' && found.open == 0 && i + 1 < text.size() && text[i + 1] == ':') {
            found.end = ++i + 1;
        }
    }
    return found;
}

/// Where the last name in text begins: past its last `::` outside brackets and literals, which
/// a name's template arguments, a function's parameters and `(anonymous namespace)` are in; 0
/// when there is none. An operator's symbols, `operator<`, open and close no bracket; `>>`
/// after `operator` is `operator>` and a bracket where `operator>>` would leave one open, as in
/// `S<&T::operator>>`, which clang and GCC print without a space.
constexpr std::size_t name_start(std::string_view text) {
    const scopes shift = scan_scopes(text, true);
    return shift.open == 0 ? shift.end : scan_scopes(text, false).end;
}

/// The identifier text begins with.
constexpr std::string_view leading_identifier(std::string_view text) {
    return text.substr(0, identifier_end(text, 0));
}

/// The last name in text: an operator's whole, `operator<=` or `operator int`, else the
/// identifier past the last `::` outside brackets.
constexpr std::string_view last_name(std::string_view text) {
    const std::string_view name = text.substr(name_start(text));
    const std::string_view identifier = leading_identifier(name);
    if(identifier != "operator") {
        return identifier;
    }
    const std::size_t symbols = name.find_first_not_of(' ', identifier.size());
    const std::size_t length =
        symbols == std::string_view::npos ? 0 : operator_length(name, symbols);
    return length == 0 ? name : name.substr(0, symbols + length);
}

/// The member a member pointer names, as the compiler prints it, `&S::member`; of the
/// declaration MSVC prints of a member function, `void __cdecl S::member(void) const`, up to
/// its parameters.
constexpr std::string_view member_text(std::string_view printed) {
#if KOTATSU_COMPILER_MSVC
    const std::size_t close = printed.rfind(')');
    // Its qualifiers alone follow the parameters, no name.
    if(close != std::string_view::npos &&
       printed.find_first_of(":<>", close) == std::string_view::npos) {
        int depth = 0;
        for(std::size_t i = close + 1; i > 0; --i) {
            if(printed[i - 1] == ')') {
                ++depth;
            } else if(printed[i - 1] == '(' && --depth == 0) {
                return printed.substr(0, i - 1);
            }
        }
    }
#endif
    return printed;
}

/// A member pointer as MSVC prints one of a class with more than one base, `{&S::f,0}`, without
/// the braces and the offsets.
constexpr std::string_view without_offsets(std::string_view printed) {
    if(!printed.starts_with('{') || !printed.ends_with('}')) {
        return printed;
    }
    printed = printed.substr(1, printed.size() - 2);
    for(std::size_t comma = printed.rfind(','); comma != std::string_view::npos;
        comma = printed.rfind(',')) {
        const std::string_view offset = printed.substr(comma + 1);
        if(offset.empty() || !(offset[0] >= '0' && offset[0] <= '9')) {
            break;
        }
        printed = printed.substr(0, comma);
    }
    return printed;
}

/// The identifier text ends in, past the characters after it that end no identifier.
constexpr std::string_view trailing_identifier(std::string_view text) {
    std::size_t end = text.size();
    while(end > 0 && !identifier_char(text[end - 1])) {
        --end;
    }
    std::size_t begin = end;
    while(begin > 0 && identifier_char(text[begin - 1])) {
        --begin;
    }
    return text.substr(begin, end - begin);
}

/// The signature of a function of T, in which the compiler spells T. Its return type names no
/// typedef, which GCC would spell out after T.
template <typename T>
consteval const char* type_signature() {
    return std::source_location::current().function_name();
}

/// Prints a pack of values into one signature, so that reflecting the members of an enum or a
/// struct takes one instantiation for many of them rather than one each. Its name and return
/// type hold no digit, as measure_zero finds the values by one.
template <auto... Vs>
consteval const char* pack_signature() {
    return std::source_location::current().function_name();
}

/// Prints one value: clang takes as an argument of a pack no value of an enum without a fixed
/// underlying type outside the enum's range, which it takes as a value of its own.
template <auto V>
consteval const char* value_signature() {
    return std::source_location::current().function_name();
}

/// Where a signature spells what it is of: after a prefix and before a suffix that are the
/// same for every type, or every pack, measured once.
struct spelling {
    std::size_t prefix;
    std::size_t suffix;

    constexpr std::string_view of(const char* signature) const {
        const std::string_view text = signature_text(signature);
        return text.substr(prefix, text.size() - prefix - suffix);
    }
};

consteval spelling measure_type() {
    const std::string_view text = signature_text(type_signature<int>());
    const std::size_t at = text.rfind("int");
    return {at, text.size() - at - 3};
}

/// On a signature of 0 alone, which MSVC prints `0x0`.
consteval spelling measure_zero(const char* signature) {
    const std::string_view text = signature_text(signature);
    return {text.find('0'), text.size() - text.rfind('0') - 1};
}

constexpr inline spelling type_spelling = measure_type();
constexpr inline spelling pack_spelling = measure_zero(pack_signature<0>());
constexpr inline spelling value_spelling = measure_zero(value_signature<0>());

/// T as the compiler spells it in a signature. MSVC writes a class type's key ahead of it
/// (`struct Foo`), and a space after a template's arguments, before the `>` that closes those
/// of the signature's own: both are left out.
template <typename T>
consteval std::string_view spelled_type() {
    std::string_view name = type_spelling.of(type_signature<T>());
#if KOTATSU_COMPILER_MSVC
    if(name.ends_with(' ')) {
        name.remove_suffix(1);
    }
    // `unsigned int` is a name of its own.
    for(std::string_view key: {"class ", "struct ", "enum ", "union "}) {
        if(name.starts_with(key)) {
            return name.substr(key.size());
        }
    }
#endif
    return name;
}

/// The values printed in pack_signature<Vs...>(), as the compiler lists them.
template <auto... Vs>
constexpr std::string_view pack_list() {
    return pack_spelling.of(pack_signature<Vs...>());
}

}  // namespace kotatsu::meta::detail

namespace kotatsu::meta {

/// T as the compiler spells it, qualified.
template <typename T>
constexpr inline std::string_view qualified_type_name = detail::spelled_type<T>();

/// T as the compiler spells it, its last name only: `Foo<ns::Bar>` for `ns::Foo<ns::Bar>`.
template <typename T>
constexpr inline std::string_view type_name =
    qualified_type_name<T>.substr(detail::name_start(qualified_type_name<T>));

/// The name of the member `Member` points to, `&S::field` or `&S::function`, which MSVC prints
/// as its declaration, `void __cdecl S::function(void)`, or, of a class with more than one base,
/// in braces with the offsets that adjust it, `{&S::function,0}`.
template <auto Member>
constexpr inline std::string_view member_name =
    detail::last_name(detail::member_text(detail::without_offsets(detail::pack_list<Member>())));

}  // namespace kotatsu::meta
