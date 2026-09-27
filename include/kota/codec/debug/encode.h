#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include "kota/support/expected_try.h"
#include "kota/meta/enum.h"
#include "kota/meta/name.h"
#include "kota/codec/visit/common.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/encode.h"

// The debug backend renders Rust-Debug-style text for humans and logs —
// struct `TypeName { field: value }`, enum `TypeName::MemberName`, bytes as
// hex — and is encode-only: nothing parses it back. It is deliberately
// format-agnostic (no `format` tag), so format-scoped meta::repr
// specializations never apply; what you see is the format-neutral shape.

namespace kota::codec::debug {

using error = rich_error;

struct Formatter {
    std::string& out;
    bool pretty = false;
    int indent = 0;

    void newline_indent() {
        if(!pretty)
            return;
        out += '\n';
        for(int i = 0; i < indent; ++i)
            out += "    ";
    }

    /// Appends c as an escape inside a literal delimited by `quote`, the way
    /// Rust's Debug spells it; false when c needs none.
    bool write_escape(char c, char quote) {
        switch(c) {
            case '\\': out += "\\\\"; return true;
            case '\n': out += "\\n"; return true;
            case '\r': out += "\\r"; return true;
            case '\t': out += "\\t"; return true;
            case '\0': out += "\\0"; return true;
            default: break;
        }
        if(c == quote) {
            out += '\\';
            out += c;
            return true;
        }
        if(auto codepoint = static_cast<unsigned char>(c); codepoint < 0x20) {
            out += std::format("\\x{:02x}", codepoint);
            return true;
        }
        return false;
    }

    void write_escape_string(std::string_view sv) {
        out += '"';
        for(char c: sv) {
            if(!write_escape(c, '"')) {
                out += c;
            }
        }
        out += '"';
    }

    /// Any other char prints as the text codecs write it.
    void write_escape_char(char c) {
        out += '\'';
        if(!write_escape(c, '\'')) {
            out += char_to_utf8(c);
        }
        out += '\'';
    }

    void write_separator(bool& first) {
        if(!first) {
            out += pretty ? "," : ", ";
        }
        newline_indent();
        first = false;
    }

    template <typename W, typename Body>
    bool write_block(char open, char close, Body&& body) {
        out += open;
        if(pretty) {
            indent++;
            W w{*this};
            if(!body(w)) {
                indent--;
                return false;
            }
            if(!w.first) {
                out += ',';
                indent--;
                newline_indent();
            } else {
                indent--;
            }
        } else {
            W w{*this};
            if(!body(w))
                return false;
        }
        out += close;
        return true;
    }
};

struct ValueWriter {
    Formatter& fmt;
    using error_type = rich_error;
    constexpr static bool human_readable = true;

    bool visit_bool(bool v) {
        fmt.out += v ? "true" : "false";
        return true;
    }

    template <typename T>
    bool visit_int(T v) {
        fmt.out += std::to_string(static_cast<std::int64_t>(v));
        return true;
    }

    template <typename T>
    bool visit_uint(T v) {
        fmt.out += std::to_string(static_cast<std::uint64_t>(v));
        return true;
    }

    template <typename T>
    bool visit_float(T v) {
        fmt.out += std::format("{}", static_cast<double>(v));
        return true;
    }

    template <typename T>
    bool visit_str(const T& v) {
        if constexpr(std::is_pointer_v<T>) {
            if(v == nullptr) {
                fmt.out += "null";
                return true;
            }
        }
        fmt.write_escape_string(std::string_view(v));
        return true;
    }

    template <typename T>
    bool visit_char(T v) {
        fmt.write_escape_char(v);
        return true;
    }

    template <typename T>
    bool visit_bytes(const T& v) {
        std::span<const std::byte> bytes(v);
        fmt.out += '[';
        for(std::size_t i = 0; i < bytes.size(); ++i) {
            if(i > 0)
                fmt.out += ", ";
            auto byte = static_cast<std::uint8_t>(bytes[i]);
            fmt.out += "0x";
            fmt.out += "0123456789abcdef"[byte >> 4];
            fmt.out += "0123456789abcdef"[byte & 0xf];
        }
        fmt.out += ']';
        return true;
    }

    bool visit_null() {
        fmt.out += "null";
        return true;
    }

    template <typename T>
    bool visit_enum(T v) {
        fmt.out += meta::type_name<T>();
        fmt.out += "::";
        auto name = meta::enum_name(v);
        if(name.empty()) {
            // Unary plus keeps a char-sized underlying value a number.
            fmt.out += std::format("{}", +std::to_underlying(v));
        } else {
            fmt.out += name;
        }
        return true;
    }

    template <typename T>
    bool visit_pointer(const T& ptr) {
        if(ptr) {
            if constexpr(std::is_pointer_v<T>) {
                fmt.out += std::format("0x{:x}", reinterpret_cast<std::uintptr_t>(ptr));
            } else {
                fmt.out += std::format("0x{:x}", reinterpret_cast<std::uintptr_t>(ptr.get()));
            }
        } else {
            fmt.out += "null";
        }
        return true;
    }

    /// A value of a type the schema knows nothing about: std::error_code as its
    /// category and message, a formattable type through std::format, anything
    /// else by name.
    template <typename T>
    bool visit_opaque(const T& v) {
        if constexpr(std::is_same_v<T, std::error_code>) {
            fmt.out += std::format("{}: {}", v.category().name(), v.message());
        } else if constexpr(std::formattable<T, char>) {
            fmt.out += std::format("{}", v);
        } else {
            fmt.out += '<';
            fmt.out += meta::type_name<T>();
            fmt.out += '>';
        }
        return true;
    }

    template <typename T, typename Body>
    bool visit_struct(const T&, Body&& body);

    template <typename Container, typename Body>
    bool visit_seq(const Container&, Body&& body);

    template <typename Container, typename Body>
    bool visit_map(const Container&, Body&& body);

    template <typename T, typename Body>
    bool visit_tuple(const T&, Body&& body);
};

struct StructWriter {
    Formatter& fmt;
    using error_type = rich_error;
    bool first = true;

    template <typename F>
    bool visit_field(std::size_t index, std::string_view name, F&& writer);
};

struct SeqWriter {
    Formatter& fmt;
    bool first = true;

    template <typename F>
    bool visit_element(F&& writer);
};

struct TupleWriter {
    Formatter& fmt;
    bool first = true;
    std::size_t count = 0;

    template <typename F>
    bool visit_element(F&& writer);
};

struct MapWriter {
    Formatter& fmt;
    bool first = true;

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_fn, VF&& value_fn);
};

template <typename T, typename Body>
bool ValueWriter::visit_struct(const T&, Body&& body) {
    fmt.out += meta::type_name<T>();
    fmt.out += ' ';
    if(fmt.pretty) {
        return fmt.write_block<StructWriter>('{', '}', std::forward<Body>(body));
    }
    fmt.out += "{ ";
    StructWriter sw{fmt};
    KOTA_CODEC_TRY(body(sw));
    if(!sw.first)
        fmt.out += ' ';
    fmt.out += '}';
    return true;
}

template <typename Container, typename Body>
bool ValueWriter::visit_seq(const Container&, Body&& body) {
    if constexpr(meta::kind_of<Container>() == meta::type_kind::set) {
        return fmt.write_block<SeqWriter>('{', '}', std::forward<Body>(body));
    } else {
        return fmt.write_block<SeqWriter>('[', ']', std::forward<Body>(body));
    }
}

template <typename Container, typename Body>
bool ValueWriter::visit_map(const Container&, Body&& body) {
    return fmt.write_block<MapWriter>('{', '}', std::forward<Body>(body));
}

template <typename T, typename Body>
bool ValueWriter::visit_tuple(const T&, Body&& body) {
    if(fmt.pretty) {
        return fmt.write_block<TupleWriter>('(', ')', std::forward<Body>(body));
    }
    fmt.out += '(';
    TupleWriter tw{fmt};
    KOTA_CODEC_TRY(body(tw));
    if(tw.count == 1)
        fmt.out += ',';
    fmt.out += ')';
    return true;
}

template <typename F>
bool StructWriter::visit_field(std::size_t, std::string_view name, F&& writer) {
    fmt.write_separator(first);
    fmt.out += name;
    fmt.out += ": ";
    ValueWriter vw{fmt};
    return writer(vw);
}

template <typename F>
bool TupleWriter::visit_element(F&& writer) {
    fmt.write_separator(first);
    ++count;
    ValueWriter vw{fmt};
    return writer(vw);
}

template <typename F>
bool SeqWriter::visit_element(F&& writer) {
    fmt.write_separator(first);
    ValueWriter vw{fmt};
    return writer(vw);
}

template <typename KF, typename VF>
bool MapWriter::visit_entry(KF&& key_fn, VF&& value_fn) {
    fmt.write_separator(first);
    ValueWriter kw{fmt};
    KOTA_CODEC_TRY(key_fn(kw));
    fmt.out += ": ";
    ValueWriter vw{fmt};
    return value_fn(vw);
}

/// Renders `value` as Rust-Debug-style text; `pretty` switches from
/// single-line output to 4-space-indented multiline.
template <typename Config = void, typename T>
auto to_string(const T& value, bool pretty = false) -> std::expected<std::string, error> {
    std::string result;
    Formatter fmt{result, pretty};
    ValueWriter vis{fmt};
    KOTA_EXPECTED_TRY(detail::run_encode<Config>(vis, value));
    return result;
}

}  // namespace kota::codec::debug
