#pragma once
#include <cerrno>
#include <charconv>
#include <concepts>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "trait.h"

namespace deco::decl {

enum class DecoType {
    Input,
    // after "--"
    TrailingInput,
    // -p
    Flag,
    // -o 1
    KV,
    // -x,a,b,c
    CommaJoined,
    // -x 1 2 3, fixed size
    Multi,
};

enum KVStyle : char {
    // -KEYValue
    Joined = 1 << 0,
    // -o 1
    Separate = 1 << 1
};

struct Category {
    // If true, any other categories should not occur if this category occurs  in args.
    bool exclusive = false;
    // if true, this category is required in args.
    bool required = false;
    std::string_view name;
    std::string_view description;
};

constexpr inline Category default_category = {
    .exclusive = false,
    .required = false,
    .name = "default",
    .description = "the default category for options",
};

struct CategoryRef {
    const Category* value = &default_category;

    constexpr CategoryRef() = default;

    constexpr CategoryRef(const Category& category) : value(&category) {}

    constexpr auto operator=(const Category& category) -> CategoryRef& {
        value = &category;
        return *this;
    }

    constexpr auto ptr() const -> const Category* {
        return value;
    }

    constexpr auto get() const -> const Category& {
        return *value;
    }

    constexpr auto operator*() const -> const Category& {
        return *value;
    }

    constexpr auto operator->() const -> const Category* {
        return value;
    }

    constexpr operator const Category&() const {
        return *value;
    }
};

struct DecoFields {
    // if true, it's required if its category occurs in options.
    bool required = true;
    // options in the same category (same Category object address) must be all set or all unset
    CategoryRef category = default_category;

    constexpr DecoFields() = default;
};

struct CommonOptionFields : DecoFields {
    std::string_view help = "no provided";
    std::string_view meta_var = "<value>";

    constexpr CommonOptionFields() = default;
};

template <typename Ty>
struct ConfigOverrideField {
    Ty value{};
    bool overridden = false;

    constexpr ConfigOverrideField() = default;

    constexpr ConfigOverrideField(const Ty& value) : value(value) {}

    template <typename U>
        requires std::constructible_from<Ty, U>
    constexpr ConfigOverrideField(U&& value) : value(Ty(std::forward<U>(value))) {}

    template <typename U>
        requires std::constructible_from<Ty, U>
    constexpr auto operator=(U&& value) -> ConfigOverrideField& {
        this->value = Ty(std::forward<U>(value));
        this->overridden = true;
        return *this;
    }

    constexpr auto is_overridden() const -> bool {
        return overridden;
    }

    constexpr auto get() const -> const Ty& {
        return value;
    }
};

// Config fields are override directives, not real option fields.
struct ConfigFields {
    ConfigOverrideField<bool> required = true;
    ConfigOverrideField<CategoryRef> category = default_category;
    ConfigOverrideField<std::string_view> help = "no provided";
    ConfigOverrideField<std::string_view> meta_var = "<value>";

    enum class Type : char {
        Start = 0,
        End = 1,
        Next = 2,  // just make sense to next
    };
    Type type;

    constexpr ConfigFields() = default;
};

struct NamedOptionFields : CommonOptionFields {
    std::vector<std::string_view> names;
    constexpr NamedOptionFields() = default;
};

struct InputFields : CommonOptionFields {
    constexpr static DecoType deco_field_ty = DecoType::Input;
    constexpr InputFields() = default;
};

struct PackFields : CommonOptionFields {
    constexpr static DecoType deco_field_ty = DecoType::TrailingInput;
    constexpr PackFields() = default;
};

struct FlagFields : NamedOptionFields {
    constexpr static DecoType deco_field_ty = DecoType::Flag;
    constexpr FlagFields() = default;
};

struct KVFields : NamedOptionFields {
    constexpr static DecoType deco_field_ty = DecoType::KV;
    char style = KVStyle::Separate;
    constexpr KVFields() = default;
};

struct CommaJoinedFields : NamedOptionFields {
    constexpr static DecoType deco_field_ty = DecoType::CommaJoined;
    constexpr CommaJoinedFields() = default;
};

struct MultiFields : NamedOptionFields {
    constexpr static DecoType deco_field_ty = DecoType::Multi;
    unsigned arg_num = 1;
    constexpr MultiFields() = default;
};

struct DecoOptionBase {
    constexpr virtual ~DecoOptionBase() = default;
    // return error message if parsing fails, otherwise return std::nullopt
    virtual std::optional<std::string> into(backend::ParsedArgument&& arg) = 0;
};

template <typename ResTy>
struct DecoOption : public DecoOptionBase, private std::optional<ResTy> {
    using result_type = ResTy;
    using BaseOpt = std::optional<ResTy>;

    using BaseOpt::operator*;
    using BaseOpt::operator->;
    using BaseOpt::has_value;
    using BaseOpt::value;
    using BaseOpt::value_or;
    using BaseOpt::reset;
    using BaseOpt::emplace;
    using BaseOpt::operator bool;

    constexpr DecoOption() = default;
    constexpr ~DecoOption() = default;

    constexpr DecoOption(ResTy default_value) : BaseOpt(std::move(default_value)) {}

    template <typename DefaultTy>
        requires (!std::same_as<std::remove_cvref_t<DefaultTy>, DecoOption<ResTy>> &&
                  std::constructible_from<ResTy, DefaultTy>)
    constexpr DecoOption(DefaultTy&& default_value) :
        BaseOpt(std::forward<DefaultTy>(default_value)) {}

    template <typename DefaultTy>
        requires (!std::same_as<std::remove_cvref_t<DefaultTy>, DecoOption<ResTy>> &&
                  std::constructible_from<ResTy, DefaultTy>)
    constexpr auto operator=(DefaultTy&& default_value) -> DecoOption<ResTy>& {
        BaseOpt::operator=(std::forward<DefaultTy>(default_value));
        return *this;
    }

    const BaseOpt& as_optional() const {
        return *this;
    }

    BaseOpt& as_optional() {
        return *this;
    }

    virtual std::optional<std::string> into(backend::ParsedArgument&& arg) override = 0;
};

namespace detail {

inline bool iequals_ascii(std::string_view lhs, std::string_view rhs) {
    if(lhs.size() != rhs.size()) {
        return false;
    }
    for(std::size_t i = 0; i < lhs.size(); ++i) {
        auto l = lhs[i];
        auto r = rhs[i];
        if(l >= 'A' && l <= 'Z') {
            l = static_cast<char>('a' + (l - 'A'));
        }
        if(r >= 'A' && r <= 'Z') {
            r = static_cast<char>('a' + (r - 'A'));
        }
        if(l != r) {
            return false;
        }
    }
    return true;
}

template <typename ErrorTy>
std::optional<std::string> normalize_into_error(const std::optional<ErrorTy>& err) {
    if(!err.has_value()) {
        return std::nullopt;
    }
    return std::string(std::string_view(*err));
}

template <typename ResTy>
std::optional<std::string> parse_primitive_scalar(ResTy& out, std::string_view text) {
    if constexpr(std::same_as<ResTy, bool>) {
        if(text == "1" || iequals_ascii(text, "true") || iequals_ascii(text, "yes") ||
           iequals_ascii(text, "on")) {
            out = true;
            return std::nullopt;
        }
        if(text == "0" || iequals_ascii(text, "false") || iequals_ascii(text, "no") ||
           iequals_ascii(text, "off")) {
            out = false;
            return std::nullopt;
        }
        return "invalid boolean value: " + std::string(text);
    } else if constexpr(std::integral<ResTy>) {
        ResTy parsed{};
        const auto* begin = text.data();
        const auto* end = text.data() + text.size();
        const auto [ptr, ec] = std::from_chars(begin, end, parsed);
        if(ec != std::errc() || ptr != end) {
            return "invalid integer value: " + std::string(text);
        }
        out = parsed;
        return std::nullopt;
    } else if constexpr(std::same_as<ResTy, long double>) {
        return "unsupported floating-point type: long double";
    } else if constexpr(std::floating_point<ResTy>) {
        std::string copy(text);
        char* parse_end = nullptr;
        errno = 0;
        ResTy parsed{};
        if constexpr(std::same_as<ResTy, float>) {
            parsed = std::strtof(copy.c_str(), &parse_end);
        } else {
            parsed = std::strtod(copy.c_str(), &parse_end);
        }
        if(parse_end != copy.c_str() + copy.size()) {
            return "invalid floating-point value: " + std::string(text);
        }
        if(errno == ERANGE) {
            return "floating-point value out of range: " + std::string(text);
        }
        out = parsed;
        return std::nullopt;
    } else if constexpr(trait::StringResultType<ResTy>) {
        out = ResTy(text);
        return std::nullopt;
    } else {
        static_assert(!sizeof(ResTy), "Unsupported scalar result type.");
        return "unsupported scalar result type";
    }
}

template <typename ResTy>
std::optional<std::string> assign_scalar(std::optional<ResTy>& target, std::string_view text) {
    if constexpr(trait::CustomStringResultTy<ResTy>) {
        auto& res = target.emplace();
        const auto err = res.into(text);
        return normalize_into_error(err);
    } else {
        ResTy parsed{};
        if(auto err = parse_primitive_scalar(parsed, text)) {
            return err;
        }
        target = std::move(parsed);
        return std::nullopt;
    }
}

template <typename ResTy>
std::optional<std::string> assign_vector(std::optional<ResTy>& target,
                                         std::span<const std::string_view> values) {
    if constexpr(trait::CustomStringVectorResultTy<ResTy>) {
        auto& res = target.emplace();
        std::vector<std::string_view> custom_values(values.begin(), values.end());
        const auto err = res.into(custom_values);
        return normalize_into_error(err);
    } else {
        using item_type = typename ResTy::value_type;
        ResTy parsed;
        parsed.clear();
        for(std::size_t i = 0; i < values.size(); ++i) {
            item_type item{};
            if(auto err = parse_primitive_scalar(item, values[i])) {
                return "invalid vector value at index " + std::to_string(i) + ": " + *err;
            }
            parsed.emplace_back(std::move(item));
        }
        target = std::move(parsed);
        return std::nullopt;
    }
}

template <typename ResTy>
std::optional<std::string> assign_input_vector(std::optional<ResTy>& target,
                                               std::vector<std::string>& raw_inputs,
                                               std::span<const std::string_view> values) {
    const auto original_size = raw_inputs.size();
    raw_inputs.reserve(original_size + values.size());
    for(const auto value: values) {
        raw_inputs.emplace_back(value);
    }

    std::vector<std::string_view> all_values;
    all_values.reserve(raw_inputs.size());
    for(const auto& value: raw_inputs) {
        all_values.emplace_back(value);
    }

    if(auto err = assign_vector(target, all_values)) {
        raw_inputs.resize(original_size);
        return err;
    }
    return std::nullopt;
}

}  // namespace detail

template <typename ResTy>
struct FlagOption : DecoOption<ResTy> {
    static_assert(trait::FlagResultType<ResTy>, "Flag result type must be bool or uint32_t.");
    using DecoOption<ResTy>::DecoOption;
    constexpr ~FlagOption() = default;

    std::optional<std::string> into(backend::ParsedArgument&& arg) override {
        if(!arg.values.empty()) {
            return "flag option does not accept values";
        }
        if constexpr(std::same_as<ResTy, bool>) {
            this->as_optional() = true;
        } else {
            this->as_optional() = this->as_optional().value_or(0) + 1;
        }
        return std::nullopt;
    }
};

template <typename ResTy>
struct ScalarOption : DecoOption<ResTy> {
    static_assert(trait::ScalarResultType<ResTy>, DecoScalarResultErrString);
    using DecoOption<ResTy>::DecoOption;
    constexpr ~ScalarOption() = default;

    std::optional<std::string> into(backend::ParsedArgument&& arg) override {
        if(arg.values.size() != 1) {
            return "expected exactly one value";
        }
        return detail::assign_scalar(this->as_optional(), arg.values.front());
    }
};

template <typename ResTy>
struct InputOption : DecoOption<ResTy> {
    static_assert(trait::InputResultType<ResTy>, DecoInputResultErrString);
    using DecoOption<ResTy>::DecoOption;
    std::vector<std::string> raw_inputs;
    constexpr ~InputOption() = default;

    std::optional<std::string> into(backend::ParsedArgument&& arg) override {
        if constexpr(trait::ScalarResultType<ResTy>) {
            if(arg.values.empty()) {
                return detail::assign_scalar(this->as_optional(), arg.get_spelling_view());
            }
            if(arg.values.size() == 1) {
                return detail::assign_scalar(this->as_optional(), arg.values.front());
            }
            return "input option expects at most one value";
        } else {
            if(arg.values.empty()) {
                const std::string_view spelling = arg.get_spelling_view();
                return detail::assign_input_vector(this->as_optional(),
                                                   this->raw_inputs,
                                                   {&spelling, 1});
            }
            return detail::assign_input_vector(this->as_optional(), this->raw_inputs, arg.values);
        }
    }
};

template <typename ResTy>
struct VectorOption : DecoOption<ResTy> {
    static_assert(trait::VectorResultType<ResTy>, DecoVectorResultErrString);
    using DecoOption<ResTy>::DecoOption;
    constexpr ~VectorOption() = default;

    std::optional<std::string> into(backend::ParsedArgument&& arg) override {
        return detail::assign_vector(this->as_optional(), arg.values);
    }
};

struct SubCommand {
    std::string_view name;
    std::string_view description;
    // if null, use name as subcommand
    std::optional<std::string_view> command;
};

}  // namespace deco::decl
