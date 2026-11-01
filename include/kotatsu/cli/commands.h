#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kotatsu/cli/detail/declaration.h"
#include "kotatsu/cli/parse.h"
#include "kotatsu/cli/spec.h"
#include "kotatsu/cli/to_argv.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::cli {

/// How cli::add_command() registers a subcommand: the word naming it, its struct's name in kebab
/// case when empty, and whether a usage leaves it out.
struct CommandInfo {
    std::string_view name = {};
    bool hidden = false;
};

namespace detail {

/// A subcommand added to a set, with what reads, writes and runs a value of its struct, whose
/// type is erased.
struct CommandEntry {
    std::string name;
    std::string_view description;
    bool hidden = false;
    const codec::type_info* type = nullptr;
    void* (*make)() = nullptr;
    void (*destroy)(void*) = nullptr;
    bool (*decode)(Reader& reader, void* value) = nullptr;
    bool (*encode)(Writer& writer, const void* value) = nullptr;
    /// The callback added, `R (*)(T&, Args...)` for the set's signature R(Args...), and what
    /// calls it on a value, `R (*)(void (*)(), void*, Args...)`.
    void (*callback)() = nullptr;
    void (*invoke)() = nullptr;
};

/// The subcommands of a set, by name.
struct CommandRegistry {
    std::vector<CommandEntry> entries;
    /// Whether a command was built from it, after which the subcommands' places are fixed.
    bool sealed = false;

    /// Adds `entry` where its name sorts. A name taken already, which no compiler sees as the
    /// files adding both are apart, and an entry after the set was read end the program.
    void add(CommandEntry entry);
};

template <typename Tag>
CommandRegistry& registry() {
    static CommandRegistry commands;
    return commands;
}

/// The subcommands of the set `Tag`, which no file adds to from now on: what a command is built
/// from.
template <typename Tag>
const CommandRegistry& sealed_registry() {
    auto& commands = registry<Tag>();
    commands.sealed = true;
    return commands;
}

/// What a cli::Commands field is: the struct of the subcommand the argv names, its type erased.
template <typename Tag>
class ErasedCommand {
public:
    using signature = typename Tag::signature;

    ErasedCommand() = default;

    ErasedCommand(ErasedCommand&& other) noexcept :
        entry(std::exchange(other.entry, nullptr)), value(std::move(other.value)) {}

    ErasedCommand& operator=(ErasedCommand&& other) noexcept {
        entry = std::exchange(other.entry, nullptr);
        value = std::move(other.value);
        return *this;
    }

    /// Whether it holds a subcommand: the argv named one.
    explicit operator bool() const {
        return entry != nullptr;
    }

    /// The name of the subcommand held, which there must be.
    std::string_view name() const {
        assert(entry != nullptr);
        return entry->name;
    }

    /// The struct held, if it is a `T`; null when it is another, or none.
    template <typename T>
    T* get_if() const {
        return entry != nullptr && entry->type == &codec::type_info::of<T, cli::format>()
                   ? static_cast<T*>(value.get())
                   : nullptr;
    }

    /// Runs the callback added with the subcommand held, which there must be.
    template <typename... CallArgs>
    decltype(auto) operator()(CallArgs&&... args) const {
        assert(entry != nullptr);
        return invoke(static_cast<signature*>(nullptr), std::forward<CallArgs>(args)...);
    }

private:
    template <typename R, typename... Args, typename... CallArgs>
    R invoke(R (*)(Args...), CallArgs&&... args) const {
        const auto call = reinterpret_cast<R (*)(void (*)(), void*, Args...)>(entry->invoke);
        return call(entry->callback, value.get(), std::forward<CallArgs>(args)...);
    }

    template <typename, typename>
    friend struct codec::deserialize_visit;
    template <typename, typename>
    friend struct codec::serialize_visit;

    const CommandEntry* entry = nullptr;
    std::unique_ptr<void, void (*)(void*)> value{nullptr, [](void*) {}};
};

/// A Commands field takes the set `Tag` names: the subcommands every file added to it.
template <typename Tag>
consteval auto __kotatsu__type_annotation(std::type_identity<ErasedCommand<Tag>>) {
    return meta::make_annotation(meta::dsl::member_proxy<&ArgSpec::commands>{} =
                                     CommandSet{true, &sealed_registry<Tag>});
}

}  // namespace detail

/// A field of subcommands that files add to themselves, rather than a std::variant naming
/// them all: `Tag` names the set and its `signature`, the type of the callback each adds,
/// `int(Context&)` say. The field holds the struct of the one the argv names, which `get_if<T>()`
/// returns, and is empty (false) when it names none, which `presence = required` on the field
/// makes an error; calling it runs that one's callback, and `name()` is its word. A subcommand
/// the argv names is read afresh: parse_into() keeps none of one held before.
///
///     struct Top { using signature = int(Context&); };
///     struct Options {
///         KANNOTATE(&help, cli::help)
///         bool help = false;
///         cli::Commands<Top> command;
///     };
///     // serve.cc
///     int run(Serve& serve, Context& context);
///     const auto added = cli::add_command<Top>(run);
template <typename Tag>
using Commands = detail::ErasedCommand<Tag>;

/// Adds the subcommand `T`, run by `run`, to the set `Tag` names, under `info.name` or `T`'s
/// name in kebab case, and returns true. Call it to initialize a variable at namespace scope, in
/// the file that declares `T`; the library holding that file must keep it when linked (Bazel
/// alwayslink), and nothing adds to a set once a command was built from it.
template <typename Tag, typename T, typename R, typename... Args>
bool add_command(R (*run)(T&, Args...), CommandInfo info = {}) {
    static_assert(std::is_same_v<typename Tag::signature, R(Args...)>,
                  "cli: a subcommand's callback takes its struct, then the set's signature");
    static_assert(detail::check_declaration(codec::type_info::of<T, cli::format>()));
    using callback_t = R (*)(T&, Args...);
    const auto& type = codec::type_info::of<T, cli::format>();
    detail::registry<Tag>().add(detail::CommandEntry{
        .name = info.name.empty() ? detail::kebab(type.type_name) : std::string(info.name),
        .description = static_cast<const codec::struct_type_info&>(type).description,
        .hidden = info.hidden,
        .type = &type,
        .make = []() -> void* { return new T(); },
        .destroy = [](void* value) { delete static_cast<T*>(value); },
        .decode =
            [](detail::Reader& reader, void* value) {
                return codec::decode_value<cli::format>(reader, *static_cast<T*>(value));
            },
        .encode =
            [](detail::Writer& writer, const void* value) {
                return codec::encode_value<cli::format>(writer, *static_cast<const T*>(value));
            },
        .callback = reinterpret_cast<void (*)()>(run),
        .invoke =
            reinterpret_cast<void (*)()>(+[](void (*callback)(), void* value, Args... args) -> R {
                return reinterpret_cast<callback_t>(callback)(*static_cast<T*>(value),
                                                              std::forward<Args>(args)...);
            }),
    });
    return true;
}

}  // namespace kotatsu::cli

namespace kotatsu::codec {

/// A set of subcommands reads the one the argv names, into a value of its struct.
template <typename Tag>
struct deserialize_visit<cli::detail::Reader, cli::detail::ErasedCommand<Tag>> {
    static bool visit(cli::detail::Reader& reader, cli::detail::ErasedCommand<Tag>& out) {
        if(reader.node.kind != cli::detail::Node::Kind::Variant) {
            return reader.fail_shape("a subcommand");
        }
        const auto& entry = cli::detail::registry<Tag>().entries[reader.node.count];
        out.entry = &entry;
        out.value = std::unique_ptr<void, void (*)(void*)>(entry.make(), entry.destroy);
        cli::detail::Reader fields{reader.node.items.front(), reader.last};
        return entry.decode(fields, out.value.get());
    }
};

/// A set of subcommands writes the one it holds, as a subcommand of a variant.
template <typename Tag>
struct serialize_visit<cli::detail::Writer, cli::detail::ErasedCommand<Tag>> {
    static bool visit(cli::detail::Writer& writer, const cli::detail::ErasedCommand<Tag>& value) {
        if(!value) {
            return writer.visit_null();
        }
        const auto& entries = cli::detail::registry<Tag>().entries;
        const auto index = static_cast<std::size_t>(value.entry - entries.data());
        return writer.visit_variant(index, [&](cli::detail::Writer& fields) {
            return value.entry->encode(fields, value.value.get());
        });
    }
};

}  // namespace kotatsu::codec
