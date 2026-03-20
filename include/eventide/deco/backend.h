#pragma once
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "eventide/deco/decl.h"
#include "eventide/deco/ty.h"
#include "eventide/common/comptime.h"
#include "eventide/common/memory.h"
#include "eventide/common/meta.h"

namespace deco::detail {

using namespace eventide::comptime;

struct ParsedNamedOption {
    std::span<const std::string_view> prefixes = backend::pfx_none;
    std::string_view prefix;
    std::string_view name;
};

constexpr auto parse_named_option(std::string_view full_name) {
    if(full_name.starts_with("--")) {
        if(full_name.size() <= 2) {
            ETD_THROW("Option name cannot be only '--'");
        }
        return ParsedNamedOption{backend::pfx_double, "--", full_name.substr(2)};
    }
    if(full_name.starts_with("-")) {
        if(full_name.size() <= 1) {
            ETD_THROW("Option name cannot be only '-'");
        }
        return ParsedNamedOption{backend::pfx_dash, "-", full_name.substr(1)};
    }
    if(full_name.starts_with("/")) {
        if(full_name.size() <= 1) {
            ETD_THROW("Option name cannot be only '/'");
        }
        return ParsedNamedOption{backend::pfx_slash_dash, "/", full_name.substr(1)};
    }
    ETD_THROW("Option name must start with '-', '--', or '/'");
}

template <typename Derived, typename RootTy>
class DecoStructConsumer {
public:
    using accessor_fn = void* (*)(void*);

private:
    struct config_state {
        decl::ConfigFields cfg{};
        std::size_t level = 0;
    };

    constexpr static void config_push(std::vector<config_state>& config_stack,
                                      const decl::ConfigFields& cfg,
                                      std::size_t level) {
        config_stack.push_back(config_state{.cfg = cfg, .level = level});
    }

    constexpr static void config_pop_nearest_start(std::vector<config_state>& config_stack) {
        for(std::size_t i = config_stack.size(); i > 0; --i) {
            if(config_stack[i - 1].cfg.type == decl::ConfigFields::Type::Start) {
                config_stack.resize(i - 1);
                return;
            }
        }
        ETD_THROW("Unmatched config end field");
    }

    constexpr static void config_consume_next(std::vector<config_state>& config_stack,
                                              std::size_t level) {
        config_stack.erase(std::remove_if(config_stack.begin(),
                                          config_stack.end(),
                                          [level](const config_state& item) {
                                              return item.level == level &&
                                                     item.cfg.type ==
                                                         decl::ConfigFields::Type::Next;
                                          }),
                           config_stack.end());
    }

    constexpr static void on_config_field(std::vector<config_state>& config_stack,
                                          const auto& cfg_owner,
                                          std::size_t level) {
        auto cfg = ty::cfg_ty_of<decltype(cfg_owner)>{};
        switch(cfg.type) {
            case decl::ConfigFields::Type::Start: config_push(config_stack, cfg, level); break;
            case decl::ConfigFields::Type::End: config_pop_nearest_start(config_stack); break;
            case decl::ConfigFields::Type::Next: config_push(config_stack, cfg, level); break;
        }
    }

    template <typename OptTy>
    constexpr static void apply_current_config(OptTy& opt,
                                               const std::vector<config_state>& config_stack) {
        for(const auto& cfg_state: config_stack) {
            const auto& cfg = cfg_state.cfg;
            if(cfg.required.is_overridden()) {
                opt.required = cfg.required.get();
            }
            if(cfg.category.is_overridden()) {
                opt.category = cfg.category.get();
            }
            if(cfg.help.is_overridden()) {
                opt.help = cfg.help.get();
            }
            if(cfg.meta_var.is_overridden()) {
                opt.meta_var = cfg.meta_var.get();
            }
        }
    }

    template <typename CfgTy>
    constexpr static CfgTy make_configured_cfg(const std::vector<config_state>& config_stack) {
        static_assert(std::is_base_of_v<decl::CommonOptionFields, CfgTy>);
        CfgTy cfg{};
        apply_current_config(static_cast<decl::CommonOptionFields&>(cfg), config_stack);
        return cfg;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr static bool dispatch_deco_option(Derived& derived,
                                               const FieldTy& field,
                                               const CfgTy& cfg,
                                               std::string_view field_name,
                                               std::index_sequence<Path...> path) {
        if constexpr(CfgTy::deco_field_ty == decl::DecoType::Input) {
            return bool(derived.on_input_config(field, cfg, field_name, path));
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::TrailingInput) {
            return bool(derived.on_trailing_input_config(field, cfg, field_name, path));
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::Flag) {
            return bool(derived.on_flag_config(field, cfg, field_name, path));
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::KV) {
            return bool(derived.on_kv_config(field, cfg, field_name, path));
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::CommaJoined) {
            return bool(derived.on_comma_joined_config(field, cfg, field_name, path));
        } else if constexpr(CfgTy::deco_field_ty == decl::DecoType::Multi) {
            return bool(derived.on_multi_config(field, cfg, field_name, path));
        } else {
            static_assert(eventide::dependent_false<CfgTy>, "Unsupported deco cfg type.");
            return true;
        }
    }

    template <typename CurrentTy, typename OnOption, std::size_t... Path>
    constexpr static bool visit_fields_impl(const CurrentTy& object,
                                            std::vector<config_state>& config_stack,
                                            std::size_t level,
                                            OnOption& on_option) {
        return refl::for_each(object, [&](auto field) {
            using FieldTy = ty::base_ty<typename decltype(field)::type>;
            constexpr auto idx = decltype(field)::index();
            constexpr auto name = decltype(field)::name();
            if constexpr(ty::is_config_field<FieldTy>) {
                on_config_field(config_stack, field.value(), level);
                return true;
            } else if constexpr(ty::deco_option_like<FieldTy>) {
                using CfgTy = ty::field_ty_of<FieldTy>;
                const auto cfg = make_configured_cfg<CfgTy>(config_stack);
                const bool keep_going =
                    bool(on_option(field.value(), cfg, name, std::index_sequence<Path..., idx>{}));
                config_consume_next(config_stack, level);
                return keep_going;
            } else if constexpr(refl::reflectable_class<FieldTy>) {
                const bool keep_going =
                    visit_fields_impl<FieldTy, OnOption, Path..., idx>(field.value(),
                                                                       config_stack,
                                                                       level + 1,
                                                                       on_option);
                config_consume_next(config_stack, level);
                return keep_going;
            } else {
                static_assert(eventide::dependent_false<FieldTy>,
                              "Only deco fields or nested option structs are supported.");
                return true;
            }
        });
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
        if(object == nullptr) {
            return nullptr;
        }
        auto& field = field_by_path<RootTy, Path...>(*static_cast<RootTy*>(object));
        return static_cast<void*>(&field);
    }

    template <std::size_t... Path>
    constexpr static accessor_fn accessor_from_path(std::index_sequence<Path...>) {
        return &field_accessor<Path...>;
    }

public:
    template <typename OnOption>
    constexpr bool visit_fields(const RootTy& object, OnOption&& on_option) const {
        std::vector<config_state> config_stack;
        return visit_fields_impl<RootTy>(object, config_stack, 0, on_option);
    }

    constexpr bool consume_deco_struct(const RootTy& object = {}) {
        static_assert(refl::reflectable_class<RootTy>,
                      "DecoStructConsumer root type must be a reflectable struct");
        auto on_option = [this](const auto& field,
                                const auto& cfg,
                                std::string_view field_name,
                                auto path) {
            return dispatch_deco_option(static_cast<Derived&>(*this), field, cfg, field_name, path);
        };
        return visit_fields(object, on_option);
    }
};

template <typename ResourceTy>
class StrPool {
    ResourceTy& resource;

    template <typename... Args>
        requires ((std::is_convertible_v<Args, std::string_view> && ...))
    constexpr void add_into(char* mem, std::string_view first, Args&&... args) {
        char* out = mem;
        auto append = [&](std::string_view part) {
            std::copy(part.data(), part.data() + part.size(), out);
            out += part.size();
        };
        append(first);
        (append(std::string_view(args)), ...);
    }

public:
    constexpr explicit StrPool(ResourceTy& resource) : resource(resource) {}

    template <typename... Args>
        requires ((std::is_convertible_v<Args, std::string_view> && ...))
    constexpr std::string_view add(std::string_view first, Args&&... args) {
        auto total_size = (first.size() + ... + std::string_view(args).size()) + 1;
        auto* mem = resource.template allocate_type<char>(total_size);
        if(ResourceTy::is_counting) {
            resource.template deallocate_type<char>(mem, total_size);
            return first;
        }
        add_into(mem, first, std::forward<Args>(args)...);
        mem[total_size - 1] = '\0';
        return std::string_view(mem, total_size - 1);
    }

    constexpr std::string_view add_replace(std::string_view str, char old_char, char new_char) {
        const auto total_size = str.size() + 1;
        auto* mem = resource.template allocate_type<char>(total_size);
        if(ResourceTy::is_counting) {
            resource.template deallocate_type<char>(mem, total_size);
            return str;
        }
        for(std::size_t i = 0; i < str.size(); ++i) {
            mem[i] = (str[i] == old_char) ? new_char : str[i];
        }
        mem[str.size()] = '\0';
        return std::string_view(mem, str.size());
    }

    constexpr std::size_t size() const {
        return resource.used_size();
    }
};

template <typename RootTy, auto record = eventide::comptime::counting_flag<3>>
class LLVMOptGenerator : public DecoStructConsumer<LLVMOptGenerator<RootTy, record>, RootTy> {
    using base_t = DecoStructConsumer<LLVMOptGenerator<RootTy, record>, RootTy>;

public:
    using accessor_fn = typename base_t::accessor_fn;

private:
    using info_item = backend::OptTable::Info;
    using resource_ty = eventide::comptime::ComptimeMemoryResource<record>;
    using item_pool_type = eventide::comptime::ComptimeVector<info_item, resource_ty, 0>;
    using id_map_type = eventide::comptime::ComptimeVector<accessor_fn, resource_ty, 1>;
    using category_map_type =
        eventide::comptime::ComptimeVector<const decl::Category*, resource_ty, 2>;

    // Keep a dummy at index 0 so item.id can be used as direct index.
    resource_ty resource{};
    StrPool<resource_ty> strPool;
    item_pool_type itemPool{};
    id_map_type idMap{};
    category_map_type categoryMap{};

    bool hasInputSlot = false;
    bool hasTrailingSlot = false;
    bool hasTrailingPack = false;
    unsigned inputOptionId = 0;
    accessor_fn trailingAccessor = nullptr;
    const decl::Category* trailingCategory = nullptr;

    constexpr static auto make_default_item(unsigned id) {
        return info_item::unaliased_one(backend::pfx_none,
                                        "",
                                        id,
                                        backend::Option::UnknownClass,
                                        0,
                                        "no help text",
                                        "");
    }

    constexpr auto& item_by_id(unsigned id) {
        return itemPool[id];
    }

    constexpr auto& new_item(accessor_fn mapped_accessor = nullptr) {
        const auto item_id = static_cast<unsigned>(itemPool.size());
        itemPool.push_back(make_default_item(item_id));
        auto& item = itemPool.back();
        item.id = item_id;
        idMap.push_back(mapped_accessor);
        categoryMap.push_back(nullptr);
        return item;
    }

    constexpr void set_category_for_item(unsigned item_id, const decl::Category* category) {
        categoryMap[item_id] = category;
    }

    constexpr auto& set_common_options(info_item& item, const decl::CommonOptionFields& fields) {
        if(!fields.help.empty()) {
            item.help_text = strPool.add(fields.help).data();
        }
        if(!fields.meta_var.empty()) {
            item.meta_var = strPool.add(fields.meta_var).data();
        }
        return item;
    }

    constexpr std::string_view generate_name_from_field(std::string_view field_name,
                                                        bool with_prefix = false) {
        auto normalized_name = strPool.add_replace(field_name, '_', '-');
        if(!with_prefix) {
            return normalized_name;
        }
        if(normalized_name.size() == 1) {
            return strPool.add("-", normalized_name);
        } else {
            return strPool.add("--", normalized_name);
        }
    }

    constexpr void set_generated_name_from_field(info_item& item,
                                                 std::string_view field_name,
                                                 std::string_view suffix = {}) {
        auto normalized_name = generate_name_from_field(field_name);
        if(normalized_name.size() == 1) {
            item._prefixes = backend::pfx_dash;
            item._prefixed_name = suffix.empty() ? strPool.add("-", normalized_name)
                                                 : strPool.add("-", normalized_name, suffix);
        } else {
            item._prefixes = backend::pfx_double;
            item._prefixed_name = suffix.empty() ? strPool.add("--", normalized_name)
                                                 : strPool.add("--", normalized_name, suffix);
        }
    }

    constexpr void set_prefixed_name(info_item& target, std::string_view full_name) {
        auto parsed = parse_named_option(full_name);
        target._prefixes = parsed.prefixes;
        target._prefixed_name = strPool.add(parsed.prefix, parsed.name);
    }

    constexpr auto& set_named_options(unsigned item_id,
                                      accessor_fn mapped_accessor,
                                      std::string_view field_name,
                                      const decl::NamedOptionFields& fields) {
        const auto category = fields.category.ptr();
        auto& item = item_by_id(item_id);
        if(fields.names.empty()) {
            set_generated_name_from_field(item, field_name);
            set_common_options(item, fields);
            set_category_for_item(item.id, category);
            return item;
        }

        set_prefixed_name(item, fields.names.front());
        set_category_for_item(item.id, category);

        const auto item_snapshot = item;
        for(std::size_t i = 1; i < fields.names.size(); ++i) {
            auto& alias = new_item(mapped_accessor);
            auto alias_id = alias.id;
            alias = item_snapshot;
            alias.id = alias_id;
            set_prefixed_name(alias, fields.names[i]);
            set_common_options(alias, fields);
            set_category_for_item(alias.id, category);
        }

        set_common_options(item_by_id(item_id), fields);
        return item_by_id(item_id);
    }

    constexpr void add_input_option(const decl::CommonOptionFields& cfg,
                                    accessor_fn mapped_accessor) {
        if(hasInputSlot) {
            ETD_THROW("Only one DecoInput can be declared");
        }
        hasInputSlot = true;
        if(inputOptionId == 0) {
            auto& item = new_item(mapped_accessor);
            item = info_item::input(item.id);
            inputOptionId = item.id;
        }
        idMap[inputOptionId] = mapped_accessor;
        set_common_options(item_by_id(inputOptionId), cfg);
        set_category_for_item(inputOptionId, cfg.category.ptr());
    }

    constexpr void add_trailing_option(const decl::CommonOptionFields& cfg,
                                       accessor_fn mapped_accessor) {
        if(hasTrailingSlot) {
            ETD_THROW("Only one DecoPack can be declared");
        }
        hasTrailingSlot = true;
        hasTrailingPack = true;
        trailingAccessor = mapped_accessor;
        trailingCategory = cfg.category.ptr();

        // The backend only has one input id slot. If trailing appears first, reserve that slot
        // now so parse_args can still emit a valid input option id.
        if(inputOptionId == 0) {
            auto& item = new_item(mapped_accessor);
            item = info_item::input(item.id);
            inputOptionId = item.id;
            set_common_options(item, cfg);
            set_category_for_item(item.id, cfg.category.ptr());
        }
    }

    constexpr void add_flag_option(const decl::FlagFields& cfg,
                                   accessor_fn mapped_accessor,
                                   std::string_view field_name) {
        auto& item = new_item(mapped_accessor);
        item.kind = backend::Option::FlagClass;
        item.param = 0;
        set_named_options(item.id, mapped_accessor, field_name, cfg);
    }

    constexpr static bool has_kv_style(char style, decl::KVStyle expected) {
        return (style & static_cast<char>(expected)) != 0;
    }

    constexpr static unsigned char kv_kind_from_name(std::string_view full_name) {
        if(full_name.ends_with('=') || full_name.ends_with(':')) {
            return backend::Option::JoinedClass;
        }
        return backend::Option::SeparateClass;
    }

    constexpr void add_generated_kv_joined_alias(unsigned item_id,
                                                 accessor_fn mapped_accessor,
                                                 std::string_view field_name,
                                                 const decl::KVFields& fields) {
        const auto base_item = item_by_id(item_id);
        auto& alias = new_item(mapped_accessor);
        auto alias_id = alias.id;
        alias = base_item;
        alias.id = alias_id;
        alias.kind = backend::Option::JoinedClass;
        set_generated_name_from_field(alias, field_name, "=");
        set_common_options(alias, fields);
        set_category_for_item(alias.id, fields.category.ptr());
    }

    constexpr auto& set_kv_options_split_by_name(unsigned item_id,
                                                 accessor_fn mapped_accessor,
                                                 std::string_view field_name,
                                                 const decl::KVFields& fields) {
        const auto category = fields.category.ptr();
        auto& item = item_by_id(item_id);

        auto set_kv_name_and_kind = [&](info_item& target, std::string_view full_name) {
            set_prefixed_name(target, full_name);
            target.kind = kv_kind_from_name(full_name);
        };

        if(fields.names.empty()) {
            set_generated_name_from_field(item, field_name);
            item.kind = backend::Option::SeparateClass;
            set_common_options(item, fields);
            set_category_for_item(item.id, category);
            add_generated_kv_joined_alias(item.id, mapped_accessor, field_name, fields);
            return item_by_id(item_id);
        }

        set_kv_name_and_kind(item, fields.names.front());
        set_category_for_item(item.id, category);

        const auto item_snapshot = item;
        for(std::size_t i = 1; i < fields.names.size(); ++i) {
            auto& alias = new_item(mapped_accessor);
            auto alias_id = alias.id;
            alias = item_snapshot;
            alias.id = alias_id;
            set_kv_name_and_kind(alias, fields.names[i]);
            set_common_options(alias, fields);
            set_category_for_item(alias.id, category);
        }

        set_common_options(item_by_id(item_id), fields);
        return item_by_id(item_id);
    }

    constexpr void add_kv_option(const decl::KVFields& cfg,
                                 accessor_fn mapped_accessor,
                                 std::string_view field_name) {
        const bool allow_joined = has_kv_style(cfg.style, decl::KVStyle::Joined);
        const bool allow_separate = has_kv_style(cfg.style, decl::KVStyle::Separate);
        if(!allow_joined && !allow_separate) {
            ETD_THROW("DecoKV style must include Joined and/or Separate");
        }

        auto& item = new_item(mapped_accessor);
        item.param = 1;
        if(allow_joined && allow_separate) {
            set_kv_options_split_by_name(item.id, mapped_accessor, field_name, cfg);
            return;
        }
        item.kind = allow_joined ? backend::Option::JoinedClass : backend::Option::SeparateClass;
        set_named_options(item.id, mapped_accessor, field_name, cfg);
        if(allow_joined && cfg.names.empty()) {
            add_generated_kv_joined_alias(item.id, mapped_accessor, field_name, cfg);
        }
    }

    constexpr void add_comma_option(const decl::CommaJoinedFields& cfg,
                                    accessor_fn mapped_accessor,
                                    std::string_view field_name) {
        auto& item = new_item(mapped_accessor);
        item.kind = backend::Option::CommaJoinedClass;
        item.param = 1;
        set_named_options(item.id, mapped_accessor, field_name, cfg);
    }

    constexpr void add_multi_option(const decl::MultiFields& cfg,
                                    accessor_fn mapped_accessor,
                                    std::string_view field_name) {
        if(cfg.arg_num == 0) {
            ETD_THROW("DecoMulti arg_num must be greater than 0");
        }
        if(cfg.arg_num > std::numeric_limits<unsigned char>::max()) {
            ETD_THROW("DecoMulti arg_num exceeds backend param capacity");
        }
        auto& item = new_item(mapped_accessor);
        item.kind = backend::Option::MultiArgClass;
        item.param = static_cast<unsigned char>(cfg.arg_num);
        set_named_options(item.id, mapped_accessor, field_name, cfg);
    }

    template <typename DecoTy>
    constexpr static bool is_field_present(const DecoTy& field) {
        return field.has_value();
    }

public:
    constexpr static unsigned unknown_option_id = 1;

    constexpr explicit LLVMOptGenerator() :
        strPool(resource), itemPool(resource), idMap(resource), categoryMap(resource) {
        // Dummy item: keeps id and index aligned (id 0 => index 0).
        itemPool.push_back(make_default_item(0));
        idMap.push_back(nullptr);
        categoryMap.push_back(nullptr);

        auto& unknown = new_item(nullptr);
        unknown = info_item::unknown(unknown.id);
    }

    LLVMOptGenerator(const LLVMOptGenerator&) = delete;
    auto operator=(const LLVMOptGenerator&) -> LLVMOptGenerator& = delete;
    LLVMOptGenerator(LLVMOptGenerator&&) = delete;
    auto operator=(LLVMOptGenerator&&) -> LLVMOptGenerator& = delete;

    constexpr explicit LLVMOptGenerator(std::in_place_t) : LLVMOptGenerator() {
        build();
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_input_config(const FieldTy&,
                                   const CfgTy& cfg,
                                   std::string_view,
                                   std::index_sequence<Path...> path) {
        const auto mapped_accessor = base_t::accessor_from_path(path);
        add_input_option(cfg, mapped_accessor);
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_trailing_input_config(const FieldTy&,
                                            const CfgTy& cfg,
                                            std::string_view,
                                            std::index_sequence<Path...> path) {
        const auto mapped_accessor = base_t::accessor_from_path(path);
        add_trailing_option(cfg, mapped_accessor);
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_flag_config(const FieldTy&,
                                  const CfgTy& cfg,
                                  std::string_view field_name,
                                  std::index_sequence<Path...> path) {
        const auto mapped_accessor = base_t::accessor_from_path(path);
        add_flag_option(cfg, mapped_accessor, field_name);
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_kv_config(const FieldTy&,
                                const CfgTy& cfg,
                                std::string_view field_name,
                                std::index_sequence<Path...> path) {
        const auto mapped_accessor = base_t::accessor_from_path(path);
        add_kv_option(cfg, mapped_accessor, field_name);
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_comma_joined_config(const FieldTy&,
                                          const CfgTy& cfg,
                                          std::string_view field_name,
                                          std::index_sequence<Path...> path) {
        const auto mapped_accessor = base_t::accessor_from_path(path);
        add_comma_option(cfg, mapped_accessor, field_name);
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    constexpr bool on_multi_config(const FieldTy&,
                                   const CfgTy& cfg,
                                   std::string_view field_name,
                                   std::index_sequence<Path...> path) {
        const auto mapped_accessor = base_t::accessor_from_path(path);
        add_multi_option(cfg, mapped_accessor, field_name);
        return true;
    }

    constexpr void build() {
        (void)this->consume_deco_struct();
    }

    constexpr bool is_unknown_option_id(backend::OptSpecifier id) const {
        return id.id() == 0 || id.id() == unknown_option_id;
    }

    constexpr bool has_input_option() const {
        return hasInputSlot;
    }

    constexpr bool has_trailing_option() const {
        return hasTrailingSlot;
    }

    constexpr std::size_t opt_size() const {
        return itemPool.size() - 1;
    }

    constexpr std::size_t strpool_size() const {
        return strPool.size();
    }

    constexpr auto option_infos() const {
        return std::span<const info_item>(itemPool.data() + 1, itemPool.size() - 1);
    }

    constexpr auto id_map() const {
        return std::span<const accessor_fn>(idMap.data(), idMap.size());
    }

    constexpr auto category_map() const {
        return std::span<const decl::Category* const>(categoryMap.data(), categoryMap.size());
    }

    constexpr void* field_ptr_of(backend::OptSpecifier opt, RootTy& object) const {
        if(!opt.is_valid()) {
            return nullptr;
        }
        const auto id = opt.id();
        if(id >= id_map().size()) {
            return nullptr;
        }
        auto accessor = idMap[id];
        if(accessor == nullptr) {
            return nullptr;
        }
        return accessor(static_cast<void*>(&object));
    }

    constexpr bool is_input_argument(const backend::ParsedArgument& arg) const {
        if(arg.option_id.id() != inputOptionId) {
            return false;
        }
        return !is_trailing_argument(arg);
    }

    constexpr bool is_trailing_argument(const backend::ParsedArgument& arg) const {
        if(arg.option_id.id() != inputOptionId) {
            return false;
        }
        return arg.get_spelling_view() == "--";
    }

    constexpr void* trailing_ptr_of(RootTy& object) const {
        if(trailingAccessor == nullptr) {
            return nullptr;
        }
        return trailingAccessor(static_cast<void*>(&object));
    }

    constexpr const decl::Category* trailing_category() const {
        return trailingCategory;
    }

    constexpr const decl::Category* category_of(backend::OptSpecifier opt) const {
        if(!opt.is_valid()) {
            return nullptr;
        }
        const auto id = opt.id();
        if(id >= category_map().size()) {
            return nullptr;
        }
        return categoryMap[id];
    }

    auto make_opt_table() const& {
        return backend::OptTable(option_infos(), false, {}, false)
            .set_tablegen_mode(false)
            .set_input_random_index(true)
            .set_dash_dash_parsing(hasTrailingPack)
            .set_dash_dash_as_single_pack(hasTrailingPack)
            .build();
    }

    auto make_opt_table() const&& = delete;

    consteval auto gen_record() const {
        static_assert(resource_ty::is_counting, "gen_record() is only for counting builders");
        return resource.gen_record();
    }
};

template <typename OptDeco>
consteval auto build_record() {
    LLVMOptGenerator<OptDeco> counter;
    counter.build();
    return counter.gen_record();
}

template <typename RootTy, auto record = eventide::comptime::counting_flag<3>>
using OptManager = LLVMOptGenerator<RootTy, record>;

template <typename OptDeco>
struct BuildStorage {
    constexpr inline static auto record = build_record<OptDeco>();
    using builder_t = LLVMOptGenerator<OptDeco, record>;
};

template <typename OptDeco>
const auto& build_storage() {
    const static typename BuildStorage<OptDeco>::builder_t value{std::in_place};
    return value;
}

}  // namespace deco::detail
