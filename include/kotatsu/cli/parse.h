#pragma once

#include <concepts>
#include <cstdint>
#include <deque>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "kotatsu/cli/detail/declaration.h"
#include "kotatsu/cli/error.h"
#include "kotatsu/cli/spec.h"
#include "kotatsu/support/numeric.h"
#include "kotatsu/support/string_list_ref.h"
#include "kotatsu/meta/enum.h"
#include "kotatsu/meta/spelling.h"
#include "kotatsu/meta/type_info.h"
#include "kotatsu/codec/decode.h"

namespace kotatsu::cli {

/// An argument parse() read, as its visit sees it once the options are decoded.
struct Given {
    /// The field it went into, by declared name from the options struct down: "session.handler".
    std::string path;
    /// Its values, as written.
    std::vector<std::string_view> values = {};
};

namespace detail {

/// What an argv gives a field, as the decode reads it.
struct Node {
    enum class Kind : std::uint8_t {
        /// A flag given: true.
        Flag,
        /// A counting flag: how often it was given, `count`.
        Count,
        /// One value, `text`.
        Text,
        /// The values of a list, or the elements of a tuple.
        List,
        /// The fields of a struct the argv gives something, `keys[i]` naming `items[i]`.
        Object,
        /// A subcommand or an exclusive field: alternative `count` of a variant, its fields
        /// `items[0]`.
        Variant,
    };

    Kind kind = Kind::Object;
    std::uint32_t count = 0;
    std::string_view text = {};
    /// The elements of the argv being read it comes from, [begin, end).
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
    std::vector<std::string_view> keys = {};
    std::vector<Node> items = {};
};

/// How the options are decoded: a field the argv does not give keeps the value it has.
struct DecodeConfig {
    constexpr static std::optional<meta::presence_kind> presence_all =
        meta::presence_kind::optional;
};

using decode_config = codec::default_config<DecodeConfig>;

struct Command;

/// Frees a command, a type complete only where it is built.
struct CommandDeleter {
    void operator()(Command* command) const;
};

using CommandPtr = std::unique_ptr<Command, CommandDeleter>;

/// Builds the command `type`, a struct, describes: its options, positional arguments, groups
/// and subcommands, and their option table.
CommandPtr build_command(const meta::type_info& type);

/// The command of options `T`, which a mistake in them keeps from compiling. Built on first
/// use.
template <typename T>
const Command& command_of() {
    static_assert(check_declaration(meta::type_info_of<T, decode_config>()));
    const static CommandPtr command = build_command(meta::type_info_of<T, decode_config>());
    return *command;
}

/// What an argv gives a command: the tree its decode reads, and where the nodes come from.
struct Reading {
    Node root;
    /// The argv as read: the user's elements, with what aliases stand for in place of them,
    /// which `expansions` holds.
    std::vector<std::string_view> active;
    /// The element of the user's argv each element of `active` comes from.
    std::vector<std::uint32_t> origin;
    std::deque<std::string> expansions;
    /// The arguments given, in order, for parse()'s visit.
    std::vector<Given> given;
    /// The elements of `active` that name a subcommand, in order.
    std::vector<std::uint32_t> subcommand_words;
};

/// Reads `argv` as `command` takes it into `reading`, or says why it does not parse.
std::optional<Error> read(const Command& command, string_list_ref argv, Reading& reading);

/// The error of a decode that failed with `error` once it read `last`.
Error decode_error(const Reading& reading,
                   string_list_ref argv,
                   const Node* last,
                   const codec::rich_error& error);

/// Reads `text` as the number `out` is, or says why it cannot.
std::optional<std::string> read_number(std::string_view text, std::int64_t& out);
std::optional<std::string> read_number(std::string_view text, std::uint64_t& out);
std::optional<std::string> read_number(std::string_view text, double& out);
std::optional<std::string> read_bool(std::string_view text, bool& out);

/// What the codec decode reads a field from: a Node.
struct Reader {
    const Node& node;
    /// The node read last, which a failed decode points at: a repr may fail once it has read.
    const Node** last;

    constexpr static bool data_driven = true;
    constexpr static bool human_readable = true;
    /// The parse checks the required arguments itself, and a help option skips the check.
    constexpr static bool presence_checked = true;

    bool fail(std::string message) const {
        *last = &node;
        return codec::scoped_context<codec::rich_error>::fail(
            codec::rich_error(std::move(message)));
    }

    bool fail_shape(std::string_view expected) const {
        return fail(std::format("expected {}", expected));
    }

    bool visit_bool(bool& out) {
        *last = &node;
        if(node.kind == Node::Kind::Flag) {
            out = true;
            return true;
        }
        if(node.kind != Node::Kind::Text) {
            return fail_shape("a boolean");
        }
        if(auto err = read_bool(node.text, out)) {
            return fail(std::move(*err));
        }
        return true;
    }

    template <typename T>
    bool visit_int(T& out) {
        *last = &node;
        using wide_t = std::conditional_t<std::is_signed_v<T>, std::int64_t, std::uint64_t>;
        wide_t wide = 0;
        if(node.kind == Node::Kind::Count) {
            wide = node.count;
        } else if(node.kind != Node::Kind::Text) {
            return fail_shape("an integer");
        } else if(auto err = read_number(node.text, wide)) {
            return fail(std::move(*err));
        }
        if(!kotatsu::narrow_int(wide, out)) {
            return fail(std::format("integer value out of range: {}", node.text));
        }
        return true;
    }

    template <typename T>
    bool visit_uint(T& out) {
        return visit_int(out);
    }

    template <typename T>
    bool visit_float(T& out) {
        *last = &node;
        double value = 0;
        if(node.kind != Node::Kind::Text) {
            return fail_shape("a number");
        }
        if(auto err = read_number(node.text, value)) {
            return fail(std::move(*err));
        }
        out = static_cast<T>(value);
        return true;
    }

    template <typename T>
    bool visit_str(T& out) {
        *last = &node;
        if(node.kind != Node::Kind::Text) {
            return fail_shape("a string");
        }
        out = T(node.text);
        return true;
    }

    template <typename T>
    bool visit_char(T& out) {
        *last = &node;
        if(node.kind != Node::Kind::Text || node.text.size() != 1) {
            return fail(std::format("invalid character value: {}", node.text));
        }
        out = static_cast<T>(node.text.front());
        return true;
    }

    template <typename T>
    bool visit_bytes(T&) {
        return fail_shape("text");
    }

    bool peek_null() const {
        return false;
    }

    bool visit_null() {
        return fail_shape("nothing");
    }

    meta::type_kind peek_kind() const {
        switch(node.kind) {
            case Node::Kind::Flag: return meta::type_kind::boolean;
            case Node::Kind::Count: return meta::type_kind::uint64;
            case Node::Kind::Text: return meta::type_kind::string;
            case Node::Kind::List: return meta::type_kind::array;
            case Node::Kind::Object:
            case Node::Kind::Variant: return meta::type_kind::structure;
        }
        std::unreachable();
    }

    template <typename F>
    bool try_read(F&& fn) {
        codec::rich_error discarded;
        codec::scoped_context<codec::rich_error> guard(discarded);
        Reader fork{node, last};
        return fn(fork);
    }

    template <typename Callback>
    bool visit_struct(Callback&& cb) {
        if(node.kind != Node::Kind::Object) {
            return fail_shape("options");
        }
        for(std::size_t i = 0; i < node.items.size(); ++i) {
            Reader field{node.items[i], last};
            KOTATSU_CODEC_TRY(cb(node.keys[i], field));
        }
        return true;
    }

    template <typename Callback>
    bool visit_seq(Callback&& cb) {
        if(node.kind != Node::Kind::List) {
            return fail_shape("a list");
        }
        for(const auto& item: node.items) {
            Reader element{item, last};
            KOTATSU_CODEC_TRY(cb(element));
        }
        return true;
    }

    template <typename Callback>
    bool visit_tuple(Callback&& cb) {
        return visit_seq(std::forward<Callback>(cb));
    }

    /// A subcommand: the alternative the argv names, read from its fields.
    template <typename Callback>
    bool visit_variant(Callback&& cb) {
        if(node.kind != Node::Kind::Variant) {
            return fail_shape("a subcommand");
        }
        Reader fields{node.items.front(), last};
        return cb(std::size_t{node.count}, fields);
    }

    template <typename Callback>
    bool visit_map(Callback&&) {
        return fail("a map is not an option");
    }
};

template <typename Enum>
std::string invalid_enum_value(std::string_view text) {
    std::string message = std::format("invalid enum value: {}", text);
    const auto& names = meta::enum_strings<Enum>();
    if(!names.empty()) {
        message += " (supported: ";
        for(std::size_t i = 0; i < names.size(); ++i) {
            message += i == 0 ? "" : ", ";
            message += names[i];
        }
        message += ")";
    }
    return message;
}

}  // namespace detail

/// parse_into(), then `visit(options, given)` for each argument it read, in order: what the
/// options alone do not keep, such as the order of the occurrences of two options.
template <typename T, typename Visit>
    requires std::invocable<Visit&, T&, const Given&>
std::expected<void, Error> parse_into(T& options, string_list_ref argv, Visit visit) {
    detail::Reading reading;
    if(auto err = detail::read(detail::command_of<T>(), argv, reading)) {
        return std::unexpected(std::move(*err));
    }
    const detail::Node* last = nullptr;
    codec::rich_error error;
    codec::scoped_context<codec::rich_error> guard(error);
    detail::Reader reader{reading.root, &last};
    if(!codec::decode_value<detail::decode_config>(reader, options)) {
        return std::unexpected(detail::decode_error(reading, argv, last, error));
    }
    for(const auto& given: reading.given) {
        visit(options, given);
    }
    return {};
}

/// Parses `argv` onto `options`, which keep what the argv does not give: options read from a
/// config file first, say, that the command line then overrides. A list, a variant or a
/// cli::Commands field the argv gives is replaced, not merged with what it held, and a required
/// option counts as given only when the argv gives it. On an error, `options` may hold part of
/// what was read.
template <typename T>
std::expected<void, Error> parse_into(T& options, string_list_ref argv) {
    return parse_into(options, argv, [](T&, const Given&) {});
}

/// parse(), then `visit(options, given)` for each argument it read, in order.
template <typename T, typename Visit>
    requires std::invocable<Visit&, T&, const Given&>
std::expected<T, Error> parse(string_list_ref argv, Visit visit) {
    T options{};
    if(auto read = parse_into(options, argv, std::move(visit)); !read) {
        return std::unexpected(std::move(read.error()));
    }
    return options;
}

/// Parses `argv`, the arguments after the program's name, into the options `T` declares: a
/// reflectable struct, each field an option, a positional argument (`cli::positional`), a
/// group of them (a struct), or subcommands (a std::variant of structs). A field the argv does
/// not give keeps the value it starts with.
template <typename T>
std::expected<T, Error> parse(string_list_ref argv) {
    return parse<T>(argv, [](T&, const Given&) {});
}

/// parse() of any other range of strings, a view such as `std::views::transform`'s.
template <typename T, std::ranges::input_range R>
    requires (!std::convertible_to<R, string_list_ref> &&
              std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>)
std::expected<T, Error> parse(R&& argv) {
    std::vector<std::string> copy;
    for(auto&& argument: argv) {
        copy.emplace_back(std::string_view(argument));
    }
    return parse<T>(string_list_ref(copy));
}

}  // namespace kotatsu::cli

namespace kotatsu::codec {

/// An enum reads from any spelling of its name meta knows: `info`, `Info`, `INFO`.
template <typename E, typename Config>
    requires (std::is_enum_v<E> && !meta::has_repr<E, meta::format_of_t<cli::detail::Reader>>)
struct deserialize_visit<cli::detail::Reader, E, Config> {
    static bool visit(cli::detail::Reader& reader, E& out) {
        if(reader.node.kind != cli::detail::Node::Kind::Text) {
            return reader.fail_shape("a name");
        }
        auto value = meta::map_string_to_enum<E>(reader.node.text);
        if(!value) {
            return reader.fail(cli::detail::invalid_enum_value<E>(reader.node.text));
        }
        out = *value;
        return true;
    }
};

}  // namespace kotatsu::codec
