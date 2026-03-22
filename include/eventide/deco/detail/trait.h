#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "eventide/option/option.h"
#include "eventide/reflection/struct.h"

namespace deco {
namespace backend = eventide::option;
namespace refl = eventide::refl;

namespace decl {

struct IntoContext;

}

}  // namespace deco

namespace deco::trait {

template <typename Ty>
using BaseResultTy = std::remove_cvref_t<Ty>;

template <typename Ty>
concept StringResultType = std::constructible_from<BaseResultTy<Ty>, std::string_view>;

template <typename Ty>
struct OptionalTrait {
    using type = void;
};

template <typename Ty>
struct OptionalTrait<std::optional<Ty>> {
    using type = BaseResultTy<Ty>;
};

template <typename Ty>
using OptionalResultType = OptionalTrait<Ty>::type;

template <typename Ty>
concept CustomStringResultTy = requires(BaseResultTy<Ty>& value, std::string_view sv) {
    requires std::convertible_to<OptionalResultType<decltype(value.into(sv))>, std::string_view>;
};

template <typename Ty>
concept CustomStringResultTyWithContext =
    requires(BaseResultTy<Ty>& value, std::string_view sv, const decl::IntoContext& ctx) {
        requires std::convertible_to<OptionalResultType<decltype(value.into(sv, ctx))>,
                                     std::string_view>;
    };

template <typename Ty>
concept CustomStringVectorResultTy = requires(BaseResultTy<Ty>& value,
                                              std::vector<std::string_view>& vals) {
    requires std::convertible_to<OptionalResultType<decltype(value.into(vals))>, std::string_view>;
};

template <typename Ty>
concept CustomStringVectorResultTyWithContext = requires(BaseResultTy<Ty>& value,
                                                         std::vector<std::string_view>& vals,
                                                         const decl::IntoContext& ctx) {
    requires std::convertible_to<OptionalResultType<decltype(value.into(vals, ctx))>,
                                 std::string_view>;
};

template <typename Ty>
concept FlagResultType =
    std::same_as<BaseResultTy<Ty>, bool> || std::same_as<BaseResultTy<Ty>, uint32_t>;

template <typename Ty>
concept PrimitiveScalarResultType =
    std::same_as<BaseResultTy<Ty>, bool> || std::integral<BaseResultTy<Ty>> ||
    (std::floating_point<BaseResultTy<Ty>> &&
     !std::is_same_v<std::remove_cvref_t<Ty>, long double>) ||
    StringResultType<Ty>;

template <typename Ty>
concept ScalarResultType = PrimitiveScalarResultType<Ty> || CustomStringResultTy<Ty> ||
                           CustomStringResultTyWithContext<Ty>;

template <typename Ty>
concept PrimitiveVectorResultType =
    std::ranges::range<BaseResultTy<Ty>> &&
    requires(BaseResultTy<Ty>& value, std::ranges::range_value_t<BaseResultTy<Ty>> elem) {
        value.clear();
        value.emplace_back(std::move(elem));
    } && PrimitiveScalarResultType<std::ranges::range_value_t<BaseResultTy<Ty>>>;

template <typename Ty>
concept VectorResultType = PrimitiveVectorResultType<Ty> || CustomStringVectorResultTy<Ty> ||
                           CustomStringVectorResultTyWithContext<Ty>;

template <typename Ty>
concept InputResultType = ScalarResultType<Ty> || VectorResultType<Ty>;

}  // namespace deco::trait

#define DecoScalarResultErrString                                                                  \
    "Result type must be a primitive scalar (bool/number/string-like) or provide into(string_view) " "or into(string_view, IntoContext)."

#define DecoVectorResultErrString                                                                  \
    "Result type must be a vector of primitive scalar values or provide into(vector<string_view>) " "or into(vector<string_view>, IntoContext)."

#define DecoInputResultErrString                                                                   \
    "Input result type must be a scalar/string-like value or a vector of primitive scalar values, " "or provide a compatible into(...) overload."
