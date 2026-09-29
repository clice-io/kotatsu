#pragma once

#include <cstdint>
#include <format>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "backend.h"
#include "kota/support/type_traits.h"

namespace kota::deco::ser {

template <typename StructTy>
class Serializer : public kota::deco::detail::DecoStructConsumer<Serializer<StructTy>, StructTy> {
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

    /// The name the option is written with: its first name, else the generated one.
    template <typename CfgTy>
    static std::string option_name(const CfgTy& cfg, std::string_view field_name) {
        if(!cfg.names.empty()) {
            return std::string(cfg.names.front());
        }
        return decl::detail::generated_option_name(field_name);
    }

    template <typename ValueTy>
    static std::string scalar_to_arg(const ValueTy& value) {
        if constexpr(std::same_as<ValueTy, std::string>) {
            return value;
        } else if constexpr(std::same_as<ValueTy, bool>) {
            return value ? "true" : "false";
        } else if constexpr(std::integral<ValueTy>) {
            // Promoted, so that a char is written as the number it parses from.
            return std::format("{}", +value);
        } else if constexpr(std::floating_point<ValueTy>) {
            // The shortest text that reads back as the same value.
            return std::format("{}", value);
        } else if constexpr(std::is_enum_v<ValueTy>) {
            return kota::meta::map_enum_to_string(value);
        } else if constexpr(requires(std::ostream& os, const ValueTy& v) { os << v; }) {
            std::ostringstream oss;
            oss << value;
            return oss.str();
        } else {
            static_assert(kota::dependent_false<ValueTy>,
                          "Unsupported scalar value type for kota::deco::ser::Serializer.");
            return {};
        }
    }

    template <typename VectorTy>
    static std::vector<std::string> vector_to_args(const VectorTy& values) {
        if constexpr(std::ranges::range<VectorTy>) {
            std::vector<std::string> output;
            for(const auto& value: values) {
                output.push_back(scalar_to_arg(value));
            }
            return output;
        } else {
            static_assert(kota::dependent_false<VectorTy>,
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
        this->consume_deco_struct(object);
        if(has_trailing) {
            argv.emplace_back("--");
            for(auto& value: trailing_values) {
                argv.push_back(std::move(value));
            }
        }
        return argv;
    }

    /// An alias has no value of its own: the option it forwards to is written instead.
    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    bool on_alias(const FieldTy&, const CfgTy&, std::string_view, std::index_sequence<Path...>) {
        return true;
    }

    template <typename FieldTy, typename CfgTy, std::size_t... Path>
    bool on_input_config(const FieldTy& field,
                         const CfgTy& cfg,
                         std::string_view,
                         std::index_sequence<Path...>) {
        if(!should_emit(cfg) || !field.has_value()) {
            return true;
        }
        using result_ty = typename FieldTy::result_type;
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
        if(!should_emit(cfg) || !field.has_value()) {
            return true;
        }
        const auto name = option_name(cfg, field_name);
        using result_ty = typename FieldTy::result_type;
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
        if(!should_emit(cfg) || !field.has_value()) {
            return true;
        }
        const auto name = option_name(cfg, field_name);
        const auto value = scalar_to_arg(*field);
        if(cfg.names.empty() && decl::detail::has_kv_style(cfg.style, decl::KVStyle::Joined)) {
            // A generated name takes its value after '=': the form that reads back any value,
            // an empty one or one starting with '=' too.
            argv.push_back(name + "=" + value);
        } else if(decl::detail::is_joined_kv_name(cfg.style, name)) {
            argv.push_back(name + value);
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

}  // namespace kota::deco::ser
