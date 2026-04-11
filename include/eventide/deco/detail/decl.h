#pragma once
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <concepts>
#include <cstdlib>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "text.h"
#include "trait.h"

#include "eventide/serde/serde/spelling.h"

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
    Separate = 1 << 1,

    // -o 1 or -o=1
    JoinedOrSeparate = Joined | Separate
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

struct ParseControl {
    enum class Action : char {
        Continue = 0,
        Stop = 1,
        Restart = 2,
    };

    Action action = Action::Continue;
    std::span<std::string> next_argv{};
    std::shared_ptr<std::vector<std::string>> owned_next_argv{};

    ParseControl() = default;

    ParseControl(Action action,
                 std::span<std::string> next_argv = {},
                 std::shared_ptr<std::vector<std::string>> owned_next_argv = {}) :
        action(action), next_argv(next_argv), owned_next_argv(std::move(owned_next_argv)) {}

    static auto next() -> ParseControl {
        return {};
    }

    static auto stop() -> ParseControl {
        return ParseControl(Action::Stop);
    }

    /// Restart parsing with a non-owning argv view.
    ///
    /// IMPORTANT: `next_argv` must outlive the parsing session (and any returned Invocation that
    /// exposes `argv()`).
    /// For callback-local rewritten argv, use the `std::vector<std::string>` overload.
    static auto restart(std::span<std::string> next_argv) -> ParseControl {
        return ParseControl(Action::Restart, next_argv);
    }

    /// Restart parsing with a new, owned argv snapshot.
    ///
    /// This overload is safe to use with callback-local containers, e.g.
    /// `std::vector<std::string> rewritten; return step.restart(std::move(rewritten));`.
    static auto restart(std::vector<std::string> next_argv) -> ParseControl {
        auto owned = std::make_shared<std::vector<std::string>>(std::move(next_argv));
        auto view = std::span<std::string>(owned->data(), owned->size());
        return ParseControl(Action::Restart, view, std::move(owned));
    }
};

template <typename ResTy>
struct ParseStep {
    const backend::ParsedArgumentOwning* parsed_arg = nullptr;
    unsigned next_cursor_index = 0;
    std::span<std::string> argv_span{};
    const ResTy* parsed_value = nullptr;

    constexpr ParseStep() = default;

    constexpr ParseStep(const backend::ParsedArgumentOwning& arg,
                        unsigned next_cursor,
                        std::span<std::string> argv,
                        const ResTy& value) :
        parsed_arg(&arg), next_cursor_index(next_cursor), argv_span(argv), parsed_value(&value) {}

    constexpr auto arg() const -> const backend::ParsedArgumentOwning& {
        return *parsed_arg;
    }

    constexpr auto next_cursor() const -> unsigned {
        return next_cursor_index;
    }

    constexpr auto argv() const -> std::span<std::string> {
        return argv_span;
    }

    constexpr auto value() const -> const ResTy& {
        return *parsed_value;
    }

    auto next() const -> ParseControl {
        return ParseControl::next();
    }

    auto stop() const -> ParseControl {
        return ParseControl::stop();
    }

    auto restart(std::span<std::string> next_argv) const -> ParseControl {
        return ParseControl::restart(next_argv);
    }

    auto restart(std::vector<std::string> next_argv) const -> ParseControl {
        return ParseControl::restart(std::move(next_argv));
    }
};

struct IntoContext {
    std::span<const std::string> argv_span{};
    unsigned highlight_begin_index = 0;
    unsigned highlight_end_index = 0;
    const cli::text::Renderer* renderer_ptr = nullptr;

    constexpr IntoContext() = default;

    constexpr IntoContext(std::span<const std::string> argv,
                          unsigned highlight_begin,
                          unsigned highlight_end,
                          const cli::text::Renderer* renderer = nullptr) :
        argv_span(argv), highlight_begin_index(highlight_begin), highlight_end_index(highlight_end),
        renderer_ptr(renderer) {}

    constexpr auto argv() const -> std::span<const std::string> {
        return argv_span;
    }

    constexpr auto highlight_begin() const -> unsigned {
        return highlight_begin_index;
    }

    constexpr auto highlight_end() const -> unsigned {
        return highlight_end_index;
    }

    constexpr auto renderer() const -> const cli::text::Renderer* {
        return renderer_ptr;
    }

    static auto at_cursor(std::span<const std::string> argv,
                          unsigned index,
                          const cli::text::Renderer* renderer = nullptr) -> IntoContext {
        const unsigned clamped = std::min<unsigned>(index, argv.size());
        return IntoContext(argv, clamped, clamped, renderer);
    }

    template <typename ArgTy>
    static auto from_argument(std::span<const std::string> argv,
                              const ArgTy& arg,
                              const cli::text::Renderer* renderer = nullptr) -> IntoContext {
        const unsigned begin = std::min<unsigned>(arg.index, argv.size());
        if(begin >= argv.size()) {
            return at_cursor(argv, begin, renderer);
        }

        unsigned end = begin + 1;
        unsigned cursor = begin + 1;
        for(const auto& value: arg.values) {
            if(cursor >= argv.size() || argv[cursor] != value) {
                break;
            }
            ++cursor;
            end = cursor;
        }
        return IntoContext(argv, begin, end, renderer);
    }

    template <typename ArgTy>
    static auto from_value(std::span<const std::string> argv,
                           const ArgTy& arg,
                           std::string_view value,
                           const cli::text::Renderer* renderer = nullptr) -> IntoContext {
        const unsigned begin = std::min<unsigned>(arg.index, argv.size());
        if(begin >= argv.size()) {
            return at_cursor(argv, begin, renderer);
        }

        if(argv[begin] == value) {
            return IntoContext(argv, begin, begin + 1, renderer);
        }

        unsigned cursor = begin + 1;
        for(const auto& item: arg.values) {
            if(cursor >= argv.size() || argv[cursor] != item) {
                break;
            }
            if(item == value) {
                return IntoContext(argv, cursor, cursor + 1, renderer);
            }
            ++cursor;
        }
        return from_argument(argv, arg, renderer);
    }

    auto format_error(std::string_view reason) const -> std::string {
        if(argv_span.empty()) {
            return cli::text::render_diagnostic(cli::text::diagnostic_message(std::string(reason)),
                                                renderer_ptr);
        }
        return cli::text::render_diagnostic(cli::text::diagnostic_at(argv_span,
                                                                     highlight_begin_index,
                                                                     highlight_end_index,
                                                                     std::string(reason)),
                                            renderer_ptr);
    }

    template <typename T>
    auto format_error(std::optional<T> reason) const -> std::optional<T> {
        if(reason) {
            return format_error(reason.value());
        }
        return std::nullopt;
    }
};

using AliasForwardResult = std::expected<std::vector<std::string>, std::string>;
using AliasForwardFn = AliasForwardResult (*)(const backend::ParsedArgumentOwning& arg);
using AliasForwardFnWithContext =
    AliasForwardResult (*)(const backend::ParsedArgumentOwning& arg, const IntoContext& context);

struct AliasForwardField {
    enum class Kind : char {
        None = 0,
        Static = 1,
        Dynamic = 2,
        DynamicWithContext = 3,
    };

    Kind kind = Kind::None;
    std::vector<std::string_view> static_tokens;
    AliasForwardFn dynamic = nullptr;
    AliasForwardFnWithContext dynamic_with_context = nullptr;

    constexpr AliasForwardField() = default;

    constexpr auto operator=(std::initializer_list<std::string_view> tokens)
        -> AliasForwardField& {
        kind = Kind::Static;
        static_tokens.assign(tokens.begin(), tokens.end());
        dynamic = nullptr;
        dynamic_with_context = nullptr;
        return *this;
    }

    constexpr auto operator=(std::vector<std::string_view> tokens) -> AliasForwardField& {
        kind = Kind::Static;
        static_tokens = std::move(tokens);
        dynamic = nullptr;
        dynamic_with_context = nullptr;
        return *this;
    }

    constexpr auto operator=(AliasForwardFn fn) -> AliasForwardField& {
        kind = fn ? Kind::Dynamic : Kind::None;
        static_tokens.clear();
        dynamic = fn;
        dynamic_with_context = nullptr;
        return *this;
    }

    constexpr auto operator=(AliasForwardFnWithContext fn) -> AliasForwardField& {
        kind = fn ? Kind::DynamicWithContext : Kind::None;
        static_tokens.clear();
        dynamic = nullptr;
        dynamic_with_context = fn;
        return *this;
    }

    constexpr explicit operator bool() const {
        return kind != Kind::None;
    }
};

constexpr inline bool is_alias_placeholder_name(std::string_view member_name) {
    return member_name.starts_with("__deco_alias_wrapper") ||
           (!member_name.empty() &&
            std::all_of(member_name.begin(), member_name.end(), [](char ch) { return ch == '_'; }));
}

struct DecoFields {
    // if true, it's required if its category occurs in options.
    bool required = true;
    // options in the same category (same Category object address) must be all set or all unset
    CategoryRef category = default_category;

    constexpr DecoFields() = default;
};

struct CommonOptionFields : DecoFields {
    std::string_view help = "not provided";
    std::string_view meta_var = "<value>";

    constexpr CommonOptionFields() = default;
};

template <typename ResTy>
struct OptionCallbackField {
    using Step = ParseStep<ResTy>;
    using ParseCallback = ParseControl (*)(const Step& step);

    struct Action {
        static auto next(const Step&) -> ParseControl {
            return ParseControl::next();
        }

        static auto stop(const Step&) -> ParseControl {
            return ParseControl::stop();
        }
    };

    ParseCallback after_parsed = nullptr;

    constexpr OptionCallbackField() = default;
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
    ConfigOverrideField<std::string_view> help = "not provided";
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

struct AliasFields : NamedOptionFields {
    AliasForwardField forward;
    constexpr AliasFields() = default;
};

struct FlagAliasFields : AliasFields {
    constexpr static DecoType deco_field_ty = DecoType::Flag;
    constexpr FlagAliasFields() = default;
};

struct KVAliasFields : AliasFields {
    constexpr static DecoType deco_field_ty = DecoType::KV;
    char style = KVStyle::Separate;
    constexpr KVAliasFields() = default;
};

struct CommaJoinedAliasFields : AliasFields {
    constexpr static DecoType deco_field_ty = DecoType::CommaJoined;
    constexpr CommaJoinedAliasFields() = default;
};

struct MultiAliasFields : AliasFields {
    constexpr static DecoType deco_field_ty = DecoType::Multi;
    unsigned arg_num = 1;
    constexpr MultiAliasFields() = default;
};

struct DecoOptionBase {
    constexpr virtual ~DecoOptionBase() = default;
    // return error message if parsing fails, otherwise return std::nullopt
    virtual std::optional<std::string> into(backend::ParsedArgument&& arg) = 0;

    virtual std::optional<std::string> into(backend::ParsedArgument&& arg,
                                            const IntoContext& context) {
        (void)context;
        return into(std::move(arg));
    }
};

struct ErasedParseCallback {
    constexpr static std::size_t storage_size = sizeof(void (*)());
    using storage_t = std::array<char, storage_size>;
    using invoker_t = ParseControl (*)(const storage_t& storage,
                                       const backend::ParsedArgumentOwning& arg,
                                       unsigned next_cursor,
                                       std::span<std::string> argv,
                                       const DecoOptionBase& option);

    storage_t storage{};
    invoker_t invoke = nullptr;

    constexpr explicit operator bool() const {
        return invoke != nullptr;
    }

    constexpr auto operator()(const backend::ParsedArgumentOwning& arg,
                              unsigned next_cursor,
                              std::span<std::string> argv,
                              const DecoOptionBase& option) const -> ParseControl {
        if(invoke == nullptr) {
            return ParseControl::next();
        }
        return invoke(storage, arg, next_cursor, argv, option);
    }
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

template <typename EnumTy>
std::string format_invalid_enum_value(std::string_view text) {
    std::string message = "invalid enum value: ";
    message += text;

    const auto& names = refl::reflection<EnumTy>::member_names;
    if(names.empty()) {
        return message;
    }

    message += " (supported: ";
    for(std::size_t i = 0; i < names.size(); ++i) {
        if(i != 0) {
            message += ", ";
        }
        message += names[i];
    }
    message += ")";
    return message;
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
    } else if constexpr(std::is_enum_v<ResTy>) {
        if(auto parsed = eventide::serde::spelling::map_string_to_enum<ResTy>(text)) {
            out = *parsed;
            return std::nullopt;
        }
        return format_invalid_enum_value<ResTy>(text);
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
        static_assert(trait::OwnedStringResultType<ResTy>,
                      "Default deco string parsing only supports owning result types. "
                      "Use std::string or provide a custom into(...) overload if you need "
                      "specialized storage.");
        out = ResTy(text);
        return std::nullopt;
    } else {
        static_assert(!sizeof(ResTy), "Unsupported scalar result type.");
        return "unsupported scalar result type";
    }
}

template <typename ResTy>
std::optional<std::string> assign_scalar(std::optional<ResTy>& target,
                                         std::string_view text,
                                         const IntoContext& context = {}) {
    if constexpr(trait::CustomStringResultTyWithContext<ResTy>) {
        auto& res = target.emplace();
        return res.into(text, context);
    } else if constexpr(trait::CustomStringResultTy<ResTy>) {
        auto& res = target.emplace();
        return res.into(text);
    } else {
        ResTy parsed{};
        if(auto err = parse_primitive_scalar(parsed, text)) {
            return context.format_error(*err);
        }
        target = std::move(parsed);
        return std::nullopt;
    }
}

template <typename ResTy>
std::optional<std::string> assign_vector(std::optional<ResTy>& target,
                                         std::span<const std::string_view> values,
                                         const IntoContext& context = {}) {
    if constexpr(trait::CustomStringVectorResultTyWithContext<ResTy>) {
        auto& res = target.emplace();
        std::vector<std::string_view> custom_values(values.begin(), values.end());
        return res.into(custom_values, context);
    } else if constexpr(trait::CustomStringVectorResultTy<ResTy>) {
        auto& res = target.emplace();
        std::vector<std::string_view> custom_values(values.begin(), values.end());
        return context.format_error(res.into(custom_values));
    } else {
        using item_type = typename ResTy::value_type;
        ResTy parsed;
        parsed.clear();
        for(std::size_t i = 0; i < values.size(); ++i) {
            item_type item{};
            if(auto err = parse_primitive_scalar(item, values[i])) {
                return context.format_error("invalid vector value at index " + std::to_string(i) +
                                            ": " + *err);
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
                                               std::span<const std::string_view> values,
                                               const IntoContext& context = {}) {
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

    if(auto err = assign_vector(target, all_values, context)) {
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
        return into(std::move(arg), {});
    }

    std::optional<std::string> into(backend::ParsedArgument&& arg,
                                    const IntoContext& context) override {
        if(!arg.values.empty()) {
            return context.format_error("flag option does not accept values");
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
        return into(std::move(arg), {});
    }

    std::optional<std::string> into(backend::ParsedArgument&& arg,
                                    const IntoContext& context) override {
        if(arg.values.size() != 1) {
            return context.format_error("expected exactly one value");
        }
        const auto value_context =
            IntoContext::from_value(context.argv(), arg, arg.values.front(), context.renderer());
        return detail::assign_scalar(this->as_optional(), arg.values.front(), value_context);
    }
};

template <typename ResTy>
struct InputOption : DecoOption<ResTy> {
    static_assert(trait::InputResultType<ResTy>, DecoInputResultErrString);
    using DecoOption<ResTy>::DecoOption;
    std::vector<std::string> raw_inputs;
    constexpr ~InputOption() = default;

    std::optional<std::string> into(backend::ParsedArgument&& arg) override {
        return into(std::move(arg), {});
    }

    std::optional<std::string> into(backend::ParsedArgument&& arg,
                                    const IntoContext& context) override {
        if constexpr(trait::ScalarResultType<ResTy>) {
            if(arg.values.empty()) {
                const auto value_context = IntoContext::from_value(context.argv(),
                                                                   arg,
                                                                   arg.get_spelling_view(),
                                                                   context.renderer());
                return detail::assign_scalar(this->as_optional(),
                                             arg.get_spelling_view(),
                                             value_context);
            }
            if(arg.values.size() == 1) {
                const auto value_context = IntoContext::from_value(context.argv(),
                                                                   arg,
                                                                   arg.values.front(),
                                                                   context.renderer());
                return detail::assign_scalar(this->as_optional(),
                                             arg.values.front(),
                                             value_context);
            }
            return context.format_error("input option expects at most one value");
        } else {
            if(arg.values.empty()) {
                const std::string_view spelling = arg.get_spelling_view();
                return detail::assign_input_vector(this->as_optional(),
                                                   this->raw_inputs,
                                                   {&spelling, 1},
                                                   context);
            }
            return detail::assign_input_vector(this->as_optional(),
                                               this->raw_inputs,
                                               arg.values,
                                               context);
        }
    }
};

template <typename ResTy>
struct VectorOption : DecoOption<ResTy> {
    static_assert(trait::VectorResultType<ResTy>, DecoVectorResultErrString);
    using DecoOption<ResTy>::DecoOption;
    constexpr ~VectorOption() = default;

    std::optional<std::string> into(backend::ParsedArgument&& arg) override {
        return into(std::move(arg), {});
    }

    std::optional<std::string> into(backend::ParsedArgument&& arg,
                                    const IntoContext& context) override {
        return detail::assign_vector(this->as_optional(), arg.values, context);
    }
};

struct SubCommand {
    std::string_view name;
    std::string_view description;
    // if null, use name as subcommand
    std::optional<std::string_view> command;
};

}  // namespace deco::decl
