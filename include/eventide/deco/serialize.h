#pragma once

#include <cstdint>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "eventide/deco/backend.h"

namespace deco::ser {

template <typename StructTy>
class Serializer : public deco::detail::DecoStructConsumer<Serializer<StructTy>, StructTy> {
    using base_t = detail::DecoStructConsumer<Serializer<StructTy>, StructTy>;
    using category_span_t = std::span<const decl::Category* const>;

    const StructTy& object;
    category_span_t categories;
    std::vector<std::string> argv;
    std::vector<std::string> trailing_values;
    bool has_trailing = false;

    constexpr bool category_selected(const decl::Category* category) const {
        if(categories.empty()) {
            return true;
        }
        for(const auto* selected: categories) {
            if(selected == category) {
                return true;
            }
        }
        return false;
    }

    template <typename CfgTy>
    constexpr bool should_emit(const CfgTy& cfg) const {
        return category_selected(cfg.category.ptr());
    }

    static std::string normalized_field_name(std::string_view field_name) {
        std::string normalized(field_name);
        for(auto& ch: normalized) {
            if(ch == '_') {
                ch = '-';
            }
        }
        return normalized;
    }

    static std::string generated_option_name(std::string_view field_name) {
        auto normalized = normalized_field_name(field_name);
        if(normalized.empty()) {
            return {};
        }
        if(normalized.size() == 1) {
            return "-" + normalized;
        }
        return "--" + normalized;
    }

    template <typename CfgTy>
    static std::string option_name(const CfgTy& cfg, std::string_view field_name) {
        if constexpr(requires { cfg.names; }) {
            if(!cfg.names.empty()) {
                return std::string(cfg.names.front());
            }
        }
        return generated_option_name(field_name);
    }

    constexpr static bool has_kv_style(char style, decl::KVStyle expected) {
        return (style & static_cast<char>(expected)) != 0;
    }

    static bool name_has_joined_suffix(std::string_view name) {
        return name.ends_with('=') || name.ends_with(':');
    }

    static std::string kv_joined_arg(std::string_view name, std::string_view value) {
        return std::string(name) + std::string(value);
    }

    template <typename ValueTy>
    static std::string scalar_to_arg(const ValueTy& value) {
        using raw_ty = std::remove_cvref_t<ValueTy>;
        if constexpr(std::same_as<raw_ty, std::string>) {
            return value;
        } else if constexpr(std::same_as<raw_ty, std::string_view>) {
            return std::string(value);
        } else if constexpr(std::same_as<raw_ty, const char*> || std::same_as<raw_ty, char*>) {
            return value ? std::string(value) : std::string{};
        } else if constexpr(std::same_as<raw_ty, bool>) {
            return value ? "true" : "false";
        } else if constexpr(std::signed_integral<raw_ty>) {
            return std::to_string(static_cast<long long>(value));
        } else if constexpr(std::unsigned_integral<raw_ty>) {
            return std::to_string(static_cast<unsigned long long>(value));
        } else if constexpr(std::floating_point<raw_ty>) {
            return std::to_string(static_cast<long double>(value));
        } else if constexpr(requires(std::ostream& os, const raw_ty& v) { os << v; }) {
            std::ostringstream oss;
            oss << value;
            return oss.str();
        } else {
            static_assert(eventide::dependent_false<raw_ty>,
                          "Unsupported scalar value type for deco::ser::Serializer.");
            return {};
        }
    }

    template <typename VectorTy>
    static std::vector<std::string> vector_to_args(const VectorTy& values) {
        using raw_ty = std::remove_cvref_t<VectorTy>;
        if constexpr(std::ranges::range<raw_ty>) {
            std::vector<std::string> output;
            for(const auto& value: values) {
                output.push_back(scalar_to_arg(value));
            }
            return output;
        } else {
            static_assert(eventide::dependent_false<raw_ty>,
                          "Vector option result type must be a range for serialization.");
            return {};
        }
    }

public:
    explicit Serializer(const StructTy& object, category_span_t categories = {}) :
        object(object), categories(categories) {}

    std::vector<std::string> to_argv() {
        argv.clear();
        trailing_values.clear();
        has_trailing = false;
        (void)this->consume_deco_struct(object);
        if(has_trailing) {
            argv.emplace_back("--");
            for(auto& value: trailing_values) {
                argv.push_back(std::move(value));
            }
        }
        return argv;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    bool on_input_config(const FieldTy& field,
                         const CfgTy& cfg,
                         std::string_view,
                         std::index_sequence<Path...>) {
        (void)sizeof...(Path);
        if(!should_emit(cfg) || !field.has_value()) {
            return true;
        }
        using result_ty = typename std::remove_cvref_t<FieldTy>::result_type;
        if constexpr(trait::ScalarResultType<result_ty>) {
            argv.push_back(scalar_to_arg(*field));
        } else {
            auto values = vector_to_args(*field);
            for(auto& value: values) {
                argv.push_back(std::move(value));
            }
        }
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    bool on_trailing_input_config(const FieldTy& field,
                                  const CfgTy& cfg,
                                  std::string_view,
                                  std::index_sequence<Path...>) {
        (void)sizeof...(Path);
        if(!should_emit(cfg) || !field.has_value()) {
            return true;
        }
        has_trailing = true;
        trailing_values = vector_to_args(*field);
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    bool on_flag_config(const FieldTy& field,
                        const CfgTy& cfg,
                        std::string_view field_name,
                        std::index_sequence<Path...>) {
        (void)sizeof...(Path);
        if(!should_emit(cfg) || !field.has_value()) {
            return true;
        }
        const auto name = option_name(cfg, field_name);
        using result_ty = typename std::remove_cvref_t<FieldTy>::result_type;
        if constexpr(std::same_as<result_ty, bool>) {
            if(*field) {
                argv.push_back(name);
            }
        } else {
            const auto count = static_cast<std::uint32_t>(*field);
            for(std::uint32_t i = 0; i < count; ++i) {
                argv.push_back(name);
            }
        }
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    bool on_kv_config(const FieldTy& field,
                      const CfgTy& cfg,
                      std::string_view field_name,
                      std::index_sequence<Path...>) {
        (void)sizeof...(Path);
        if(!should_emit(cfg) || !field.has_value()) {
            return true;
        }
        const auto name = option_name(cfg, field_name);
        const auto value = scalar_to_arg(*field);
        const bool allow_joined = has_kv_style(cfg.style, decl::KVStyle::Joined);
        const bool allow_separate = has_kv_style(cfg.style, decl::KVStyle::Separate);
        const bool use_joined = name_has_joined_suffix(name) || (allow_joined && !allow_separate);
        if(use_joined) {
            argv.push_back(kv_joined_arg(name, value));
        } else {
            argv.push_back(name);
            argv.push_back(value);
        }
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    bool on_comma_joined_config(const FieldTy& field,
                                const CfgTy& cfg,
                                std::string_view field_name,
                                std::index_sequence<Path...>) {
        (void)sizeof...(Path);
        if(!should_emit(cfg) || !field.has_value()) {
            return true;
        }
        auto values = vector_to_args(*field);
        if(values.empty()) {
            return true;
        }
        std::string token = option_name(cfg, field_name);
        for(const auto& value: values) {
            token.push_back(',');
            token += value;
        }
        argv.push_back(std::move(token));
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    bool on_multi_config(const FieldTy& field,
                         const CfgTy& cfg,
                         std::string_view field_name,
                         std::index_sequence<Path...>) {
        (void)sizeof...(Path);
        if(!should_emit(cfg) || !field.has_value()) {
            return true;
        }
        auto values = vector_to_args(*field);
        if(values.empty()) {
            return true;
        }
        argv.push_back(option_name(cfg, field_name));
        for(const auto& value: values) {
            argv.push_back(value);
        }
        return true;
    }
};

template <typename StructTy>
std::vector<std::string> to_argv(const StructTy& object) {
    return Serializer<StructTy>(object).to_argv();
}

template <typename StructTy>
std::vector<std::string> to_argv(const StructTy& object, const decl::Category& category) {
    const decl::Category* selected_categories[] = {&category};
    return Serializer<StructTy>(object, std::span<const decl::Category* const>(selected_categories))
        .to_argv();
}

template <typename StructTy>
std::vector<std::string> to_argv(const StructTy& object,
                                 std::span<const decl::Category* const> categories) {
    return Serializer<StructTy>(object, categories).to_argv();
}

}  // namespace deco::ser
