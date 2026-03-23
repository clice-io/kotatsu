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
#include <variant>
#include <vector>

#include "./backend.h"
#include "./decl.h"
#include "./descriptor.h"
#include "./text.h"
#include "eventide/common/functional.h"

namespace deco::util {

std::vector<std::string> argvify(int argc, const char* const* argv, unsigned skip_num = 1);

}  // namespace deco::util

namespace deco::cli {

template <typename Signature>
using runtime_callable_t = eventide::function<Signature>;

template <typename T>
struct Invocation {
    unsigned next_index = 0;
    T options{};
    std::set<const decl::Category*> matched_categories;
    std::span<std::string> original_argv{};
    std::span<std::string> active_argv{};
    std::shared_ptr<std::vector<std::string>> owned_active_argv{};
    std::vector<backend::ParsedArgumentOwning> parsed_arguments{};
    std::vector<std::string> command_path{};
    std::string_view command_overview{};
    void (*usage_writer)(std::ostream&, std::string_view, bool, const text::Renderer*) = nullptr;
    const text::Renderer* renderer_ptr = nullptr;

    auto next_cursor() const -> unsigned {
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

    auto trace() const -> std::span<const backend::ParsedArgumentOwning> {
        return parsed_arguments;
    }

    auto trace() -> std::span<backend::ParsedArgumentOwning> {
        return parsed_arguments;
    }

    auto matched(const decl::Category& category) const -> bool {
        return matched_categories.contains(&category);
    }

    auto renderer() const -> const text::Renderer& {
        return text::resolve_renderer(renderer_ptr);
    }

    auto into_context_at_cursor(unsigned index) const -> decl::IntoContext {
        const auto argv_view = std::span<const std::string>(argv().data(), argv().size());
        return decl::IntoContext::at_cursor(argv_view, index, renderer_ptr);
    }

    auto into_context(const backend::ParsedArgumentOwning& arg) const -> decl::IntoContext {
        const auto argv_view = std::span<const std::string>(argv().data(), argv().size());
        return decl::IntoContext::from_argument(argv_view, arg, renderer_ptr);
    }

    auto format_error(std::string_view reason) const -> std::string {
        return into_context_at_cursor(next_cursor()).format_error(reason);
    }

    auto usage(std::ostream& os, bool include_help = true) const -> void {
        if(usage_writer != nullptr) {
            usage_writer(os, command_overview, include_help, renderer_ptr);
        }
    }

    auto print_usage(bool include_help = true, std::ostream& os = std::cout) const -> void {
        usage(os, include_help);
    }
};

template <typename T>
using ParsedResult = Invocation<T>;

template <typename T>
constexpr inline bool always_false_v = false;

namespace detail {

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
auto make_usage_document(std::string_view command_overview) -> text::UsageDocument {
    text::UsageDocument document{
        .overview = std::string(command_overview),
    };
    std::vector<const decl::Category*> seen_categories;
    const auto& storage = ::deco::detail::build_storage<T>();
    storage.visit_fields(T{}, [&](const auto& field, const auto& cfg, std::string_view name, auto) {
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
            });
            group_index = document.groups.size() - 1;
        }

        auto& group = document.groups[group_index];
        group.entries.push_back(text::UsageEntry{
            .usage = desc::from_deco_option(field, false, name),
            .help = desc::detail::has_help_text(cfg.help) ? std::string(cfg.help) : std::string{},
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
                     const text::Renderer* renderer = nullptr) {
    os << text::render_usage(detail::make_usage_document<T>(command_overview),
                             include_help,
                             renderer);
}

template <typename T, typename FieldTy>
class AfterStep {
    using invocation_t = Invocation<T>;

    invocation_t* invocation_ptr = nullptr;
    const backend::ParsedArgumentOwning* parsed_arg = nullptr;
    unsigned next_cursor_index = 0;
    std::span<std::string> argv_span{};
    const FieldTy* parsed_value = nullptr;

public:
    AfterStep() = default;

    AfterStep(invocation_t& invocation,
              const backend::ParsedArgumentOwning& arg,
              unsigned next_cursor,
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

    auto arg() const -> const backend::ParsedArgumentOwning& {
        return *parsed_arg;
    }

    auto trace() const -> std::span<const backend::ParsedArgumentOwning> {
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

    auto arg_index() const -> unsigned {
        return arg().index;
    }

    auto cursor() const -> unsigned {
        return next_cursor_index;
    }

    auto next_cursor() const -> unsigned {
        return next_cursor_index;
    }

    auto renderer() const -> const text::Renderer& {
        return invocation().renderer();
    }

    auto into_context_at_cursor(unsigned index) const -> decl::IntoContext {
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

    auto seek(unsigned index) const -> decl::ParseControl {
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
    std::string_view name{};
    std::string_view command{};

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
    const auto& storage = ::deco::detail::build_storage<T>();
    std::string err = "";
    // check required options
    storage.visit_fields(options, [&](auto& field, const auto& cfg, std::string_view name, auto) {
        if(matched_categories.contains(cfg.category.ptr()) && cfg.required && !field.has_value()) {
            err = std::format("required option {} is missing",
                              desc::from_deco_option(cfg, false, name));
            return false;
        }
        return true;
    });
    if(!err.empty()) {
        return err;
    }
    // check category requirements
    const auto& c_map = storage.category_map();
    std::set<const decl::Category*> required_categories;
    // 0 is dummy
    for(std::size_t i = 1; i < c_map.size(); ++i) {
        const auto* category = c_map[i];
        if(category != nullptr && category->required) {
            required_categories.insert(category);
        }
    }
    if(storage.has_trailing_option()) {
        if(const auto* trailing = storage.trailing_category();
           trailing != nullptr && trailing->required) {
            required_categories.insert(trailing);
        }
    }
    for(const auto* category: required_categories) {
        if(!matched_categories.contains(category)) {
            err = std::format("required {} is missing", desc::detail::category_desc(*category));
            return err;
        }
    }
    // check category exclusiveness
    for(auto category: matched_categories) {
        if(category->exclusive && matched_categories.size() > 1) {
            err = std::format("options in {} are exclusive, but multiple categories are matched",
                              desc::detail::category_desc(*category));
            return err;
        }
    }
    return {};
}

namespace detail {

template <typename T, typename OnOption>
std::expected<Invocation<T>, ParseError>
    run_parse_session(std::span<std::string> argv,
                      OnOption&& on_option,
                      const text::Renderer* formatter = nullptr) {
    const auto& storage = ::deco::detail::build_storage<T>();
    backend::OptTable table = storage.make_opt_table();
    Invocation<T> res{};
    ParseError err;
    std::span<std::string> current_argv = argv;
    std::shared_ptr<std::vector<std::string>> current_owned_argv{};
    bool stopped_during_parse = false;
    res.original_argv = argv;
    res.active_argv = current_argv;
    res.owned_active_argv = current_owned_argv;

    while(true) {
        bool restart_requested = false;
        std::span<std::string> restart_argv{};
        std::shared_ptr<std::vector<std::string>> restart_owned_argv{};
        res.active_argv = current_argv;
        res.owned_active_argv = current_owned_argv;
        const auto argv_view =
            std::span<const std::string>(current_argv.data(), current_argv.size());

        table.parse_args(
            current_argv,
            [&](unsigned next_cursor, std::expected<backend::ParsedArgument, std::string> arg) {
                auto error_at_cursor = [&](std::string_view reason) {
                    return decl::IntoContext::at_cursor(argv_view, next_cursor, formatter)
                        .format_error(reason);
                };
                if(!arg.has_value()) {
                    err = {ParseError::Type::BackendParsing, error_at_cursor(arg.error())};
                    return false;
                }

                const auto arg_snapshot = backend::ParsedArgumentOwning::from_parsed_argument(*arg);
                auto error_at_argument = [&](std::string_view reason) {
                    return decl::IntoContext::from_argument(argv_view, arg_snapshot, formatter)
                        .format_error(reason);
                };
                if(storage.is_unknown_option_id(arg->option_id)) {
                    err = {ParseError::Type::BackendParsing,
                           error_at_argument(
                               std::format("unknown option '{}'", arg->get_spelling_view()))};
                    return false;
                }

                auto& raw_parg = *arg;
                void* opt_raw_ptr = nullptr;
                const decl::Category* category = nullptr;
                decl::ErasedParseCallback option_callback{};

                if(storage.is_input_argument(raw_parg)) {
                    if(!storage.has_input_option()) {
                        err = {ParseError::Type::DecoParsing,
                               error_at_argument(std::format("unexpected input argument {}",
                                                             raw_parg.get_spelling_view()))};
                        return false;
                    }
                } else if(storage.is_trailing_argument(raw_parg)) {
                    if(!storage.has_trailing_option()) {
                        err = {ParseError::Type::DecoParsing,
                               error_at_argument(std::format("unexpected trailing argument {}",
                                                             raw_parg.get_spelling_view()))};
                        return false;
                    }
                    opt_raw_ptr = storage.trailing_ptr_of(res.options);
                    category = storage.trailing_category();
                    option_callback = storage.trailing_callback();
                }

                opt_raw_ptr = opt_raw_ptr ? opt_raw_ptr
                                          : storage.field_ptr_of(raw_parg.option_id, res.options);
                category = category ? category : storage.category_of(raw_parg.option_id);
                if(!option_callback) {
                    option_callback = storage.callback_of(raw_parg.option_id);
                }

                auto* opt_accessor = static_cast<decl::DecoOptionBase*>(opt_raw_ptr);
                if(opt_accessor == nullptr) {
                    err = {ParseError::Type::Internal,
                           error_at_argument("no option accessor found for option id " +
                                             std::to_string(raw_parg.option_id.id()))};
                    return false;
                }
                if(auto parse_err = opt_accessor->into(
                       std::move(raw_parg),
                       decl::IntoContext::from_argument(argv_view, arg_snapshot, formatter))) {
                    err = {ParseError::Type::IntoError, std::move(*parse_err)};
                    return false;
                }

                if(category == nullptr) {
                    err = {ParseError::Type::Internal,
                           error_at_argument("no category found for option id " +
                                             std::to_string(raw_parg.option_id.id()))};
                    return false;
                }
                res.matched_categories.insert(category);
                res.next_index = next_cursor;
                res.parsed_arguments.push_back(arg_snapshot);

                auto apply_control = [&](const decl::ParseControl& control) {
                    switch(control.action) {
                        case decl::ParseControl::Action::Continue: return true;
                        case decl::ParseControl::Action::Stop:
                            stopped_during_parse = true;
                            return false;
                        case decl::ParseControl::Action::Restart:
                            restart_requested = true;
                            restart_argv = control.next_argv;
                            restart_owned_argv = control.owned_next_argv;
                            if(!restart_owned_argv && current_owned_argv &&
                               !current_owned_argv->empty()) {
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
                    return true;
                };

                if(option_callback &&
                   !apply_control(
                       option_callback(arg_snapshot, next_cursor, current_argv, *opt_accessor))) {
                    return false;
                }

                if(!apply_control(
                       on_option(res, *opt_accessor, arg_snapshot, next_cursor, current_argv))) {
                    return false;
                }

                return true;
            });

        if(stopped_during_parse || !err.message.empty()) {
            break;
        }
        if(!restart_requested) {
            break;
        }

        current_argv = restart_argv;
        current_owned_argv = std::move(restart_owned_argv);
        res.next_index = 0;
    }
    if(!err.message.empty()) {
        return std::unexpected(std::move(err));
    }
    if(stopped_during_parse) {
        return res;
    }

    storage.visit_fields(
        res.options,
        [&](auto& field, const auto& cfg, std::string_view name, auto) {
            if(res.matched_categories.contains(cfg.category.ptr()) && cfg.required &&
               !field.has_value()) {
                const auto active_argv =
                    std::span<const std::string>(res.active_argv.data(), res.active_argv.size());
                err = {ParseError::Type::DecoParsing,
                       decl::IntoContext::at_cursor(active_argv, res.next_index, formatter)
                           .format_error(std::format("required option {} is missing",
                                                     desc::from_deco_option(cfg, false, name)))};
                return false;
            }
            return true;
        });
    if(!err.message.empty()) {
        return std::unexpected(std::move(err));
    }

    const std::string check_err = check_valid(res.options, res.matched_categories);
    if(!check_err.empty()) {
        const auto active_argv =
            std::span<const std::string>(res.active_argv.data(), res.active_argv.size());
        err = {ParseError::Type::DecoParsing,
               decl::IntoContext::at_cursor(active_argv, res.next_index, formatter)
                   .format_error(check_err)};
        return std::unexpected(std::move(err));
    }
    return res;
}

}  // namespace detail

template <typename T, typename Fn>
    requires std::is_invocable_r_v<bool, Fn, const T&, decl::DecoOptionBase*>
std::expected<ParsedResult<T>, ParseError> parse_with_callback(std::span<std::string> argv,
                                                               Fn&& cont_fn) {
    return detail::run_parse_session<T>(
        argv,
        [fn = std::forward<Fn>(cont_fn)](Invocation<T>& res,
                                         decl::DecoOptionBase& accessor,
                                         const backend::ParsedArgumentOwning&,
                                         unsigned,
                                         std::span<std::string>) mutable -> decl::ParseControl {
            if(std::invoke(fn, std::as_const(res.options), &accessor)) {
                return decl::ParseControl::next();
            }
            return decl::ParseControl::stop();
        });
}

template <typename T>
std::expected<Invocation<T>, ParseError> invoke(std::span<std::string> argv,
                                                const text::Renderer& formatter) {
    return detail::run_parse_session<T>(
        argv,
        [](auto&, decl::DecoOptionBase&, const backend::ParsedArgumentOwning&, unsigned, auto) {
            return decl::ParseControl::next();
        },
        &formatter);
}

template <typename T>
std::expected<Invocation<T>, ParseError> invoke(std::span<std::string> argv) {
    return detail::run_parse_session<T>(
        argv,
        [](auto&, decl::DecoOptionBase&, const backend::ParsedArgumentOwning&, unsigned, auto) {
            return decl::ParseControl::next();
        });
}

template <typename T>
std::expected<Invocation<T>, ParseError> parse(std::span<std::string> argv,
                                               const text::Renderer& formatter) {
    return invoke<T>(argv, formatter);
}

template <typename T>
std::expected<Invocation<T>, ParseError> parse(std::span<std::string> argv) {
    return invoke<T>(argv);
}

template <typename T>
// Returns bare options without preserving Invocation lifetime state.
// Built-in deco parsing therefore only supports owning string result types.
// Custom into(...) implementations remain responsible for any borrowed storage they expose.
std::expected<T, ParseError> parse_only(std::span<std::string> argv,
                                        const text::Renderer& formatter) {
    auto res = parse<T>(argv, formatter);
    if(!res.has_value()) {
        return std::unexpected(std::move(res.error()));
    }
    return std::move(res->options);
}

template <typename T>
// Returns bare options without preserving Invocation lifetime state.
// Built-in deco parsing therefore only supports owning string result types.
// Custom into(...) implementations remain responsible for any borrowed storage they expose.
std::expected<T, ParseError> parse_only(std::span<std::string> argv) {
    auto res = parse<T>(argv);
    if(!res.has_value()) {
        return std::unexpected(std::move(res.error()));
    }
    return std::move(res->options);
}

template <typename T>
class Command {
    using invocation_t = Invocation<T>;
    using finalize_handler_t = runtime_callable_t<void(invocation_t&)>;
    using match_handler_t = runtime_callable_t<void(invocation_t&)>;
    using error_fn_t = runtime_callable_t<void(ParseError)>;
    using step_runner_t =
        runtime_callable_t<decl::ParseControl(invocation_t&,
                                              const backend::ParsedArgumentOwning&,
                                              unsigned,
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
                    static_assert(always_false_v<HandlerTy>,
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
                    handler(std::move(invocation.options));
                } else if constexpr(std::is_invocable_v<HandlerTy&, invocation_t>) {
                    handler(std::move(invocation));
                } else if constexpr(std::is_invocable_v<HandlerTy&, invocation_t&>) {
                    handler(invocation);
                } else if constexpr(std::is_invocable_v<HandlerTy&, const invocation_t&>) {
                    handler(invocation);
                } else {
                    static_assert(always_false_v<HandlerTy>,
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

    std::string commandOverview;
    std::string commandName;
    std::vector<AfterHook> afterHooks;
    std::vector<finalize_handler_t> finalizers;
    std::vector<CategoryMatch> categoryMatches;
    std::optional<match_handler_t> matchAllHandler;
    std::optional<text::Renderer> textRenderer;
    error_fn_t errorHandler = [](const ParseError& err) {
        std::println(stderr, "{}", err.message);
    };

    auto renderer_ptr() const -> const text::Renderer* {
        return textRenderer.has_value() ? &*textRenderer : nullptr;
    }

    auto bind_runtime(invocation_t& invocation) const -> void {
        invocation.command_overview = commandOverview;
        invocation.usage_writer = &write_usage_for<T>;
        invocation.renderer_ptr = renderer_ptr();
        if(!commandName.empty() && invocation.command_path.empty()) {
            invocation.command_path = {commandName};
        }
    }

public:
    explicit Command(std::string_view command_overview) :
        commandOverview(command_overview), commandName(default_command_name(command_overview)) {}

    template <auto... Members, typename Fn>
    auto& after(Fn&& fn) {
        static_assert(sizeof...(Members) > 0,
                      "Command::after requires at least one member pointer.");
        using OptionTy = std::remove_cvref_t<detail::member_path_result_t<T, Members...>>;
        static_assert(
            std::derived_from<OptionTy, decl::DecoOptionBase>,
            "Command::after only supports member pointer paths ending at a deco " "option member.");
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
                           const backend::ParsedArgumentOwning& arg,
                           unsigned cursor,
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
        afterHooks.push_back(std::move(hook));
        return *this;
    }

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
        for(auto& item: categoryMatches) {
            if(item.category == &category) {
                item.handler = adapt_match_handler(std::forward<Handler>(handler));
                return *this;
            }
        }
        categoryMatches.push_back(
            CategoryMatch{.category = &category,
                          .handler = adapt_match_handler(std::forward<Handler>(handler))});
        return *this;
    }

    template <typename Handler>
        requires (std::is_invocable_v<std::remove_cvref_t<Handler>&, T> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, invocation_t> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, invocation_t&> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, const invocation_t&>)
    auto& matchAll(Handler&& handler) {
        matchAllHandler = adapt_match_handler(std::forward<Handler>(handler));
        return *this;
    }

    auto& on_error(error_fn_t handler) {
        errorHandler = std::move(handler);
        return *this;
    }

    auto& on_error(std::ostream& os) {
        errorHandler = [&os](const ParseError& err) {
            os << err.message << "\n";
        };
        return *this;
    }

    auto& render_with(text::Renderer renderer) {
        textRenderer = std::move(renderer);
        return *this;
    }

    auto& text_style(text::TextStyle style) {
        textRenderer = text::CompatibleRenderer(std::move(style));
        return *this;
    }

    auto invoke(std::span<std::string> argv) -> std::expected<invocation_t, ParseError> {
        auto res = detail::run_parse_session<T>(
            argv,
            [this](invocation_t& invocation,
                   decl::DecoOptionBase& accessor,
                   const backend::ParsedArgumentOwning& arg,
                   unsigned cursor,
                   std::span<std::string> active_argv) {
                if(afterHooks.empty()) {
                    return decl::ParseControl::next();
                }
                bind_runtime(invocation);
                auto* accessor_ptr = &accessor;
                for(auto& hook: afterHooks) {
                    if(hook.matches != nullptr && hook.matches(invocation.options, accessor_ptr)) {
                        const auto control =
                            hook.handler(invocation, arg, cursor, active_argv, accessor);
                        if(control.action != decl::ParseControl::Action::Continue) {
                            return control;
                        }
                    }
                }
                return decl::ParseControl::next();
            },
            renderer_ptr());
        if(!res.has_value()) {
            return res;
        }

        bind_runtime(*res);
        for(auto& finalize: finalizers) {
            finalize(*res);
        }
        return res;
    }

    // Returns bare options without preserving Invocation lifetime state.
    // Built-in deco parsing therefore only supports owning string result types.
    // Custom into(...) implementations remain responsible for any borrowed storage they expose.
    auto parse_only(std::span<std::string> argv) -> std::expected<T, ParseError> {
        auto res = invoke(argv);
        if(!res.has_value()) {
            return std::unexpected(std::move(res.error()));
        }
        return std::move(res->options);
    }

    template <typename Os>
    auto usage(Os& os, bool include_help = true) const -> void {
        write_usage_for<T>(os, commandOverview, include_help, renderer_ptr());
    }

    auto execute(std::span<std::string> argv) -> void {
        auto res = invoke(argv);
        if(!res.has_value()) {
            errorHandler(std::move(res.error()));
            return;
        }

        for(auto& item: categoryMatches) {
            if(res->matched(*item.category)) {
                item.handler(*res);
                return;
            }
        }
        if(matchAllHandler.has_value()) {
            (*matchAllHandler)(*res);
        }
    }

    auto operator()(std::span<std::string> argv) -> void {
        execute(argv);
    }
};

template <typename T>
auto command(std::string_view command_overview) -> Command<T> {
    return Command<T>(command_overview);
}

class SubCommander {
    using match_t = SubCommandMatch;
    using handler_fn_t = runtime_callable_t<void(match_t)>;
    using error_fn_t = runtime_callable_t<void(SubCommandError)>;

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
                handler(match.args());
            } else if constexpr(std::is_invocable_v<HandlerTy&, match_t>) {
                handler(std::move(match));
            } else if constexpr(std::is_invocable_v<HandlerTy&, const match_t&>) {
                handler(match);
            } else {
                static_assert(
                    always_false_v<HandlerTy>,
                    "SubCommander handler must accept std::span<std::string> or " "SubCommandMatch.");
            }
        });
    }

    error_fn_t errorHandler = [](const SubCommandError& err) {
        std::println(stderr, "{}", err.message);
    };
    std::optional<handler_fn_t> defaultHandler;
    std::vector<SubCommandHandler> handlers;
    std::map<std::string, std::size_t, std::less<>> commandToHandler;

    std::string commandOverview;
    std::string overview;
    std::optional<text::Renderer> textRenderer;

    static auto command_of(const decl::SubCommand& subcommand) -> std::string;
    static auto display_name_of(const decl::SubCommand& subcommand, std::string_view command)
        -> std::string;

    auto renderer_ptr() const -> const text::Renderer* {
        return textRenderer.has_value() ? &*textRenderer : nullptr;
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
        return add(subcommand, [&command](const match_t& match) { command(match.args()); });
    }

    template <typename OptTy>
    auto& add(const decl::SubCommand& subcommand, Command<OptTy>&& command) {
        return add(subcommand, [command = std::move(command)](const match_t& match) mutable {
            command(match.args());
        });
    }

    auto add(handler_fn_t default_handler) -> SubCommander&;

    template <typename Handler>
        requires (!std::same_as<std::remove_cvref_t<Handler>, handler_fn_t> &&
                  (std::is_invocable_v<std::remove_cvref_t<Handler>&, std::span<std::string>> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, match_t> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, const match_t&>))
    auto& add(Handler&& default_handler) {
        return add(adapt_handler(std::forward<Handler>(default_handler)));
    }

    auto& render_with(text::Renderer renderer) {
        textRenderer = std::move(renderer);
        return *this;
    }

    auto& text_style(text::TextStyle style) {
        textRenderer = text::CompatibleRenderer(std::move(style));
        return *this;
    }

    auto when_err(error_fn_t error_handler) -> SubCommander&;
    auto when_err(std::ostream& os) -> SubCommander&;
    void usage(std::ostream& os) const;
    auto match(std::span<std::string> argv) const -> std::expected<match_t, SubCommandError>;
    void parse(std::span<std::string> argv);
    void operator()(std::span<std::string> argv);
};

};  // namespace deco::cli
