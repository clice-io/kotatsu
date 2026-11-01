#pragma once

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
#include "kotatsu/meta/type_info.h"

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
    const meta::type_info* type = nullptr;
    void* (*make)() = nullptr;
    void (*destroy)(void*) = nullptr;
    bool (*decode)(Reader& reader, void* value) = nullptr;
    bool (*encode)(Writer& writer, const void* value) = nullptr;
    /// The callback, of the set's signature R(Args...), as `R (*)(void*, Args...)`.
    void (*run)() = nullptr;
};

/// The subcommands of a set, by name.
struct CommandRegistry {
    std::vector<CommandEntry> entries;

    /// Adds `entry` where its name sorts; a name taken already ends the program, as no
    /// compiler sees the files that add both.
    void add(CommandEntry entry);
};

template <typename Tag>
CommandRegistry& registry() {
    static CommandRegistry commands;
    return commands;
}

template <typename Tag>
const CommandRegistry& registry_of() {
    return registry<Tag>();
}

/// The value a Commands field holds: one subcommand's struct, its type erased.
template <typename Tag>
class ErasedCommand;

template <typename Tag>
struct commands_tag {
    constexpr static auto spec =
        meta::make_spec(meta::dsl::member_proxy<&ArgSpec::commands>{} = &registry_of<Tag>);
};

template <typename Tag>
class ErasedCommand {
public:
    using signature = typename Tag::signature;

    ErasedCommand() = default;

    /// Whether the argv named a subcommand of the set.
    explicit operator bool() const {
        return value != nullptr;
    }

    /// The name of the subcommand held.
    std::string_view name() const {
        return entry->name;
    }

    /// The struct held, if it is a `T`.
    template <typename T>
    T* get_if() const {
        return entry != nullptr && entry->type == &meta::type_info_of<T, decode_config>()
                   ? static_cast<T*>(value.get())
                   : nullptr;
    }

    /// Runs the callback added with the subcommand held, which there must be.
    template <typename... CallArgs>
    decltype(auto) operator()(CallArgs&&... args) const {
        return invoke(static_cast<signature*>(nullptr), std::forward<CallArgs>(args)...);
    }

private:
    template <typename R, typename... Args, typename... CallArgs>
    R invoke(R (*)(Args...), CallArgs&&... args) const {
        const auto run = reinterpret_cast<R (*)(void*, Args...)>(entry->run);
        return run(value.get(), std::forward<CallArgs>(args)...);
    }

    template <typename, typename, typename>
    friend struct codec::deserialize_visit;
    template <typename, typename, typename>
    friend struct codec::serialize_visit;

    const CommandEntry* entry = nullptr;
    std::unique_ptr<void, void (*)(void*)> value{nullptr, [](void*) {}};
};

}  // namespace detail

/// A field of subcommands that files add to themselves, rather than a std::variant naming
/// them all: `Tag` names the set and its `signature`, the type of the callback each adds,
/// `int(Context&)` say. The field holds the struct of the one the argv names, empty when it
/// names none; calling it runs that one's callback.
///
///     struct Top { using signature = int(Context&); };
///     struct Options { cli::Help help; cli::Commands<Top> command; };
///     // serve.cc
///     int run(Serve& serve, Context& context);
///     const auto added = cli::add_command<Top>(run);
template <typename Tag>
using Commands =
    meta::annotate<detail::commands_tag<Tag>>::template type<detail::ErasedCommand<Tag>>;

/// Adds the subcommand `T`, run by `run`, to the set `Tag` names, under `info.name` or `T`'s
/// name in kebab case. Call it to initialize a variable at namespace scope, in the file that
/// declares `T`; the library holding that file must keep it when linked (Bazel alwayslink).
template <typename Tag, typename T, typename R, typename... Args>
bool add_command(R (*run)(T&, Args...), CommandInfo info = {}) {
    static_assert(std::is_same_v<typename Tag::signature, R(Args...)>,
                  "cli: a subcommand's callback takes its struct, then the set's signature");
    static_assert(detail::check_declaration(meta::type_info_of<T, detail::decode_config>()));
    static R (*callback)(T&, Args...) = run;
    const auto& type = meta::type_info_of<T, detail::decode_config>();
    detail::registry<Tag>().add(detail::CommandEntry{
        .name = info.name.empty() ? detail::kebab(type.type_name) : std::string(info.name),
        .description = static_cast<const meta::struct_type_info&>(type).description,
        .hidden = info.hidden,
        .type = &type,
        .make = []() -> void* { return new T(); },
        .destroy = [](void* value) { delete static_cast<T*>(value); },
        .decode =
            [](detail::Reader& reader, void* value) {
                return codec::decode_value<detail::decode_config>(reader, *static_cast<T*>(value));
            },
        .encode =
            [](detail::Writer& writer, const void* value) {
                return codec::encode_value<detail::decode_config>(writer,
                                                                  *static_cast<const T*>(value));
            },
        .run = reinterpret_cast<void (*)()>(+[](void* value, Args... args) -> R {
            return callback(*static_cast<T*>(value), std::forward<Args>(args)...);
        }),
    });
    return true;
}

}  // namespace kotatsu::cli

namespace kotatsu::codec {

/// A set of subcommands reads the one the argv names, into a value of its struct.
template <typename Tag, typename Config>
struct deserialize_visit<cli::detail::Reader, cli::detail::ErasedCommand<Tag>, Config> {
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
template <typename Tag, typename Config>
struct serialize_visit<cli::detail::Writer, cli::detail::ErasedCommand<Tag>, Config> {
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
