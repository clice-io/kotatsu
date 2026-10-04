#pragma once

#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "backend.h"
#include "decl.h"
#include "descriptor.h"
#include "text.h"
#include "kota/support/functional.h"
#include "kota/support/type_traits.h"

namespace kota::deco::util {

std::vector<std::string> argvify(int argc, const char* const* argv, std::uint32_t skip_num = 1);

}  // namespace kota::deco::util

namespace kota::deco::cli {

template <typename Signature>
using runtime_callable_t = kota::function<Signature>;

template <typename T>
struct Invocation {
    std::uint32_t next_index = 0;
    T options{};
    std::set<const decl::Category*> matched_categories;
    std::span<std::string> original_argv{};
    std::span<std::string> active_argv{};
    std::shared_ptr<std::vector<std::string>> owned_active_argv{};
    std::vector<ParsedArgOwning> parsed_arguments{};
    std::vector<std::string> command_path{};
    std::string command_overview{};
    std::optional<config::Config> usage_config{};
    std::optional<text::Renderer> resolved_renderer{};
    void (*usage_writer)(std::ostream&,
                         std::string_view,
                         bool,
                         const config::Config*,
                         const text::Renderer*) = nullptr;
    const text::Renderer* renderer_ptr = nullptr;

    auto next_cursor() const -> std::uint32_t {
        return next_index;
    }

    auto argv() const -> std::span<std::string> {
        return active_argv;
    }

    auto remaining() const -> std::span<std::string> {
        if(next_index > active_argv.size()) {
            return {};
        }
        return active_argv.subspan(next_index);
    }

    auto trace() const -> std::span<const ParsedArgOwning> {
        return parsed_arguments;
    }

    auto trace() -> std::span<ParsedArgOwning> {
        return parsed_arguments;
    }

    auto matched(const decl::Category& category) const -> bool {
        return matched_categories.contains(&category);
    }

    /// The renderer the invocation renders with: the one its command resolved for it, which it
    /// keeps a copy of, so that it renders the same once moved or once the command is gone;
    /// else, while its command parses, the command's; else the default renderer.
    auto renderer() const -> const text::Renderer& {
        if(resolved_renderer.has_value()) {
            return *resolved_renderer;
        }
        return text::resolve_renderer(renderer_ptr);
    }

    auto into_context_at_cursor(std::uint32_t index) const -> decl::IntoContext {
        const auto argv_view = std::span<const std::string>(argv().data(), argv().size());
        return decl::IntoContext::at_cursor(argv_view, index, &renderer());
    }

    auto into_context(const ParsedArgOwning& arg) const -> decl::IntoContext {
        const auto argv_view = std::span<const std::string>(argv().data(), argv().size());
        return decl::IntoContext::from_argument(argv_view, arg, &renderer());
    }

    auto format_error(std::string_view reason) const -> std::string {
        return into_context_at_cursor(next_cursor()).format_error(reason);
    }

    auto usage(std::ostream& os, bool include_help = true) const -> void {
        if(usage_writer != nullptr) {
            usage_writer(os,
                         command_overview,
                         include_help,
                         usage_config ? &*usage_config : nullptr,
                         &renderer());
        }
    }

    auto print_usage(bool include_help = true, std::ostream& os = std::cout) const -> void {
        usage(os, include_help);
    }
};

template <typename T>
using ParsedResult = Invocation<T>;

namespace detail {

/// The argv a static alias forward rewrites `arg` into: its tokens, then the values of `arg`;
/// a comma alias joins the values to its last token instead, `--target,a,b`.
template <typename AliasMeta>
auto resolve_static_alias_forward(const AliasMeta& meta, const ParsedArgOwning& arg)
    -> decl::AliasForwardResult {
    std::vector<std::string> argv(meta.static_tokens.begin(), meta.static_tokens.end());
    if(meta.kind != decl::DecoType::CommaJoined) {
        argv.insert(argv.end(), arg.values.begin(), arg.values.end());
        return argv;
    }
    if(argv.empty()) {
        return std::unexpected(
            std::string("comma alias forward requires at least one target token"));
    }
    for(const auto& value: arg.values) {
        argv.back().push_back(',');
        argv.back() += value;
    }
    return argv;
}

template <typename AliasMeta>
auto resolve_alias_forward(const AliasMeta& meta,
                           const ParsedArgOwning& arg,
                           const decl::IntoContext& context) -> decl::AliasForwardResult {
    switch(meta.forward_kind) {
        case decl::AliasForwardField::Kind::Static: return resolve_static_alias_forward(meta, arg);
        case decl::AliasForwardField::Kind::Dynamic: return meta.dynamic(arg);
        case decl::AliasForwardField::Kind::DynamicWithContext:
            return meta.dynamic_with_context(arg, context);
        case decl::AliasForwardField::Kind::None: break;
    }
    std::unreachable();
}

template <typename Ptr>
struct member_object_pointer_traits;

template <typename Member, typename Class>
struct member_object_pointer_traits<Member Class::*> {
    using member_type = Member;
    using class_type = Class;
};

template <typename Current, auto Member>
struct member_path_step {
    using member_ptr_t = decltype(Member);
    static_assert(std::is_member_object_pointer_v<member_ptr_t>,
                  "Command::after only supports member object pointers.");

    using owner_type = typename member_object_pointer_traits<member_ptr_t>::class_type;
    static_assert(std::derived_from<std::remove_cvref_t<Current>, owner_type>,
                  "Command::after member pointer path must be contiguous.");

    using type = std::remove_cvref_t<decltype(std::declval<Current&>().*Member)>;
};

template <typename Current, auto... Members>
struct member_path_result;

template <typename Current, auto Member>
struct member_path_result<Current, Member> : member_path_step<Current, Member> {};

template <typename Current, auto Member, auto Next, auto... Rest>
struct member_path_result<Current, Member, Next, Rest...> :
    member_path_result<typename member_path_step<Current, Member>::type, Next, Rest...> {};

template <typename Current, auto... Members>
using member_path_result_t = typename member_path_result<Current, Members...>::type;

template <auto Member, auto... Rest, typename Obj>
constexpr decltype(auto) access_member_path(Obj&& obj) {
    if constexpr(sizeof...(Rest) == 0) {
        return (std::forward<Obj>(obj).*Member);
    } else {
        return access_member_path<Rest...>((std::forward<Obj>(obj).*Member));
    }
}

template <typename T>
auto make_usage_document(std::string_view command_overview,
                         const config::Config* usage_config = nullptr) -> text::UsageDocument {
    text::UsageDocument document{
        .overview = std::string(command_overview),
        .groups = {},
    };
    std::vector<const decl::Category*> seen_categories;
    const auto& generator = ::kota::deco::detail::generator_of<T>();
    generator.visit_fields(
        T{},
        [&](const auto& field, const auto& cfg, std::string_view name, auto) {
            const auto* category = cfg.category.ptr();
            std::size_t group_index = 0;
            bool found = false;
            for(std::size_t i = 0; i < seen_categories.size(); ++i) {
                if(seen_categories[i] == category) {
                    group_index = i;
                    found = true;
                    break;
                }
            }
            if(!found) {
                seen_categories.push_back(category);
                document.groups.push_back(text::UsageGroup{
                    .title = desc::detail::category_desc(*category),
                    .exclusive = category->exclusive,
                    .is_default = category == &decl::default_category,
                    .entries = {},
                });
                group_index = document.groups.size() - 1;
            }

            auto& group = document.groups[group_index];
            group.entries.push_back(text::UsageEntry{
                .usage = desc::from_deco_option(field, false, name, usage_config),
                .help =
                    desc::detail::has_help_text(cfg.help) ? std::string(cfg.help) : std::string{},
            });
            return true;
        });
    return document;
}

}  // namespace detail

template <typename T>
void write_usage_for(std::ostream& os,
                     std::string_view command_overview,
                     bool include_help = true,
                     const config::Config* usage_config = nullptr,
                     const text::Renderer* renderer = nullptr) {
    os << text::render_usage(detail::make_usage_document<T>(command_overview, usage_config),
                             include_help,
                             renderer);
}

template <typename T, typename FieldTy>
class AfterStep {
    using invocation_t = Invocation<T>;

    invocation_t* invocation_ptr = nullptr;
    const ParsedArgOwning* parsed_arg = nullptr;
    std::uint32_t next_cursor_index = 0;
    std::span<std::string> argv_span{};
    const FieldTy* parsed_value = nullptr;

public:
    AfterStep() = default;

    AfterStep(invocation_t& invocation,
              const ParsedArgOwning& arg,
              std::uint32_t next_cursor,
              std::span<std::string> argv,
              const FieldTy& value) :
        invocation_ptr(&invocation), parsed_arg(&arg), next_cursor_index(next_cursor),
        argv_span(argv), parsed_value(&value) {}

    auto invocation() -> invocation_t& {
        return *invocation_ptr;
    }

    auto invocation() const -> const invocation_t& {
        return *invocation_ptr;
    }

    auto options() -> T& {
        return invocation().options;
    }

    auto options() const -> const T& {
        return invocation().options;
    }

    auto arg() const -> const ParsedArgOwning& {
        return *parsed_arg;
    }

    auto trace() const -> std::span<const ParsedArgOwning> {
        return invocation().trace();
    }

    auto argv() const -> std::span<std::string> {
        return argv_span;
    }

    auto original_argv() const -> std::span<std::string> {
        return invocation().original_argv;
    }

    auto command_path() const -> std::span<const std::string> {
        return invocation().command_path;
    }

    auto value() const -> const FieldTy& {
        return *parsed_value;
    }

    auto arg_index() const -> std::uint32_t {
        return arg().index;
    }

    auto cursor() const -> std::uint32_t {
        return next_cursor_index;
    }

    auto next_cursor() const -> std::uint32_t {
        return next_cursor_index;
    }

    auto renderer() const -> const text::Renderer& {
        return invocation().renderer();
    }

    auto into_context_at_cursor(std::uint32_t index) const -> decl::IntoContext {
        return invocation().into_context_at_cursor(index);
    }

    auto into_context() const -> decl::IntoContext {
        return invocation().into_context(arg());
    }

    auto format_error(std::string_view reason) const -> std::string {
        return invocation().format_error(reason);
    }

    auto usage(std::ostream& os, bool include_help = true) const -> void {
        invocation().usage(os, include_help);
    }

    auto print_usage(bool include_help = true, std::ostream& os = std::cout) const -> void {
        invocation().print_usage(include_help, os);
    }

    auto next() const -> decl::ParseControl {
        return decl::ParseControl::next();
    }

    auto stop() const -> decl::ParseControl {
        return decl::ParseControl::stop();
    }

    auto seek(std::uint32_t index) const -> decl::ParseControl {
        if(index >= argv_span.size()) {
            return resume_from(std::span<std::string>{});
        }
        return resume_from(argv_span.subspan(index));
    }

    auto resume_from(std::span<std::string> next_argv) const -> decl::ParseControl {
        return decl::ParseControl::restart(next_argv);
    }

    auto resume_from(std::vector<std::string> next_argv) const -> decl::ParseControl {
        return decl::ParseControl::restart(std::move(next_argv));
    }
};

struct SubCommandMatch {
    enum class Kind : char {
        Default = 0,
        Command = 1,
    };

    Kind kind = Kind::Default;
    std::span<std::string> original_argv{};
    std::span<std::string> remaining_argv{};
    std::string_view token{};
    std::string name{};
    std::string command{};

    auto args() const -> std::span<std::string> {
        return remaining_argv;
    }

    auto is_command() const -> bool {
        return kind == Kind::Command;
    }

    auto is_default() const -> bool {
        return kind == Kind::Default;
    }
};

struct ParseError {
    enum class Type { Internal, BackendParsing, DecoParsing, IntoError };

    Type type;

    std::string message;
};

struct SubCommandError {
    enum class Type { Internal, MissingSubCommand, UnknownSubCommand };

    Type type;

    std::string message;
};

template <typename T>
std::string check_valid(const T& options,
                        const std::set<const decl::Category*>& matched_categories) {
    const auto& generator = ::kota::deco::detail::generator_of<T>();
    std::string err = "";
    // check required options
    generator.visit_fields(options, [&](auto& field, const auto& cfg, std::string_view name, auto) {
        using field_ty = std::remove_cvref_t<decltype(field)>;
        if constexpr(ty::deco_option_like<field_ty>) {
            if(matched_categories.contains(cfg.category.ptr()) && cfg.required &&
               !field.has_value()) {
                err = std::format("required option {} is missing",
                                  desc::from_deco_option(cfg, false, name));
                return false;
            }
        }
        return true;
    });
    if(!err.empty()) {
        return err;
    }
    // check category requirements; the options without one are the dummy, the unknown option
    // and an input option no DecoInput stands for
    std::set<const decl::Category*> required_categories;
    for(const auto* category: generator.category_map()) {
        if(category != nullptr && category->required) {
            required_categories.insert(category);
        }
    }
    if(const auto* trailing = generator.trailing_category();
       trailing != nullptr && trailing->required) {
        required_categories.insert(trailing);
    }
    for(const auto* category: required_categories) {
        if(!matched_categories.contains(category)) {
            return std::format("required {} is missing", desc::detail::category_desc(*category));
        }
    }
    // check category exclusiveness
    for(const auto* category: matched_categories) {
        if(category->exclusive && matched_categories.size() > 1) {
            return std::format("options in {} are exclusive, but multiple categories are matched",
                               desc::detail::category_desc(*category));
        }
    }
    return {};
}

namespace detail {

template <typename T, typename OnOption>
std::expected<Invocation<T>, ParseError>
    run_parse_session(std::span<std::string> argv,
                      OnOption&& on_option,
                      const text::Renderer* renderer = nullptr) {
    const auto& generator = ::kota::deco::detail::generator_of<T>();
    backend::OptTable table = generator.make_opt_table();
    backend::ParseOptions parse_options = generator.make_parse_options();
    Invocation<T> res{};
    std::optional<ParseError> err;
    std::span<std::string> current_argv = argv;
    std::shared_ptr<std::vector<std::string>> current_owned_argv{};
    bool stopped_during_parse = false;
    res.original_argv = argv;

    while(true) {
        bool restart_requested = false;
        std::span<std::string> restart_argv{};
        std::shared_ptr<std::vector<std::string>> restart_owned_argv{};
        res.active_argv = current_argv;
        res.owned_active_argv = current_owned_argv;
        const auto argv_view =
            std::span<const std::string>(current_argv.data(), current_argv.size());

        auto apply_control = [&](const decl::ParseControl& control) {
            switch(control.action) {
                case decl::ParseControl::Action::Continue: return true;
                case decl::ParseControl::Action::Stop: stopped_during_parse = true; return false;
                case decl::ParseControl::Action::Restart:
                    restart_requested = true;
                    restart_argv = control.next_argv;
                    restart_owned_argv = control.owned_next_argv;
                    // A view into the argv being parsed keeps it alive.
                    if(!restart_owned_argv && current_owned_argv && !current_owned_argv->empty()) {
                        std::less<std::string*> ptr_less;
                        auto* begin = current_owned_argv->data();
                        auto* end = begin + current_owned_argv->size();
                        auto* cursor = restart_argv.data();
                        if(cursor != nullptr && !ptr_less(cursor, begin) &&
                           !ptr_less(end, cursor)) {
                            restart_owned_argv = current_owned_argv;
                        }
                    }
                    return false;
            }
            std::unreachable();
        };

        for(auto& result: table.parse(current_argv, parse_options)) {
            if(!result.has_value()) {
                auto& parse_error = result.error();
                err = ParseError{
                    ParseError::Type::BackendParsing,
                    decl::IntoContext::at_cursor(argv_view, parse_error.index, renderer)
                        .format_error(parse_error.message),
                };
                break;
            }

            auto& raw_parg = *result;
            const std::uint32_t next_cursor = raw_parg.next_index;
            auto arg_snapshot = ParsedArgOwning::from(raw_parg);
            const auto into_context =
                decl::IntoContext::from_argument(argv_view, arg_snapshot, renderer);
            if(raw_parg.id == generator.unknown_option_id) {
                err = ParseError{
                    ParseError::Type::BackendParsing,
                    into_context.format_error(
                        std::format("unknown option '{}'", raw_parg.spelling)),
                };
                break;
            }

            void* field = nullptr;
            const decl::Category* category = nullptr;
            decl::ErasedParseCallback option_callback{};
            if(generator.is_trailing_argument(raw_parg)) {
                if(!generator.has_trailing_option()) {
                    err = ParseError{
                        ParseError::Type::DecoParsing,
                        into_context.format_error(
                            std::format("unexpected trailing argument {}", raw_parg.spelling)),
                    };
                    break;
                }
                field = generator.trailing_ptr_of(res.options);
                category = generator.trailing_category();
                option_callback = generator.trailing_callback();
            } else if(const auto* alias_meta = generator.alias_meta_of(raw_parg.id)) {
                auto resolved = resolve_alias_forward(*alias_meta, arg_snapshot, into_context);
                if(!resolved.has_value()) {
                    // A forward given the context has formatted its error itself.
                    const bool formatted = alias_meta->forward_kind ==
                                           decl::AliasForwardField::Kind::DynamicWithContext;
                    err = ParseError{
                        ParseError::Type::IntoError,
                        formatted ? std::move(resolved.error())
                                  : into_context.format_error(resolved.error()),
                    };
                    break;
                }

                // Parsing goes on from the forward, followed by the arguments after the alias.
                auto rewritten = std::move(*resolved);
                const auto suffix = current_argv.subspan(next_cursor);
                rewritten.insert(rewritten.end(), suffix.begin(), suffix.end());
                res.next_index = next_cursor;
                restart_requested = true;
                restart_owned_argv =
                    std::make_shared<std::vector<std::string>>(std::move(rewritten));
                restart_argv = *restart_owned_argv;
                break;
            } else {
                if(generator.is_input_argument(raw_parg) && !generator.has_input_option()) {
                    err = ParseError{
                        ParseError::Type::DecoParsing,
                        into_context.format_error(
                            std::format("unexpected input argument {}", raw_parg.spelling)),
                    };
                    break;
                }
                field = generator.field_ptr_of(raw_parg.id, res.options);
                category = generator.category_of(raw_parg.id);
                option_callback = generator.callback_of(raw_parg.id);
            }

            auto& option = *static_cast<decl::DecoOptionBase*>(field);
            if(auto parse_err = option.into(arg_snapshot, into_context)) {
                err = ParseError{ParseError::Type::IntoError, std::move(*parse_err)};
                break;
            }
            res.matched_categories.insert(category);
            res.next_index = next_cursor;
            res.parsed_arguments.push_back(std::move(arg_snapshot));

            if(option_callback && !apply_control(option_callback(res.parsed_arguments.back(),
                                                                 next_cursor,
                                                                 current_argv,
                                                                 option))) {
                break;
            }
            if(!apply_control(on_option(res,
                                        option,
                                        res.parsed_arguments.back(),
                                        next_cursor,
                                        current_argv))) {
                break;
            }
        }

        if(err.has_value() || stopped_during_parse || !restart_requested) {
            break;
        }
        current_argv = restart_argv;
        current_owned_argv = std::move(restart_owned_argv);
        res.next_index = 0;
    }
    if(err.has_value()) {
        return std::unexpected(std::move(*err));
    }
    if(stopped_during_parse) {
        return res;
    }

    if(auto check_err = check_valid(res.options, res.matched_categories); !check_err.empty()) {
        const auto active_argv =
            std::span<const std::string>(res.active_argv.data(), res.active_argv.size());
        return std::unexpected(ParseError{
            ParseError::Type::DecoParsing,
            decl::IntoContext::at_cursor(active_argv, res.next_index, renderer)
                .format_error(check_err),
        });
    }
    return res;
}

/// Calls `handler` with `args` and returns the exit code it gives: its int, or `otherwise`
/// when it returns nothing.
template <typename Handler, typename... Args>
int exit_code_of(int otherwise, Handler& handler, Args&&... args) {
    using result_t = std::invoke_result_t<Handler&, Args...>;
    if constexpr(std::is_void_v<result_t>) {
        std::invoke(handler, std::forward<Args>(args)...);
        return otherwise;
    } else {
        static_assert(std::same_as<result_t, int>,
                      "A handler returns nothing, or an int that is the exit code.");
        return std::invoke(handler, std::forward<Args>(args)...);
    }
}

/// Whether `options` hold a decl::HelpOption that was given.
template <typename T>
bool help_requested(const T& options) {
    bool requested = false;
    ::kota::deco::detail::generator_of<T>().visit_fields(
        options,
        [&](const auto& field, const auto&, std::string_view, auto) {
            if constexpr(std::same_as<std::remove_cvref_t<decltype(field)>, decl::HelpOption>) {
                requested = field.has_value();
            }
            return !requested;
        });
    return requested;
}

}  // namespace detail

/// What a Command or SubCommander returns when its argv does not parse, unless its error
/// handler returns another: the usual exit status of a usage error.
constexpr inline int parse_error_exit_code = 2;

template <typename T, typename Fn>
    requires std::is_invocable_r_v<bool, Fn, const T&, decl::DecoOptionBase*>
std::expected<ParsedResult<T>, ParseError> parse_with_callback(std::span<std::string> argv,
                                                               Fn&& cont_fn) {
    return detail::run_parse_session<T>(
        argv,
        [fn = std::forward<Fn>(cont_fn)](Invocation<T>& res,
                                         decl::DecoOptionBase& accessor,
                                         const ParsedArgOwning&,
                                         std::uint32_t,
                                         std::span<std::string>) mutable -> decl::ParseControl {
            if(std::invoke(fn, std::as_const(res.options), &accessor)) {
                return decl::ParseControl::next();
            }
            return decl::ParseControl::stop();
        });
}

template <typename T>
std::expected<Invocation<T>, ParseError> invoke(std::span<std::string> argv,
                                                const text::Renderer& renderer) {
    return detail::run_parse_session<T>(
        argv,
        [](auto&, decl::DecoOptionBase&, const ParsedArgOwning&, std::uint32_t, auto) {
            return decl::ParseControl::next();
        },
        &renderer);
}

template <typename T>
std::expected<Invocation<T>, ParseError> invoke(std::span<std::string> argv) {
    return detail::run_parse_session<T>(
        argv,
        [](auto&, decl::DecoOptionBase&, const ParsedArgOwning&, std::uint32_t, auto) {
            return decl::ParseControl::next();
        });
}

template <typename T>
std::expected<Invocation<T>, ParseError> parse(std::span<std::string> argv,
                                               const text::Renderer& renderer) {
    return invoke<T>(argv, renderer);
}

template <typename T>
std::expected<Invocation<T>, ParseError> parse(std::span<std::string> argv) {
    return invoke<T>(argv);
}

template <typename T>
class Command {
    using invocation_t = Invocation<T>;
    using finalize_handler_t = runtime_callable_t<void(invocation_t&)>;
    using match_handler_t = runtime_callable_t<int(invocation_t&)>;
    using error_fn_t = runtime_callable_t<int(ParseError)>;
    using step_runner_t = runtime_callable_t<decl::ParseControl(invocation_t&,
                                                                const ParsedArgOwning&,
                                                                std::uint32_t,
                                                                std::span<std::string>,
                                                                decl::DecoOptionBase&)>;

    struct CategoryMatch {
        const decl::Category* category = nullptr;
        match_handler_t handler;
    };

    struct AfterHook {
        bool (*matches)(T&, decl::DecoOptionBase*) = nullptr;
        step_runner_t handler;
    };

    template <typename Handler>
    static auto adapt_finalize_handler(Handler&& handler) -> finalize_handler_t {
        using HandlerTy = std::remove_cvref_t<Handler>;
        return finalize_handler_t(
            [handler = std::forward<Handler>(handler)](invocation_t& invocation) mutable {
                if constexpr(std::is_invocable_v<HandlerTy&, invocation_t&>) {
                    handler(invocation);
                } else if constexpr(std::is_invocable_v<HandlerTy&, const invocation_t&>) {
                    handler(invocation);
                } else {
                    static_assert(kota::dependent_false<HandlerTy>,
                                  "Command handler must accept Invocation<T>&.");
                }
            });
    }

    template <typename Handler>
    static auto adapt_match_handler(Handler&& handler) -> match_handler_t {
        using HandlerTy = std::remove_cvref_t<Handler>;
        return match_handler_t(
            [handler = std::forward<Handler>(handler)](invocation_t& invocation) mutable {
                if constexpr(std::is_invocable_v<HandlerTy&, T>) {
                    return detail::exit_code_of(0, handler, std::move(invocation.options));
                } else if constexpr(std::is_invocable_v<HandlerTy&, invocation_t>) {
                    return detail::exit_code_of(0, handler, std::move(invocation));
                } else if constexpr(std::is_invocable_v<HandlerTy&, invocation_t&>) {
                    return detail::exit_code_of(0, handler, invocation);
                } else if constexpr(std::is_invocable_v<HandlerTy&, const invocation_t&>) {
                    return detail::exit_code_of(0, handler, invocation);
                } else {
                    static_assert(kota::dependent_false<HandlerTy>,
                                  "Command match handler must accept T or Invocation<T>.");
                }
            });
    }

    static auto default_command_name(std::string_view overview) -> std::string {
        const auto pos = overview.find_first_of(" \t");
        if(pos == std::string_view::npos) {
            return std::string(overview);
        }
        return std::string(overview.substr(0, pos));
    }

    std::string command_overview;
    std::string command_name;
    std::vector<AfterHook> after_hooks;
    std::vector<finalize_handler_t> finalizers;
    std::vector<CategoryMatch> category_matches;
    std::optional<match_handler_t> match_all_handler;
    std::optional<text::Renderer> text_renderer;
    config::ConfigOverride config_override{};
    error_fn_t error_handler = [](const ParseError& err) {
        std::println(stderr, "{}", err.message);
        return parse_error_exit_code;
    };

    auto resolved_config() const -> config::Config {
        return config::merge(config::get(), config_override);
    }

    /// The renderer the command renders with: its own, else the one its config makes, unless
    /// a default renderer is set, which rendering then looks up.
    auto active_renderer() const -> std::optional<text::Renderer> {
        if(text_renderer.has_value()) {
            return text_renderer;
        }
        if(text::explicit_default_renderer() != nullptr) {
            return std::nullopt;
        }
        return text::CompatibleRenderer(resolved_config().render.compatible);
    }

    auto bind_runtime(invocation_t& invocation, const text::Renderer* renderer) const -> void {
        invocation.command_overview = command_overview;
        invocation.usage_config = resolved_config();
        invocation.usage_writer = &write_usage_for<T>;
        invocation.renderer_ptr = renderer;
        if(!command_name.empty() && invocation.command_path.empty()) {
            invocation.command_path = {command_name};
        }
    }

public:
    explicit Command(std::string_view command_overview) :
        command_overview(command_overview), command_name(default_command_name(command_overview)) {}

    template <auto... Members, typename Fn>
    auto& after(Fn&& fn) {
        static_assert(sizeof...(Members) > 0,
                      "Command::after requires at least one member pointer.");
        using OptionTy = std::remove_cvref_t<detail::member_path_result_t<T, Members...>>;
        static_assert(std::derived_from<OptionTy, decl::DecoOptionBase>,
                      "Command::after only supports member pointer paths ending at a deco "
                      "option member.");
        using ValueTy = typename OptionTy::result_type;
        using FnTy = std::remove_cvref_t<Fn>;
        if constexpr(!std::is_invocable_r_v<decl::ParseControl, FnTy&, AfterStep<T, ValueTy>&>) {
            static_assert(
                std::is_invocable_r_v<decl::ParseControl, FnTy&, const AfterStep<T, ValueTy>&>,
                "Command::after callback must return ParseControl and accept AfterStep.");
        }

        AfterHook hook{
            .matches =
                [](T& options, decl::DecoOptionBase* accessor) {
                    return static_cast<decl::DecoOptionBase*>(
                               &(detail::access_member_path<Members...>(options))) == accessor;
                },
            .handler = [fn = std::forward<Fn>(fn)](
                           invocation_t& invocation,
                           const ParsedArgOwning& arg,
                           std::uint32_t cursor,
                           std::span<std::string> argv,
                           decl::DecoOptionBase& accessor) mutable -> decl::ParseControl {
                auto& typed_option = static_cast<OptionTy&>(accessor);
                AfterStep<T, ValueTy> step(invocation, arg, cursor, argv, typed_option.value());
                if constexpr(std::is_invocable_r_v<decl::ParseControl,
                                                   FnTy&,
                                                   AfterStep<T, ValueTy>&>) {
                    return fn(step);
                } else {
                    return fn(std::as_const(step));
                }
            },
        };
        after_hooks.push_back(std::move(hook));
        return *this;
    }

    /// Runs `handler` on the invocation of every argv that parses, unless it gives the help
    /// option.
    template <typename Handler>
        requires (std::is_invocable_v<std::remove_cvref_t<Handler>&, invocation_t&> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, const invocation_t&>)
    auto& finalize(Handler&& handler) {
        finalizers.push_back(adapt_finalize_handler(std::forward<Handler>(handler)));
        return *this;
    }

    template <typename Handler>
        requires (std::is_invocable_v<std::remove_cvref_t<Handler>&, T> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, invocation_t> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, invocation_t&> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, const invocation_t&>)
    auto& match(const decl::Category& category, Handler&& handler) {
        for(auto& item: category_matches) {
            if(item.category == &category) {
                item.handler = adapt_match_handler(std::forward<Handler>(handler));
                return *this;
            }
        }
        category_matches.push_back(
            CategoryMatch{.category = &category,
                          .handler = adapt_match_handler(std::forward<Handler>(handler))});
        return *this;
    }

    template <typename Handler>
        requires (std::is_invocable_v<std::remove_cvref_t<Handler>&, T> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, invocation_t> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, invocation_t&> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, const invocation_t&>)
    auto& match_all(Handler&& handler) {
        match_all_handler = adapt_match_handler(std::forward<Handler>(handler));
        return *this;
    }

    /// Hands an argv that does not parse to `handler`, which returns nothing, so that the
    /// command returns parse_error_exit_code, or the exit code to return.
    template <typename Handler>
        requires std::is_invocable_v<std::remove_cvref_t<Handler>&, ParseError>
    auto& on_error(Handler&& handler) {
        error_handler = [handler = std::forward<Handler>(handler)](ParseError err) mutable {
            return detail::exit_code_of(parse_error_exit_code, handler, std::move(err));
        };
        return *this;
    }

    auto& on_error(std::ostream& os) {
        return on_error([&os](const ParseError& err) { os << err.message << "\n"; });
    }

    auto& render_with(text::Renderer renderer) {
        text_renderer = std::move(renderer);
        return *this;
    }

    auto& config(config::ConfigOverride override_config) {
        config_override = std::move(override_config);
        return *this;
    }

    auto& render_with_compatible(text::CompatibleRendererConfig config = {}) {
        return render_with(text::CompatibleRenderer(std::move(config)));
    }

    auto& render_with_modern(text::ModernRendererConfig config = {}) {
        return render_with(text::ModernRenderer(std::move(config)));
    }

    auto invoke(std::span<std::string> argv) -> std::expected<invocation_t, ParseError> {
        std::optional<text::Renderer> renderer = active_renderer();
        const text::Renderer* renderer_ptr = renderer.has_value() ? &*renderer : nullptr;

        auto res = detail::run_parse_session<T>(
            argv,
            [this, renderer_ptr](invocation_t& invocation,
                                 decl::DecoOptionBase& accessor,
                                 const ParsedArgOwning& arg,
                                 std::uint32_t cursor,
                                 std::span<std::string> active_argv) {
                if(after_hooks.empty()) {
                    return decl::ParseControl::next();
                }
                bind_runtime(invocation, renderer_ptr);
                for(auto& hook: after_hooks) {
                    if(hook.matches(invocation.options, &accessor)) {
                        const auto control =
                            hook.handler(invocation, arg, cursor, active_argv, accessor);
                        if(control.action != decl::ParseControl::Action::Continue) {
                            return control;
                        }
                    }
                }
                return decl::ParseControl::next();
            },
            renderer_ptr);
        if(!res.has_value()) {
            return res;
        }

        res->resolved_renderer = std::move(renderer);
        bind_runtime(*res, nullptr);
        // The help option stopped the parse before the options were checked.
        if(!detail::help_requested(res->options)) {
            for(auto& finalize: finalizers) {
                finalize(*res);
            }
        }
        return res;
    }

    template <typename Os>
    auto usage(Os& os, bool include_help = true) const -> void {
        const auto usage_config = resolved_config();
        const auto renderer = active_renderer();
        write_usage_for<T>(os,
                           command_overview,
                           include_help,
                           &usage_config,
                           renderer.has_value() ? &*renderer : nullptr);
    }

    /// Parses `argv` and runs the handler of the first category it matched, else the
    /// match_all handler, and returns what the handler returns: 0 for one that returns
    /// nothing, or when no handler runs. A decl::HelpOption given prints the usage to stdout
    /// and returns 0, running no handler; an argv that does not parse goes to the error
    /// handler, and returns what it returns.
    auto execute(std::span<std::string> argv) -> int {
        auto res = invoke(argv);
        if(!res.has_value()) {
            return error_handler(std::move(res.error()));
        }
        if(detail::help_requested(res->options)) {
            res->print_usage();
            return 0;
        }

        for(auto& item: category_matches) {
            if(res->matched(*item.category)) {
                return item.handler(*res);
            }
        }
        if(match_all_handler.has_value()) {
            return (*match_all_handler)(*res);
        }
        return 0;
    }

    auto operator()(std::span<std::string> argv) -> int {
        return execute(argv);
    }
};

template <typename T>
auto command(std::string_view command_overview) -> Command<T> {
    return Command<T>(command_overview);
}

class SubCommander {
    using match_t = SubCommandMatch;
    using handler_fn_t = runtime_callable_t<int(match_t)>;
    using error_fn_t = runtime_callable_t<int(SubCommandError)>;

    struct SubCommandHandler {
        std::string name;
        std::string description;
        std::string command;
        handler_fn_t handler;
    };

    template <typename Handler>
    static auto adapt_handler(Handler&& handler) -> handler_fn_t {
        using HandlerTy = std::remove_cvref_t<Handler>;
        return handler_fn_t([handler = std::forward<Handler>(handler)](match_t match) mutable {
            if constexpr(std::is_invocable_v<HandlerTy&, std::span<std::string>>) {
                return detail::exit_code_of(0, handler, match.args());
            } else if constexpr(std::is_invocable_v<HandlerTy&, match_t>) {
                return detail::exit_code_of(0, handler, std::move(match));
            } else if constexpr(std::is_invocable_v<HandlerTy&, const match_t&>) {
                return detail::exit_code_of(0, handler, match);
            } else {
                static_assert(kota::dependent_false<HandlerTy>,
                              "SubCommander handler must accept std::span<std::string> or "
                              "SubCommandMatch.");
            }
        });
    }

    error_fn_t error_handler = [](const SubCommandError& err) {
        std::println(stderr, "{}", err.message);
        return parse_error_exit_code;
    };
    std::optional<handler_fn_t> default_handler;
    bool help_enabled = false;
    std::vector<SubCommandHandler> handlers;
    std::map<std::string, std::size_t, std::less<>> command_to_handler;

    std::string command_overview;
    std::string overview;
    std::optional<text::Renderer> text_renderer;
    config::ConfigOverride config_override{};

    static auto command_of(const decl::SubCommand& subcommand) -> std::string;
    static auto display_name_of(const decl::SubCommand& subcommand, std::string_view command)
        -> std::string;

    auto renderer_ptr() const -> const text::Renderer* {
        return text_renderer.has_value() ? &*text_renderer : nullptr;
    }

    auto resolved_config() const -> config::Config {
        return config::merge(config::get(), config_override);
    }

public:
    SubCommander(std::string_view command_overview, std::string_view overview = {});

    auto add(const decl::SubCommand& subcommand, handler_fn_t handler) -> SubCommander&;

    template <typename Handler>
        requires (!std::same_as<std::remove_cvref_t<Handler>, handler_fn_t> &&
                  (std::is_invocable_v<std::remove_cvref_t<Handler>&, std::span<std::string>> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, match_t> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, const match_t&>))
    auto& add(const decl::SubCommand& subcommand, Handler&& handler) {
        return add(subcommand, adapt_handler(std::forward<Handler>(handler)));
    }

    template <typename OptTy>
    auto& add(const decl::SubCommand& subcommand, Command<OptTy>& command) {
        return add(subcommand, [&command](const match_t& match) { return command(match.args()); });
    }

    template <typename OptTy>
    auto& add(const decl::SubCommand& subcommand, Command<OptTy>&& command) {
        return add(subcommand, [command = std::move(command)](const match_t& match) mutable {
            return command(match.args());
        });
    }

    auto add(handler_fn_t handler) -> SubCommander&;

    template <typename Handler>
        requires (!std::same_as<std::remove_cvref_t<Handler>, handler_fn_t> &&
                  (std::is_invocable_v<std::remove_cvref_t<Handler>&, std::span<std::string>> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, match_t> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, const match_t&>))
    auto& add(Handler&& handler) {
        return add(adapt_handler(std::forward<Handler>(handler)));
    }

    auto& render_with(text::Renderer renderer) {
        text_renderer = std::move(renderer);
        return *this;
    }

    auto& render_with_compatible(text::CompatibleRendererConfig config = {}) {
        return render_with(text::CompatibleRenderer(std::move(config)));
    }

    auto& render_with_modern(text::ModernRendererConfig config = {}) {
        return render_with(text::ModernRenderer(std::move(config)));
    }

    auto& config(config::ConfigOverride override_config) {
        config_override = std::move(override_config);
        return *this;
    }

    /// Hands a missing or unknown subcommand to `handler`, which returns nothing, so that the
    /// commander returns parse_error_exit_code, or the exit code to return. It is handed too
    /// the Internal error of an add() given a subcommand with no name; what it returns for
    /// that is dropped, as add() has no exit code to give.
    template <typename Handler>
        requires std::is_invocable_v<std::remove_cvref_t<Handler>&, SubCommandError>
    auto when_err(Handler&& handler) -> SubCommander& {
        error_handler = [handler = std::forward<Handler>(handler)](SubCommandError err) mutable {
            return detail::exit_code_of(parse_error_exit_code, handler, std::move(err));
        };
        return *this;
    }

    auto when_err(std::ostream& os) -> SubCommander&;

    /// Answers `-h` or `--help` as the first argument by printing the subcommands to stdout
    /// and returning 0.
    auto enable_help() -> SubCommander&;

    void usage(std::ostream& os) const;
    auto match(std::span<std::string> argv) const -> std::expected<match_t, SubCommandError>;

    /// Runs the handler of the subcommand `argv` starts with, else the default handler, and
    /// returns what it returns, 0 for one that returns nothing. A missing or unknown
    /// subcommand goes to the error handler, and returns what it returns.
    auto parse(std::span<std::string> argv) -> int;
    auto operator()(std::span<std::string> argv) -> int;
};

};  // namespace kota::deco::cli
