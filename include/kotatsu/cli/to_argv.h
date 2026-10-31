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
#include "kotatsu/meta/spelling.h"
#include "kotatsu/codec/visit/encode.h"

namespace kotatsu::cli {

namespace detail {

/// What the codec encode writes options into: a Node, its texts kept in `texts`. A bool false
/// or an empty optional is a Flag node of count 0, which to_argv() leaves out.
struct Writer {
    Node& node;
    std::deque<std::string>& texts;

    constexpr static bool human_readable = true;

    bool text(std::string value) {
        node = Node{.kind = Node::Kind::Text, .text = texts.emplace_back(std::move(value))};
        return true;
    }

    bool visit_bool(bool value) {
        node = Node{.kind = Node::Kind::Flag, .count = value ? 1u : 0u};
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

    template <typename F>
    bool visit_field(std::size_t, std::string_view name, F&& write) {
        node.keys.push_back(name);
        Writer field{node.items.emplace_back(), texts};
        return write(field);
    }
};

struct ElementWriter {
    Node& node;
    std::deque<std::string>& texts;

    template <typename F>
    bool visit_element(F&& write) {
        Writer element{node.items.emplace_back(), texts};
        return write(element);
    }
};

template <typename T, typename Body>
bool Writer::visit_struct(const T&, Body&& body) {
    node = Node{.kind = Node::Kind::Object};
    FieldWriter fields{node, texts};
    return body(fields);
}

template <typename Container, typename Body>
bool Writer::visit_seq(const Container&, Body&& body) {
    node = Node{.kind = Node::Kind::List};
    ElementWriter elements{node, texts};
    return body(elements);
}

template <typename T, typename Body>
bool Writer::visit_tuple(const T&, Body&& body) {
    node = Node{.kind = Node::Kind::List};
    ElementWriter elements{node, texts};
    return body(elements);
}

/// The argv `root`, options as a Writer wrote them, stands for, as `command` reads it.
std::vector<std::string> write_args(const Command& command, const Node& root);

}  // namespace detail

/// The argv parse() reads back as `options`: each option and positional argument with a
/// value, in declaration order, then the subcommand and its own.
template <typename T>
std::vector<std::string> to_argv(const T& options) {
    detail::Node root;
    std::deque<std::string> texts;
    detail::Writer writer{root, texts};
    codec::rich_error error;
    codec::scoped_context<codec::rich_error> guard(error);
    [[maybe_unused]] const bool written =
        codec::encode_value<detail::decode_config>(writer, options);
    assert(written);
    return detail::write_args(detail::command_of(meta::type_info_of<T, detail::decode_config>()),
                              root);
}

}  // namespace kotatsu::cli

namespace kotatsu::codec {

/// An enum writes as its name.
template <typename E, typename Config>
    requires std::is_enum_v<E>
struct serialize_visit<cli::detail::Writer, E, Config> {
    static bool visit(cli::detail::Writer& writer, const E& value) {
        return writer.text(meta::map_enum_to_string(value));
    }
};

/// A variant of subcommands writes the one it holds.
template <typename... Ts, typename Config>
struct serialize_visit<cli::detail::Writer, std::variant<Ts...>, Config> {
    static bool visit(cli::detail::Writer& writer, const std::variant<Ts...>& value) {
        writer.node = cli::detail::Node{.kind = cli::detail::Node::Kind::Variant,
                                        .count = static_cast<std::uint32_t>(value.index())};
        cli::detail::Writer fields{writer.node.items.emplace_back(), writer.texts};
        return std::visit(
            [&](const auto& alternative) { return encode_value<Config>(fields, alternative); },
            value);
    }
};

}  // namespace kotatsu::codec
