#pragma once
#include <type_traits>
#include <utility>

#include "decl.h"
#include "kota/meta/struct.h"

namespace deco::ty {

template <typename T>
using base_ty = std::remove_cvref_t<T>;

// struct {using __deco_cfg_ty = ...;} is a config field, and __deco_cfg_ty is the config type of
// the field.
template <typename T>
concept is_config_field = std::is_base_of_v<decl::ConfigFields, typename base_ty<T>::__deco_cfg_ty>;

template <typename T>
concept is_alias_field = requires { typename base_ty<T>::__deco_alias_ty; };

// deco field is either a config field or derived from decl::DecoFields, which is the base class for
// all option fields.
template <typename T>
concept is_deco_field =
    std::is_base_of_v<decl::DecoFields, base_ty<T>> || is_config_field<T> || is_alias_field<T>;

// field that define the option kind
template <typename T>
concept is_option_field =
    std::is_base_of_v<decl::CommonOptionFields, base_ty<T>> && !is_config_field<T> && requires {
        { base_ty<T>::deco_field_ty } -> std::convertible_to<decl::DecoType>;
    };

// The option with a field ty within the struct
template <typename T>
concept deco_option_like = requires {
    requires std::is_base_of_v<decl::DecoOptionBase, base_ty<T>>;
    typename base_ty<T>::result_type;
    typename base_ty<T>::__deco_field_ty;
    requires is_option_field<typename base_ty<T>::__deco_field_ty>;
};

template <typename T>
concept is_deco_field_or_option = is_deco_field<T> || deco_option_like<T>;

template <typename T>
concept is_decoed = is_config_field<T> || deco_option_like<T> || is_alias_field<T>;

template <typename T>
using field_ty_of = typename base_ty<T>::__deco_field_ty;

template <typename T>
using cfg_ty_of = typename base_ty<T>::__deco_cfg_ty;

template <typename T>
using alias_ty_of = typename base_ty<T>::__deco_alias_ty;

template <is_deco_field_or_option T>
constexpr auto dyn_cast(const T& field) {
    if constexpr(deco_option_like<T>) {
        return field_ty_of<T>{};
    } else if constexpr(is_alias_field<T>) {
        return alias_ty_of<T>{};
    } else {
        return field;
    }
}

}  // namespace deco::ty
