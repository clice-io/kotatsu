#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "decl.h"
#include "ty.h"
#include "kota/support/comptime.h"
#include "kota/support/config.h"

namespace kota::deco::detail {

/// The prefixes an option named `full_name` accepts: "--name" takes "--", "-n" takes "-", and
/// "/name" takes "/" or "-".
constexpr auto prefixes_of(std::string_view full_name) -> std::span<const std::string_view> {
    if(full_name.starts_with("--")) {
        if(full_name.size() <= 2) {
            KOTA_THROW("Option name cannot be only '--'");
        }
        return backend::pfx_double;
    }
    if(full_name.starts_with("-")) {
        if(full_name.size() <= 1) {
            KOTA_THROW("Option name cannot be only '-'");
        }
        return backend::pfx_dash;
    }
    if(full_name.starts_with("/")) {
        if(full_name.size() <= 1) {
            KOTA_THROW("Option name cannot be only '/'");
        }
        return backend::pfx_slash_dash;
    }
    KOTA_THROW("Option name must start with '-', '--', or '/'");
}

/// Walks a deco struct in declaration order, nested structs included, and hands each option
/// and alias to `Derived` with its declaration after the config fields in scope are applied:
/// consume_deco_struct() hands over the fields of an object, consume_deco_struct_schema()
/// only their types, as `std::type_identity`.
template <typename Derived, typename RootTy>
class DecoStructConsumer {
public:
    using accessor_fn = void* (*)(void*);

private:
    struct ConfigState {
        decl::ConfigFields cfg{};
        std::size_t level = 0;
    };

    /// What option or alias `FieldTy` is declared with.
    template <typename FieldTy>
    using declaration_of = decltype(ty::dyn_cast(std::declval<const FieldTy&>()));

    /// Applies config field `FieldTy`, met at nesting `level`: Start and Next push their
    /// overrides, End drops everything back to the nearest Start.
    template <typename FieldTy>
    constexpr static void apply_config_field(std::vector<ConfigState>& config_stack,
                                             std::size_t level) {
        const auto cfg = ty::cfg_ty_of<FieldTy>{};
        if(cfg.type != decl::ConfigFields::Type::End) {
            config_stack.push_back(ConfigState{.cfg = cfg, .level = level});
            return;
        }
        for(std::size_t i = config_stack.size(); i > 0; --i) {
            if(config_stack[i - 1].cfg.type == decl::ConfigFields::Type::Start) {
                config_stack.resize(i - 1);
                return;
            }
        }
        KOTA_THROW("Unmatched config end field");
    }

    /// Drops the Next configs met at nesting `level`, once the field they apply to is visited.
    constexpr static void drop_next_configs(std::vector<ConfigState>& config_stack,
                                            std::size_t level) {
        std::erase_if(config_stack, [level](const ConfigState& state) {
            return state.level == level && state.cfg.type == decl::ConfigFields::Type::Next;
        });
    }

    /// The declaration `CfgTy` of an option, with the overrides of the configs in scope.
    template <typename CfgTy>
    constexpr static CfgTy make_configured_cfg(const std::vector<ConfigState>& config_stack) {
        static_assert(std::is_base_of_v<decl::CommonOptionFields, CfgTy>);
        CfgTy cfg{};
        for(const auto& state: config_stack) {
            const auto& config = state.cfg;
            if(config.required.is_overridden()) {
                cfg.required = config.required.get();
            }
            if(config.category.is_overridden()) {
                cfg.category = config.category.get();
            }
            if(config.help.is_overridden()) {
                cfg.help = config.help.get();
            }
            if(config.meta_var.is_overridden()) {
                cfg.meta_var = config.meta_var.get();
                cfg.meta_var.explicit_value = true;
            }
        }
        return cfg;
    }

    /// Hands `field`, an option or an alias, to the handler of its kind; `field` is the field
    /// itself, or `std::type_identity` of its type when only the declaration is visited.
    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr static bool dispatch(Derived& derived,
                                   const FieldTy& field,
                                   const CfgTy& cfg,
                                   std::string_view field_name,
                                   std::index_sequence<Path...> path) {
        if constexpr(std::is_base_of_v<decl::AliasFields, CfgTy>) {
            return derived.on_alias(field, cfg, field_name, path);
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::Input) {
            return derived.on_input_config(field, cfg, field_name, path);
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::TrailingInput) {
            return derived.on_trailing_input_config(field, cfg, field_name, path);
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::Flag) {
            return derived.on_flag_config(field, cfg, field_name, path);
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::KV) {
            return derived.on_kv_config(field, cfg, field_name, path);
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::CommaJoined) {
            return derived.on_comma_joined_config(field, cfg, field_name, path);
        } else {
            static_assert(CfgTy::deco_field_ty == decl::DecoType::Multi);
            return derived.on_multi_config(field, cfg, field_name, path);
        }
    }

    template <typename CurrentTy, typename OnOption, std::size_t... Path>
    constexpr static bool visit_fields_impl(const CurrentTy& object,
                                            std::vector<ConfigState>& config_stack,
                                            std::size_t level,
                                            OnOption& on_option) {
        return refl::for_each(object, [&](auto field) {
            using FieldTy = ty::base_ty<typename decltype(field)::type>;
            constexpr auto idx = decltype(field)::index();
            constexpr auto name = decltype(field)::name();
            if constexpr(ty::is_config_field<FieldTy>) {
                apply_config_field<FieldTy>(config_stack, level);
                return true;
            } else if constexpr(ty::deco_option_like<FieldTy> || ty::is_alias_field<FieldTy>) {
                const auto cfg = make_configured_cfg<declaration_of<FieldTy>>(config_stack);
                const bool keep_going =
                    bool(on_option(field.value(), cfg, name, std::index_sequence<Path..., idx>{}));
                drop_next_configs(config_stack, level);
                return keep_going;
            } else if constexpr(refl::reflectable_class<FieldTy>) {
                const bool keep_going =
                    visit_fields_impl<FieldTy, OnOption, Path..., idx>(field.value(),
                                                                       config_stack,
                                                                       level + 1,
                                                                       on_option);
                drop_next_configs(config_stack, level);
                return keep_going;
            } else {
                // A plain member is the struct's own business: deco leaves it alone, and a
                // Next config applies to the deco field after it.
                return true;
            }
        });
    }

    template <typename CurrentTy, std::size_t I, typename OnOption, std::size_t... Path>
    constexpr static bool visit_schema_field(std::vector<ConfigState>& config_stack,
                                             std::size_t level,
                                             OnOption& on_option) {
        using FieldTy = ty::base_ty<refl::field_type<CurrentTy, I>>;
        constexpr auto name = refl::field_name<I, CurrentTy>();
        if constexpr(ty::is_config_field<FieldTy>) {
            apply_config_field<FieldTy>(config_stack, level);
            return true;
        } else if constexpr(ty::deco_option_like<FieldTy> || ty::is_alias_field<FieldTy>) {
            const auto cfg = make_configured_cfg<declaration_of<FieldTy>>(config_stack);
            const bool keep_going = bool(on_option(std::type_identity<FieldTy>{},
                                                   cfg,
                                                   name,
                                                   std::index_sequence<Path..., I>{}));
            drop_next_configs(config_stack, level);
            return keep_going;
        } else if constexpr(refl::reflectable_class<FieldTy>) {
            const bool keep_going =
                visit_schema_fields_impl<FieldTy, OnOption, Path..., I>(config_stack,
                                                                        level + 1,
                                                                        on_option);
            drop_next_configs(config_stack, level);
            return keep_going;
        } else {
            return true;
        }
    }

    template <typename CurrentTy, typename OnOption, std::size_t... Path, std::size_t... Is>
    constexpr static bool visit_schema_fields_indices(std::index_sequence<Is...>,
                                                      std::vector<ConfigState>& config_stack,
                                                      std::size_t level,
                                                      OnOption& on_option) {
        return (
            visit_schema_field<CurrentTy, Is, OnOption, Path...>(config_stack, level, on_option) &&
            ...);
    }

    template <typename CurrentTy, typename OnOption, std::size_t... Path>
    constexpr static bool visit_schema_fields_impl(std::vector<ConfigState>& config_stack,
                                                   std::size_t level,
                                                   OnOption& on_option) {
        return visit_schema_fields_indices<CurrentTy, OnOption, Path...>(
            std::make_index_sequence<refl::field_count<CurrentTy>()>{},
            config_stack,
            level,
            on_option);
    }

protected:
    template <typename ObjTy, std::size_t I>
    constexpr static auto& field_by_path(ObjTy& object) {
        return refl::field_of<I>(object);
    }

    template <typename ObjTy, std::size_t I, std::size_t J, std::size_t... Rest>
    constexpr static auto& field_by_path(ObjTy& object) {
        auto& nested = refl::field_of<I>(object);
        return field_by_path<std::remove_cvref_t<decltype(nested)>, J, Rest...>(nested);
    }

    template <std::size_t... Path>
    static void* field_accessor(void* object) {
        return &field_by_path<RootTy, Path...>(*static_cast<RootTy*>(object));
    }

    template <std::size_t... Path>
    constexpr static accessor_fn accessor_from_path(std::index_sequence<Path...>) {
        return &field_accessor<Path...>;
    }

public:
    /// Calls `on_option(field, cfg, name, path)` for each option and alias of `object`, until
    /// it returns false; returns whether it never did.
    template <typename OnOption>
    constexpr bool visit_fields(const RootTy& object, OnOption&& on_option) const {
        std::vector<ConfigState> config_stack;
        return visit_fields_impl<RootTy>(object, config_stack, 0, on_option);
    }

    /// visit_fields() over the declaration alone: `field` is `std::type_identity` of its type.
    template <typename OnOption>
    constexpr bool visit_schema_fields(OnOption&& on_option) const {
        std::vector<ConfigState> config_stack;
        return visit_schema_fields_impl<RootTy>(config_stack, 0, on_option);
    }

    constexpr void consume_deco_struct(const RootTy& object) {
        static_assert(refl::reflectable_class<RootTy>,
                      "DecoStructConsumer root type must be a reflectable struct");
        visit_fields(object, [this](const auto& field, const auto& cfg, auto name, auto path) {
            return dispatch(static_cast<Derived&>(*this), field, cfg, name, path);
        });
    }

    constexpr void consume_deco_struct_schema() {
        static_assert(refl::reflectable_class<RootTy>,
                      "DecoStructConsumer root type must be a reflectable struct");
        visit_schema_fields([this](const auto& field, const auto& cfg, auto name, auto path) {
            return dispatch(static_cast<Derived&>(*this), field, cfg, name, path);
        });
    }
};

template <typename ResourceTy>
class StrPool {
    ResourceTy& resource;

public:
    constexpr explicit StrPool(ResourceTy& resource) : resource(resource) {}

    /// Copies the concatenation of `first` and `rest` into the pool, NUL-terminated, and
    /// returns it; a counting pool only counts it and returns an empty view.
    template <typename... Args>
        requires ((std::is_convertible_v<Args, std::string_view> && ...))
    constexpr std::string_view add(std::string_view first, Args&&... rest) {
        const auto total_size = (first.size() + ... + std::string_view(rest).size()) + 1;
        auto* mem = resource.template allocate_type<char>(total_size);
        if constexpr(ResourceTy::is_counting) {
            resource.template deallocate_type<char>(mem, total_size);
            return {};
        } else {
            char* out = std::ranges::copy(first, mem).out;
            ((out = std::ranges::copy(std::string_view(rest), out).out), ...);
            *out = '\0';
            return std::string_view(mem, total_size - 1);
        }
    }
};

/// Builds the backend option table of deco struct `RootTy`, together with what the runtime
/// needs about each option, by id: the field it parses into, its category, its `after_parsed`
/// callback and, for an alias, its forward. A counting generator (the default `record`) only
/// measures the pools; built with the record it produced, the generator holds them inline.
template <typename RootTy, auto record = kota::comptime::counting_flag<6>>
class LLVMOptGenerator : public DecoStructConsumer<LLVMOptGenerator<RootTy, record>, RootTy> {
    using base_t = DecoStructConsumer<LLVMOptGenerator<RootTy, record>, RootTy>;

public:
    using accessor_fn = typename base_t::accessor_fn;
    using parse_callback_t = decl::ErasedParseCallback;

    /// What an alias forwards to: the argv its arguments are rewritten into.
    struct AliasRuntimeMeta {
        /// The kind of the alias: Flag, KV, CommaJoined or Multi.
        decl::DecoType kind = decl::DecoType::Flag;
        /// None for an option that is no alias.
        decl::AliasForwardField::Kind forward_kind = decl::AliasForwardField::Kind::None;
        std::span<const std::string_view> static_tokens = {};
        decl::AliasForwardFn dynamic = nullptr;
        decl::AliasForwardFnWithContext dynamic_with_context = nullptr;
    };

    /// Every table has these two: the unknown option, and the input option that positional
    /// arguments parse as, whether the struct declares a DecoInput or not.
    constexpr static std::uint32_t unknown_option_id = 1;
    constexpr static std::uint32_t input_option_id = 2;

private:
    using resource_ty = kota::comptime::ComptimeMemoryResource<record>;

    template <typename T, std::size_t reserved_id>
    using pool_t = kota::comptime::ComptimeVector<T, resource_ty, reserved_id>;

    /// Where a DecoPack parses into: its arguments arrive as the input option spelled "--".
    struct Trailing {
        accessor_fn accessor = nullptr;
        const decl::Category* category = nullptr;
        parse_callback_t callback{};
    };

    resource_ty resource{};
    StrPool<resource_ty> str_pool;
    // Indexed by option id. Id 0 is a dummy, so that ids index them directly.
    pool_t<backend::Option, 0> items;
    pool_t<accessor_fn, 1> accessors;
    pool_t<const decl::Category*, 2> categories;
    pool_t<parse_callback_t, 3> callbacks;
    pool_t<AliasRuntimeMeta, 4> alias_metas;
    // The tokens of static alias forwards, which alias_metas view.
    pool_t<std::string_view, 5> alias_tokens;

    Trailing trailing{};

    /// Adds an option of `kind` taking `num_args` values, which parses into the field
    /// `accessor` reaches (an alias reaches none), and returns its id. It is named later.
    constexpr std::uint32_t new_option(backend::Kind kind,
                                       unsigned char num_args,
                                       accessor_fn accessor,
                                       const decl::Category* category,
                                       parse_callback_t callback = {},
                                       const AliasRuntimeMeta& alias_meta = {}) {
        const auto id = static_cast<std::uint32_t>(items.size());
        items.push_back(backend::Option::unaliased_one(backend::pfx_none,
                                                       "",
                                                       id,
                                                       kind,
                                                       num_args,
                                                       "no help text",
                                                       ""));
        accessors.push_back(accessor);
        categories.push_back(category);
        callbacks.push_back(callback);
        alias_metas.push_back(alias_meta);
        return id;
    }

    /// Adds another spelling of option `id`: a copy of it that parses into the same field,
    /// with the same category, callback and forward, and returns the copy's id.
    constexpr std::uint32_t add_spelling(std::uint32_t id) {
        const auto option = items[id];
        const auto spelling = new_option(option.kind,
                                         option.num_args,
                                         accessors[id],
                                         categories[id],
                                         callbacks[id],
                                         alias_metas[id]);
        items[spelling] = option;
        items[spelling].id = spelling;
        return spelling;
    }

    /// Gives option `id` the name `full_name`: "--name", "-n" or "/name".
    constexpr void set_name(std::uint32_t id, std::string_view full_name) {
        items[id].prefixes = prefixes_of(full_name);
        items[id].prefixed_name = str_pool.add(full_name);
    }

    /// Gives option `id` its help text and meta var.
    template <typename FieldsTy>
    constexpr void set_help(std::uint32_t id, const FieldsTy& fields) {
        if(!fields.help.empty()) {
            items[id].help_text = str_pool.add(fields.help).data();
        }
        if(!fields.meta_var.empty()) {
            items[id].meta_var = str_pool.add(fields.meta_var).data();
        }
    }

    /// Names option `id` after its declaration, and gives it its help, which each spelling then
    /// copies: the first of `fields.names` names it and each further one adds a spelling of it,
    /// of the kind `kind_of(name)` gives; without names, its field's name does.
    template <typename FieldsTy, typename KindOf>
    constexpr void name_option(std::uint32_t id,
                               std::string_view field_name,
                               const FieldsTy& fields,
                               const KindOf& kind_of) {
        set_help(id, fields);
        if(fields.names.empty()) {
            if(decl::is_alias_placeholder_name(field_name)) {
                KOTA_THROW("Deco placeholder fields must declare explicit names");
            }
            set_name(id, decl::detail::generated_option_name(field_name));
            return;
        }
        auto name = [&](std::uint32_t target, std::string_view full_name) {
            set_name(target, full_name);
            items[target].kind = kind_of(full_name);
        };
        name(id, fields.names.front());
        for(const auto full_name: fields.names | std::views::drop(1)) {
            name(add_spelling(id), full_name);
        }
    }

    /// Adds option or alias `fields` of `kind`, whatever its names.
    template <typename FieldsTy>
    constexpr void add_named(backend::Kind kind,
                             unsigned char num_args,
                             const FieldsTy& fields,
                             std::string_view field_name,
                             accessor_fn accessor,
                             parse_callback_t callback,
                             const AliasRuntimeMeta& alias_meta = {}) {
        const auto id =
            new_option(kind, num_args, accessor, fields.category.ptr(), callback, alias_meta);
        name_option(id, field_name, fields, [kind](std::string_view) { return kind; });
    }

    /// Adds KV option or alias `fields`: each explicit name takes its value the way
    /// is_joined_kv_name() says, and a generated name takes it separate, joined to the name
    /// and '=', or both, as the style allows.
    template <typename FieldsTy>
    constexpr void add_kv(const FieldsTy& fields,
                          std::string_view field_name,
                          accessor_fn accessor,
                          parse_callback_t callback,
                          const AliasRuntimeMeta& alias_meta = {}) {
        const bool joined = decl::detail::has_kv_style(fields.style, decl::KVStyle::Joined);
        const bool separate = decl::detail::has_kv_style(fields.style, decl::KVStyle::Separate);
        if(!joined && !separate) {
            KOTA_THROW("KV style must include Joined and/or Separate");
        }
        const auto id = new_option(separate ? backend::Kind::Separate : backend::Kind::Joined,
                                   1,
                                   accessor,
                                   fields.category.ptr(),
                                   callback,
                                   alias_meta);
        name_option(id, field_name, fields, [style = fields.style](std::string_view name) {
            return decl::detail::is_joined_kv_name(style, name) ? backend::Kind::Joined
                                                                : backend::Kind::Separate;
        });
        if(fields.names.empty() && joined) {
            const auto spelling = add_spelling(id);
            items[spelling].kind = backend::Kind::Joined;
            set_name(spelling, decl::detail::generated_option_name(field_name) + "=");
        }
    }

    constexpr static unsigned char checked_arg_num(std::uint32_t arg_num) {
        if(arg_num == 0) {
            KOTA_THROW("DecoMulti arg_num must be greater than 0");
        }
        if(arg_num > std::numeric_limits<unsigned char>::max()) {
            KOTA_THROW("DecoMulti arg_num exceeds backend param capacity");
        }
        return static_cast<unsigned char>(arg_num);
    }

    template <typename ResultTy, typename CallbackTy>
    static auto invoke_parse_callback(parse_callback_t::erased_fn callback,
                                      const ParsedArgOwning& arg,
                                      std::uint32_t next_cursor,
                                      std::span<std::string> argv,
                                      const decl::DecoOptionBase& option) -> decl::ParseControl {
        const auto& typed_option = static_cast<const decl::DecoOption<ResultTy>&>(option);
        const decl::ParseStep<ResultTy> step(arg, next_cursor, argv, typed_option.value());
        return reinterpret_cast<CallbackTy>(callback)(step);
    }

    template <typename CfgTy>
    constexpr static auto make_parse_callback(const CfgTy& cfg) -> parse_callback_t {
        using callback_t = decltype(cfg.after_parsed);
        if constexpr(resource_ty::is_counting) {
            return {};
        } else {
            if(cfg.after_parsed == nullptr) {
                return {};
            }
            return parse_callback_t{
                .callback = reinterpret_cast<parse_callback_t::erased_fn>(cfg.after_parsed),
                .invoke = &invoke_parse_callback<typename CfgTy::result_type, callback_t>,
            };
        }
    }

    /// Copies `tokens` into the pools and returns the copy.
    constexpr auto store_alias_tokens(std::span<const std::string_view> tokens)
        -> std::span<const std::string_view> {
        const auto offset = alias_tokens.size();
        for(const auto token: tokens) {
            alias_tokens.push_back(str_pool.add(token));
        }
        return std::span<const std::string_view>(alias_tokens.data() + offset, tokens.size());
    }

    template <typename CfgTy>
    constexpr auto make_alias_meta(const CfgTy& cfg) -> AliasRuntimeMeta {
        AliasRuntimeMeta meta{
            .kind = CfgTy::deco_field_ty,
            .forward_kind = cfg.forward.kind,
        };
        switch(cfg.forward.kind) {
            case decl::AliasForwardField::Kind::None: std::unreachable();
            case decl::AliasForwardField::Kind::Static:
                meta.static_tokens = store_alias_tokens(cfg.forward.static_tokens);
                break;
            case decl::AliasForwardField::Kind::Dynamic: meta.dynamic = cfg.forward.dynamic; break;
            case decl::AliasForwardField::Kind::DynamicWithContext:
                meta.dynamic_with_context = cfg.forward.dynamic_with_context;
                break;
        }
        return meta;
    }

public:
    constexpr LLVMOptGenerator() :
        str_pool(resource), items(resource), accessors(resource), categories(resource),
        callbacks(resource), alias_metas(resource), alias_tokens(resource) {
        // The dummy, then the unknown and input options.
        new_option(backend::Kind::Unknown, 0, nullptr, nullptr);
        new_option(backend::Kind::Unknown, 0, nullptr, nullptr);
        new_option(backend::Kind::Input, 0, nullptr, nullptr);
        items[unknown_option_id] = backend::Option::unknown(unknown_option_id);
        items[input_option_id] = backend::Option::input(input_option_id);
        this->consume_deco_struct_schema();
    }

    LLVMOptGenerator(const LLVMOptGenerator&) = delete;
    auto operator=(const LLVMOptGenerator&) -> LLVMOptGenerator& = delete;
    LLVMOptGenerator(LLVMOptGenerator&&) = delete;
    auto operator=(LLVMOptGenerator&&) -> LLVMOptGenerator& = delete;

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_input_config(std::type_identity<FieldTy>,
                                   const CfgTy& cfg,
                                   std::string_view,
                                   std::index_sequence<Path...> path) {
        if(has_input_option()) {
            KOTA_THROW("Only one DecoInput can be declared");
        }
        accessors[input_option_id] = base_t::accessor_from_path(path);
        categories[input_option_id] = cfg.category.ptr();
        callbacks[input_option_id] = make_parse_callback(cfg);
        set_help(input_option_id, cfg);
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_trailing_input_config(std::type_identity<FieldTy>,
                                            const CfgTy& cfg,
                                            std::string_view,
                                            std::index_sequence<Path...> path) {
        if(has_trailing_option()) {
            KOTA_THROW("Only one DecoPack can be declared");
        }
        trailing = Trailing{
            .accessor = base_t::accessor_from_path(path),
            .category = cfg.category.ptr(),
            .callback = make_parse_callback(cfg),
        };
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_flag_config(std::type_identity<FieldTy>,
                                  const CfgTy& cfg,
                                  std::string_view field_name,
                                  std::index_sequence<Path...> path) {
        add_named(backend::Kind::Flag,
                  0,
                  cfg,
                  field_name,
                  base_t::accessor_from_path(path),
                  make_parse_callback(cfg));
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_kv_config(std::type_identity<FieldTy>,
                                const CfgTy& cfg,
                                std::string_view field_name,
                                std::index_sequence<Path...> path) {
        add_kv(cfg, field_name, base_t::accessor_from_path(path), make_parse_callback(cfg));
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_comma_joined_config(std::type_identity<FieldTy>,
                                          const CfgTy& cfg,
                                          std::string_view field_name,
                                          std::index_sequence<Path...> path) {
        add_named(backend::Kind::CommaJoined,
                  1,
                  cfg,
                  field_name,
                  base_t::accessor_from_path(path),
                  make_parse_callback(cfg));
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_multi_config(std::type_identity<FieldTy>,
                                   const CfgTy& cfg,
                                   std::string_view field_name,
                                   std::index_sequence<Path...> path) {
        add_named(backend::Kind::MultiArg,
                  checked_arg_num(cfg.arg_num),
                  cfg,
                  field_name,
                  base_t::accessor_from_path(path),
                  make_parse_callback(cfg));
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_alias(std::type_identity<FieldTy>,
                            const CfgTy& cfg,
                            std::string_view field_name,
                            std::index_sequence<Path...>) {
        if(!cfg.forward) {
            KOTA_THROW("Deco alias requires forward");
        }
        const auto meta = make_alias_meta(cfg);
        if constexpr(CfgTy::deco_field_ty == decl::DecoType::Flag) {
            add_named(backend::Kind::Flag, 0, cfg, field_name, nullptr, {}, meta);
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::KV) {
            add_kv(cfg, field_name, nullptr, {}, meta);
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::CommaJoined) {
            add_named(backend::Kind::CommaJoined, 1, cfg, field_name, nullptr, {}, meta);
        } else {
            add_named(backend::Kind::MultiArg,
                      checked_arg_num(cfg.arg_num),
                      cfg,
                      field_name,
                      nullptr,
                      {},
                      meta);
        }
        return true;
    }

    constexpr bool has_input_option() const {
        return accessors[input_option_id] != nullptr;
    }

    constexpr bool has_trailing_option() const {
        return trailing.accessor != nullptr;
    }

    /// The options of the table, from the unknown option on (without the dummy).
    constexpr auto option_infos() const {
        return std::span<const backend::Option>(items.data() + 1, items.size() - 1);
    }

    /// The category of each option, by id; the dummy, unknown and input options have none
    /// unless the struct declares a DecoInput, which the input option then stands for.
    constexpr auto category_map() const {
        return std::span<const decl::Category* const>(categories.data(), categories.size());
    }

    /// The forward of alias `id`, or null when `id` is no alias.
    constexpr auto alias_meta_of(std::uint32_t id) const -> const AliasRuntimeMeta* {
        if(alias_metas[id].forward_kind == decl::AliasForwardField::Kind::None) {
            return nullptr;
        }
        return &alias_metas[id];
    }

    /// The field of `object` that option `id` parses into; `id` must reach one, which the
    /// unknown option, an alias and the input option of a struct without a DecoInput do not.
    constexpr void* field_ptr_of(std::uint32_t id, RootTy& object) const {
        assert(accessors[id] != nullptr);
        return accessors[id](&object);
    }

    constexpr bool is_input_argument(const backend::ParsedArg& arg) const {
        return arg.id == input_option_id && arg.spelling != "--";
    }

    constexpr bool is_trailing_argument(const backend::ParsedArg& arg) const {
        return arg.id == input_option_id && arg.spelling == "--";
    }

    /// The DecoPack of `object`; the struct must declare one.
    constexpr void* trailing_ptr_of(RootTy& object) const {
        assert(has_trailing_option());
        return trailing.accessor(&object);
    }

    constexpr const decl::Category* trailing_category() const {
        return trailing.category;
    }

    constexpr parse_callback_t trailing_callback() const {
        return trailing.callback;
    }

    constexpr const decl::Category* category_of(std::uint32_t id) const {
        return categories[id];
    }

    constexpr parse_callback_t callback_of(std::uint32_t id) const {
        return callbacks[id];
    }

    auto make_opt_table() const& {
        auto table = backend::OptTable(option_infos());
        table.search_includes_input = true;
        return table;
    }

    auto make_opt_table() const&& = delete;

    auto make_parse_options() const {
        backend::ParseOptions opts;
        opts.dash_dash_parsing = has_trailing_option();
        opts.dash_dash_packing = has_trailing_option();
        return opts;
    }

    consteval auto gen_record() const {
        static_assert(resource_ty::is_counting, "gen_record() is only for counting builders");
        return resource.gen_record();
    }
};

template <typename OptDeco>
consteval auto build_record() {
    const LLVMOptGenerator<OptDeco> counter;
    return counter.gen_record();
}

/// The generator of `OptDeco` with its pools sized by a counting pass.
template <typename OptDeco>
struct SizedGenerator {
    constexpr inline static auto record = build_record<OptDeco>();
    using type = LLVMOptGenerator<OptDeco, record>;
};

/// The generator of `OptDeco`, built on first use.
template <typename OptDeco>
const auto& generator_of() {
    const static typename SizedGenerator<OptDeco>::type generator;
    return generator;
}

}  // namespace kota::deco::detail
