#pragma once

#include <cassert>
#include <concepts>
#include <cstdint>
#include <deque>
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
#include "kotatsu/support/naming.h"
#include "kotatsu/support/numeric.h"
#include "kotatsu/support/string_list_ref.h"
#include "kotatsu/meta/enum.h"
#include "kotatsu/codec/decode.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::cli {

/// How options are read from and written to an argv: a field the argv does not give keeps the
/// value it has.
struct format : codec::extend<codec::format> {
    constexpr static std::optional<codec::presence_kind> presence_all =
        codec::presence_kind::optional;
};

/// The usage an argv asks for with -h or --help, which every command takes: of the command it
/// names by the subcommands it gives before the option, outermost first; of the program's own
/// when it gives none.
struct Help {
    std::vector<std::string> subcommands = {};
};

/// What parse() makes of an argv: the options it gives, the Help it asks for, or the Error it
/// does not parse with. parse_into(), which reads onto options the caller holds, makes a
/// `Parsed<void>`, without options.
///
///     auto options = cli::parse<Options>(argv);
///     if(auto help = options.help()) {
///         cli::print_usage<Options>("tool", help->subcommands);
///         return 0;
///     }
///     if(auto error = options.error()) {
///         cli::report(*error);
///         return 2;
///     }
///     run(*options);
template <typename T>
class Parsed {
    using value_type = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

public:
    Parsed()
        requires std::is_void_v<T>
    = default;

    Parsed(value_type options)
        requires (!std::is_void_v<T>)
        : state(std::in_place_index<0>, std::move(options)) {}

    Parsed(Help request) : state(std::in_place_index<1>, std::move(request)) {}

    Parsed(Error reason) : state(std::in_place_index<2>, std::move(reason)) {}

    /// Whether the argv gives options, neither asking for help nor failing to parse.
    bool has_value() const {
        return state.index() == 0;
    }

    explicit operator bool() const {
        return has_value();
    }

    /// The help the argv asks for, or null.
    const Help* help() const {
        return std::get_if<1>(&state);
    }

    /// Why the argv does not parse, or null.
    const Error* error() const {
        return std::get_if<2>(&state);
    }

    /// The options, which there must be.
    value_type& operator*()
        requires (!std::is_void_v<T>) {
        assert(has_value());
        return *std::get_if<0>(&state);
    }

    const value_type& operator*() const
        requires (!std::is_void_v<T>) {
        assert(has_value());
        return *std::get_if<0>(&state);
    }

    value_type* operator->()
        requires (!std::is_void_v<T>) {
        return &**this;
    }

    const value_type* operator->() const
        requires (!std::is_void_v<T>) {
        return &**this;
    }

private:
    std::variant<value_type, Help, Error> state;
};

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

struct Command;

/// Frees a command, a type complete only where it is built.
struct CommandDeleter {
    void operator()(Command* command) const;
};

using CommandPtr = std::unique_ptr<Command, CommandDeleter>;

/// Builds the command `type`, a struct, describes: its options, positional arguments, groups
/// and subcommands, and their option table.
CommandPtr build_command(const codec::type_info& type);

/// The command of options `T`, which a mistake in them keeps from compiling. Built on first
/// use.
template <typename T>
const Command& command_of() {
    static_assert(check_declaration(codec::type_info::of<T, cli::format>()));
    const static CommandPtr command = build_command(codec::type_info::of<T, cli::format>());
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

/// Reads `argv` as `command` takes it into `reading`, unless it asks for help or does not parse.
Parsed<void> read(const Command& command, string_list_ref argv, Reading& reading);

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
    using format = cli::format;
    /// The parse checks the required arguments itself, unless an option that ends it skips the
    /// check, and a usage decodes the defaults of a subcommand from no arguments at all.
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

/// How an argument spells enumerator `name`: in lower camel case, `light_blue` as
/// `lightBlue`, without the `_` of a keyword-like name (`Delete_`) or the `_` or `V` that
/// makes one starting with a digit an identifier (`_2d`, `V3`).
inline std::string enum_text(std::string_view name) {
    return naming::snake_to_camel(name, false);
}

/// What an argument and an enumerator compare by, so that any spelling of the name reads:
/// its words in lower snake case (`lightBlue`, `light-blue`, `LIGHT_BLUE` and `light_blue`
/// alike), without the `v` of a name that starts with a digit.
inline std::string enum_key(std::string_view text) {
    auto key = naming::normalize_to_lower_snake(text);
    if(key.size() > 1 && key.front() == 'v' && naming::is_digit(key[1])) {
        key.erase(0, 1);
    }
    return key;
}

/// The enumerator `text` names, in any of the spellings enum_key reads alike.
template <typename Enum>
std::optional<Enum> enum_from_text(std::string_view text) {
    const auto key = enum_key(text);
    const auto& names = meta::reflection<Enum>::member_names;
    for(std::size_t i = 0; i < names.size(); ++i) {
        if(enum_key(names[i]) == key) {
            return meta::reflection<Enum>::member_values[i];
        }
    }
    return std::nullopt;
}

template <typename Enum>
std::string invalid_enum_value(std::string_view text) {
    std::string message = std::format("invalid enum value: {}", text);
    const auto& names = meta::reflection<Enum>::member_names;
    if(!names.empty()) {
        message += " (supported: ";
        for(std::size_t i = 0; i < names.size(); ++i) {
            message += i == 0 ? "" : ", ";
            message += enum_text(names[i]);
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
Parsed<void> parse_into(T& options, string_list_ref argv, Visit visit) {
    detail::Reading reading;
    if(auto read = detail::read(detail::command_of<T>(), argv, reading); !read) {
        return read;
    }
    const detail::Node* last = nullptr;
    codec::rich_error error;
    codec::scoped_context<codec::rich_error> guard(error);
    detail::Reader reader{reading.root, &last};
    if(!codec::decode_value(reader, options)) {
        return detail::decode_error(reading, argv, last, error);
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
/// what was read; on help, nothing of it.
template <typename T>
Parsed<void> parse_into(T& options, string_list_ref argv) {
    return parse_into(options, argv, [](T&, const Given&) {});
}

/// parse(), then `visit(options, given)` for each argument it read, in order.
template <typename T, typename Visit>
    requires std::invocable<Visit&, T&, const Given&>
Parsed<T> parse(string_list_ref argv, Visit visit) {
    T options{};
    auto read = parse_into(options, argv, std::move(visit));
    if(auto help = read.help()) {
        return *help;
    }
    if(auto error = read.error()) {
        return *error;
    }
    return options;
}

/// Parses `argv`, the arguments after the program's name, into the options `T` declares: a
/// reflectable struct, each field an option, a positional argument (`cli::positional`), a
/// group of them (a struct), or subcommands (a std::variant of structs). A field the argv does
/// not give keeps the value it starts with. Every command, the program's and each subcommand's,
/// takes -h and --help besides, each unless it names an option so itself: given, the parse ends
/// there and asks for the usage of that command (Help). The arguments before it are read as
/// ever, an unknown option or a value out of its field's range failing the parse, but none is
/// decoded: one its field's type cannot take, "--jobs many", goes unnoticed.
template <typename T>
Parsed<T> parse(string_list_ref argv) {
    return parse<T>(argv, [](T&, const Given&) {});
}

/// parse() of any other range of strings, a view such as `std::views::transform`'s.
template <typename T, std::ranges::input_range R>
    requires (!std::convertible_to<R, string_list_ref> &&
              std::convertible_to<std::ranges::range_reference_t<R>, std::string_view>)
Parsed<T> parse(R&& argv) {
    std::vector<std::string> copy;
    for(auto&& argument: argv) {
        copy.emplace_back(std::string_view(argument));
    }
    return parse<T>(string_list_ref(copy));
}

}  // namespace kotatsu::cli

namespace kotatsu::codec {

/// An enum reads from any spelling of its name: `info`, `Info`, `INFO`.
template <typename E>
    requires (std::is_enum_v<E> && !codec::has_repr<E, cli::format>)
struct deserialize_visit<cli::detail::Reader, E> {
    static bool visit(cli::detail::Reader& reader, E& out) {
        if(reader.node.kind != cli::detail::Node::Kind::Text) {
            return reader.fail_shape("a name");
        }
        auto value = cli::detail::enum_from_text<E>(reader.node.text);
        if(!value) {
            return reader.fail(cli::detail::invalid_enum_value<E>(reader.node.text));
        }
        out = *value;
        return true;
    }
};

}  // namespace kotatsu::codec
