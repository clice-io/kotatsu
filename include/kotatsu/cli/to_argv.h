#pragma once

#include <cassert>
#include <cstdint>
#include <deque>
#include <format>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kotatsu/cli/parse.h"
#include "kotatsu/codec/encode.h"

namespace kotatsu::cli {

namespace detail {

/// What the codec encode writes options into: a Node, its texts kept in `texts`. A bool is the
/// text "true" or "false"; an empty optional is a Flag node, which to_argv() leaves out.
struct Writer {
    Node& node;
    std::deque<std::string>& texts;
    /// Whether a field that fails to write is left out rather than failing the options, as a
    /// usage collecting the defaults does: it shows none for a value no argument names.
    bool lenient = false;

    using format = cli::format;

    bool text(std::string value) {
        node = Node{.kind = Node::Kind::Text, .text = texts.emplace_back(std::move(value))};
        return true;
    }

    bool visit_bool(bool value) {
        node = Node{.kind = Node::Kind::Text, .text = value ? "true" : "false"};
        return true;
    }

    template <typename T>
    bool visit_int(T value) {
        return text(std::to_string(static_cast<std::int64_t>(value)));
    }

    template <typename T>
    bool visit_uint(T value) {
        return text(std::to_string(static_cast<std::uint64_t>(value)));
    }

    template <typename T>
    bool visit_float(T value) {
        // The shortest text that reads back as the same value.
        return text(std::format("{}", static_cast<double>(value)));
    }

    template <typename T>
    bool visit_str(const T& value) {
        return text(std::string(std::string_view(value)));
    }

    template <typename T>
    bool visit_char(T value) {
        return text(std::string(1, static_cast<char>(value)));
    }

    bool visit_null() {
        node = Node{.kind = Node::Kind::Flag};
        return true;
    }

    /// A subcommand or an exclusive field's alternative: alternative `index`, its fields below.
    /// A variant tagged by name writes an object of one field instead, the name of the
    /// alternative its key.
    template <typename Body>
    bool visit_variant(std::size_t index, Body&& body) {
        node = Node{.kind = Node::Kind::Variant, .count = static_cast<std::uint32_t>(index)};
        Writer fields{node.items.emplace_back(), texts, lenient};
        return body(fields);
    }

    template <typename T, typename Body>
    bool visit_struct(const T&, Body&& body);

    template <typename Container, typename Body>
    bool visit_seq(const Container&, Body&& body);

    template <typename T, typename Body>
    bool visit_tuple(const T&, Body&& body);
};

struct FieldWriter {
    Node& node;
    std::deque<std::string>& texts;
    bool lenient = false;

    template <typename Field, typename F>
    bool visit_field(Field field, F&& write) {
        node.keys.push_back(field.name);
        Writer value{node.items.emplace_back(), texts, lenient};
        if(write(value)) {
            return true;
        }
        if(!lenient) {
            return false;
        }
        node.keys.pop_back();
        node.items.pop_back();
        return true;
    }
};

struct ElementWriter {
    Node& node;
    std::deque<std::string>& texts;
    bool lenient = false;

    template <typename F>
    bool visit_element(F&& write) {
        Writer element{node.items.emplace_back(), texts, lenient};
        return write(element);
    }
};

template <typename T, typename Body>
bool Writer::visit_struct(const T&, Body&& body) {
    node = Node{.kind = Node::Kind::Object};
    FieldWriter fields{node, texts, lenient};
    return body(fields);
}

template <typename Container, typename Body>
bool Writer::visit_seq(const Container&, Body&& body) {
    node = Node{.kind = Node::Kind::List};
    ElementWriter elements{node, texts, lenient};
    return body(elements);
}

template <typename T, typename Body>
bool Writer::visit_tuple(const T& value, Body&& body) {
    return visit_seq(value, std::forward<Body>(body));
}

/// The argv `root`, options as a Writer wrote them, stands for, as `command` reads it.
std::vector<std::string> write_args(const Command& command, const Node& root);

}  // namespace detail

namespace detail {

/// `options` as a Writer writes them, their texts kept in `texts`; leniently, without the
/// fields that fail to write.
template <typename T>
Node write_options(const T& options, std::deque<std::string>& texts, bool lenient = false) {
    Node root;
    Writer writer{root, texts, lenient};
    codec::rich_error error;
    codec::scoped_context<codec::rich_error> guard(error);
    [[maybe_unused]] const bool written = codec::encode_value<cli::format>(writer, options);
    assert(written);
    return root;
}

}  // namespace detail

/// The argv parse() reads back as `options`: each option and positional argument with a
/// value, in declaration order, then the subcommand and its own.
template <typename T>
std::vector<std::string> to_argv(const T& options) {
    std::deque<std::string> texts;
    return detail::write_args(detail::command_of<T>(), detail::write_options(options, texts));
}

}  // namespace kotatsu::cli

namespace kotatsu::codec {

/// An enum writes as its name (cli::detail::enum_text); a value without one fails.
template <typename E>
    requires (std::is_enum_v<E> && !codec::has_repr<E, cli::format>)
struct serialize_visit<cli::detail::Writer, E> {
    static bool visit(cli::detail::Writer& writer, const E& value) {
        auto name = meta::enum_name(value);
        if(name.empty()) {
            return detail::fail_unnamed_enum(value);
        }
        return writer.text(cli::detail::enum_text(name));
    }
};

}  // namespace kotatsu::codec
