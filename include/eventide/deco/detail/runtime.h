#pragma once

#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <map>
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

namespace deco::util {

std::vector<std::string> argvify(int argc, const char* const* argv, unsigned skip_num = 1);

}  // namespace deco::util

namespace deco::cli {

template <typename Signature>
using runtime_callable_t =
#if defined(__APPLE__)
    std::function<Signature>;
#else
    std::move_only_function<Signature>;
#endif

template <typename T, typename State = std::monostate>
struct Invocation {
    unsigned next_index = 0;
    T options{};
    State state{};
    std::set<const decl::Category*> matched_categories;
    std::span<std::string> original_argv{};
    std::span<std::string> active_argv{};
    std::vector<backend::ParsedArgumentOwning> parsed_arguments{};
    std::vector<std::string> command_path{};

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

template <typename T, typename State = std::monostate>
class Context {
    using invocation_t = Invocation<T, State>;

    invocation_t* invocation_ptr = nullptr;
    std::string_view command_overview{};
    void (*usage_writer)(std::ostream&, std::string_view, bool, const text::Renderer*) = nullptr;
    const text::Renderer* renderer_ptr = nullptr;

public:
    Context() = default;

    Context(invocation_t& invocation,
            std::string_view command_overview,
            void (*usage_writer)(std::ostream&, std::string_view, bool, const text::Renderer*),
            const text::Renderer* renderer = nullptr) :
        invocation_ptr(&invocation), command_overview(command_overview), usage_writer(usage_writer),
        renderer_ptr(renderer) {}

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

    auto state() -> State& {
        return invocation().state;
    }

    auto state() const -> const State& {
        return invocation().state;
    }

    auto argv() const -> std::span<std::string> {
        return invocation().argv();
    }

    auto original_argv() const -> std::span<std::string> {
        return invocation().original_argv;
    }

    auto remaining() const -> std::span<std::string> {
        return invocation().remaining();
    }

    auto trace() const -> std::span<const backend::ParsedArgumentOwning> {
        return invocation().trace();
    }

    auto matched(const decl::Category& category) const -> bool {
        return invocation().matched(category);
    }

    auto next_cursor() const -> unsigned {
        return invocation().next_cursor();
    }

    auto command_path() const -> std::span<const std::string> {
        return invocation().command_path;
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

template <typename T, typename FieldTy, typename State = std::monostate>
class ParseStep {
    Context<T, State>* context_ptr = nullptr;
    const backend::ParsedArgumentOwning* parsed_arg = nullptr;
    unsigned next_cursor_index = 0;
    std::span<std::string> argv_span{};
    const FieldTy* parsed_value = nullptr;

public:
    ParseStep() = default;

    ParseStep(Context<T, State>& context,
              const backend::ParsedArgumentOwning& arg,
              unsigned next_cursor,
              std::span<std::string> argv,
              const FieldTy& value) :
        context_ptr(&context), parsed_arg(&arg), next_cursor_index(next_cursor), argv_span(argv),
        parsed_value(&value) {}

    auto context() -> Context<T, State>& {
        return *context_ptr;
    }

    auto context() const -> const Context<T, State>& {
        return *context_ptr;
    }

    auto options() -> T& {
        return context().options();
    }

    auto options() const -> T& {
        return context_ptr->options();
    }

    auto state() -> State& {
        return context().state();
    }

    auto state() const -> State& {
        return context_ptr->state();
    }

    auto arg() const -> const backend::ParsedArgumentOwning& {
        return *parsed_arg;
    }

    auto trace() const -> std::span<const backend::ParsedArgumentOwning> {
        return context().trace();
    }

    auto argv() const -> std::span<std::string> {
        return argv_span;
    }

    auto original_argv() const -> std::span<std::string> {
        return context().original_argv();
    }

    auto command_path() const -> std::span<const std::string> {
        return context().command_path();
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

    auto next() const -> decl::ParseControl {
        return decl::ParseControl::next();
    }

    auto stop() const -> decl::ParseControl {
        return decl::ParseControl::stop();
    }

    auto seek(unsigned index) const -> decl::ParseControl {
        if(index >= argv_span.size()) {
            return resume_from({});
        }
        return resume_from(argv_span.subspan(index));
    }

    auto resume_from(std::span<std::string> next_argv) const -> decl::ParseControl {
        return decl::ParseControl::restart(next_argv);
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

template <typename T, typename State, typename OnOption>
std::expected<Invocation<T, State>, ParseError>
    run_parse_session(std::span<std::string> argv,
                      State initial_state,
                      OnOption&& on_option,
                      const text::Renderer* formatter = nullptr) {
    const auto& storage = ::deco::detail::build_storage<T>();
    backend::OptTable table = storage.make_opt_table();
    Invocation<T, State> res{};
    ParseError err;
    std::span<std::string> current_argv = argv;
    bool stopped_during_parse = false;
    res.original_argv = argv;
    res.active_argv = current_argv;
    res.state = std::move(initial_state);

    while(true) {
        bool restart_requested = false;
        std::span<std::string> restart_argv{};
        res.active_argv = current_argv;
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
    return detail::run_parse_session<T, std::monostate>(
        argv,
        std::monostate{},
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
    return detail::run_parse_session<T, std::monostate>(
        argv,
        std::monostate{},
        [](auto&, decl::DecoOptionBase&, const backend::ParsedArgumentOwning&, unsigned, auto) {
            return decl::ParseControl::next();
        },
        &formatter);
}

template <typename T>
std::expected<Invocation<T>, ParseError> invoke(std::span<std::string> argv) {
    return detail::run_parse_session<T, std::monostate>(
        argv,
        std::monostate{},
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
std::expected<T, ParseError> parse_only(std::span<std::string> argv,
                                        const text::Renderer& formatter) {
    auto res = parse<T>(argv, formatter);
    if(!res.has_value()) {
        return std::unexpected(std::move(res.error()));
    }
    return std::move(res->options);
}

template <typename T>
std::expected<T, ParseError> parse_only(std::span<std::string> argv) {
    auto res = parse<T>(argv);
    if(!res.has_value()) {
        return std::unexpected(std::move(res.error()));
    }
    return std::move(res->options);
}

template <typename T, typename State = std::monostate>
class Command {
    using invocation_t = Invocation<T, State>;
    using context_t = Context<T, State>;
    using context_handler_t = runtime_callable_t<void(context_t&)>;
    using error_fn_t = runtime_callable_t<void(ParseError)>;
    using step_runner_t =
        runtime_callable_t<decl::ParseControl(context_t&,
                                              const backend::ParsedArgumentOwning&,
                                              unsigned,
                                              std::span<std::string>,
                                              decl::DecoOptionBase&)>;

    struct CategoryHandler {
        const decl::Category* category = nullptr;
        context_handler_t handler;
    };

    struct AfterHook {
        bool (*matches)(T&, decl::DecoOptionBase*) = nullptr;
        step_runner_t handler;
    };

    template <typename Handler>
    static auto adapt_context_handler(Handler&& handler) -> context_handler_t {
        using HandlerTy = std::remove_cvref_t<Handler>;
        return context_handler_t(
            [handler = std::forward<Handler>(handler)](context_t& ctx) mutable {
                if constexpr(std::is_invocable_v<HandlerTy&, context_t&>) {
                    handler(ctx);
                } else if constexpr(std::is_invocable_v<HandlerTy&, const context_t&>) {
                    handler(ctx);
                } else {
                    static_assert(always_false_v<HandlerTy>,
                                  "Command handler must accept Context<T, State>&.");
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
    State initialState{};
    std::vector<AfterHook> afterHooks;
    std::vector<context_handler_t> finalizers;
    std::vector<CategoryHandler> categoryHandlers;
    std::optional<context_handler_t> defaultHandler;
    std::optional<text::Renderer> textRenderer;
    error_fn_t errorHandler = [](const ParseError& err) {
        std::println(stderr, "{}", err.message);
    };

    auto renderer_ptr() const -> const text::Renderer* {
        return textRenderer.has_value() ? &*textRenderer : nullptr;
    }

    auto make_context(invocation_t& invocation) const -> context_t {
        return context_t(invocation, commandOverview, &write_usage_for<T>, renderer_ptr());
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
        static_assert(
            std::is_invocable_r_v<decl::ParseControl, FnTy&, ParseStep<T, ValueTy, State>&> ||
                std::is_invocable_r_v<decl::ParseControl,
                                      FnTy&,
                                      const ParseStep<T, ValueTy, State>&>,
            "Command::after callback must return ParseControl and accept ParseStep.");

        AfterHook hook;
        hook.matches = [](T& options, decl::DecoOptionBase* accessor) {
            return static_cast<decl::DecoOptionBase*>(
                       &(detail::access_member_path<Members...>(options))) == accessor;
        };
        hook.handler = [fn = std::forward<Fn>(fn)](
                           context_t& ctx,
                           const backend::ParsedArgumentOwning& arg,
                           unsigned cursor,
                           std::span<std::string> argv,
                           decl::DecoOptionBase& accessor) mutable -> decl::ParseControl {
            auto& typed_option = static_cast<OptionTy&>(accessor);
            ParseStep<T, ValueTy, State> step(ctx, arg, cursor, argv, typed_option.value());
            if constexpr(std::is_invocable_r_v<decl::ParseControl,
                                               FnTy&,
                                               ParseStep<T, ValueTy, State>&>) {
                return fn(step);
            } else {
                return fn(std::as_const(step));
            }
        };
        afterHooks.push_back(std::move(hook));
        return *this;
    }

    template <typename Handler>
        requires (std::is_invocable_v<std::remove_cvref_t<Handler>&, context_t&> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, const context_t&>)
    auto& finalize(Handler&& handler) {
        finalizers.push_back(adapt_context_handler(std::forward<Handler>(handler)));
        return *this;
    }

    template <typename Handler>
        requires (std::is_invocable_v<std::remove_cvref_t<Handler>&, context_t&> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, const context_t&>)
    auto& when(const decl::Category& category, Handler&& handler) {
        for(auto& item: categoryHandlers) {
            if(item.category == &category) {
                item.handler = adapt_context_handler(std::forward<Handler>(handler));
                return *this;
            }
        }
        categoryHandlers.push_back(
            CategoryHandler{.category = &category,
                            .handler = adapt_context_handler(std::forward<Handler>(handler))});
        return *this;
    }

    template <typename Handler>
        requires (std::is_invocable_v<std::remove_cvref_t<Handler>&, context_t&> ||
                  std::is_invocable_v<std::remove_cvref_t<Handler>&, const context_t&>)
    auto& run(Handler&& handler) {
        defaultHandler = adapt_context_handler(std::forward<Handler>(handler));
        return *this;
    }

    auto& when_err(error_fn_t handler) {
        errorHandler = std::move(handler);
        return *this;
    }

    auto& when_err(std::ostream& os) {
        errorHandler = [&os](const ParseError& err) {
            os << err.message << "\n";
        };
        return *this;
    }

    auto& with_state(State state) {
        initialState = std::move(state);
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
        auto res = detail::run_parse_session<T, State>(
            argv,
            initialState,
            [this](invocation_t& invocation,
                   decl::DecoOptionBase& accessor,
                   const backend::ParsedArgumentOwning& arg,
                   unsigned cursor,
                   std::span<std::string> active_argv) {
                if(afterHooks.empty()) {
                    return decl::ParseControl::next();
                }
                auto ctx = make_context(invocation);
                auto* accessor_ptr = &accessor;
                for(auto& hook: afterHooks) {
                    if(hook.matches != nullptr && hook.matches(invocation.options, accessor_ptr)) {
                        const auto control = hook.handler(ctx, arg, cursor, active_argv, accessor);
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

        if(!commandName.empty()) {
            res->command_path = {commandName};
        }
        auto ctx = make_context(*res);
        for(auto& finalize: finalizers) {
            finalize(ctx);
        }
        return res;
    }

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

        auto ctx = make_context(*res);
        for(auto& item: categoryHandlers) {
            if(ctx.matched(*item.category)) {
                item.handler(ctx);
                return;
            }
        }
        if(defaultHandler.has_value()) {
            (*defaultHandler)(ctx);
        }
    }

    auto operator()(std::span<std::string> argv) -> void {
        execute(argv);
    }
};

template <typename T, typename State = std::monostate>
auto command(std::string_view command_overview) -> Command<T, State> {
    return Command<T, State>(command_overview);
}

template <typename T>
class Dispatcher {
    using invocation_t = Invocation<T>;
    using handler_fn_t = runtime_callable_t<void(invocation_t)>;
    using error_fn_t = runtime_callable_t<void(ParseError)>;

    struct CategoryHandler {
        const deco::decl::Category* category = nullptr;
        handler_fn_t handler;
    };

    template <typename Handler>
    static auto adapt_handler(Handler&& handler) -> handler_fn_t {
        using HandlerTy = std::remove_cvref_t<Handler>;
        return handler_fn_t(
            [handler = std::forward<Handler>(handler)](invocation_t invocation) mutable {
                if constexpr(std::is_invocable_v<HandlerTy&, T>) {
                    handler(std::move(invocation.options));
                } else if constexpr(std::is_invocable_v<HandlerTy&, invocation_t>) {
                    handler(std::move(invocation));
                } else if constexpr(std::is_invocable_v<HandlerTy&, const invocation_t&>) {
                    handler(invocation);
                } else {
                    static_assert(always_false_v<HandlerTy>,
                                  "Dispatcher handler must accept T or Invocation<T>.");
                }
            });
    }

    handler_fn_t defaultHandler = [](invocation_t) {
        return "nothing we can do with this options";
    };
    error_fn_t errorHandler = [](auto err) {
        std::println(stderr, "{}", err.message);
    };
    std::vector<CategoryHandler> handlers;
    std::string_view commandOverview;
    std::optional<text::Renderer> textRenderer;

    auto renderer_ptr() const -> const text::Renderer* {
        return textRenderer.has_value() ? &*textRenderer : nullptr;
    }

public:
    Dispatcher(std::string_view command_overview) : commandOverview(command_overview) {}

    auto& dispatch(const decl::Category& category, handler_fn_t handler) {
        for(auto& item: handlers) {
            if(item.category == &category) {
                item.handler = std::move(handler);
                return *this;
            }
        }
        handlers.push_back(CategoryHandler{.category = &category, .handler = std::move(handler)});
        return *this;
    }

    template <typename Handler>
        requires (!std::same_as<std::remove_cvref_t<Handler>, handler_fn_t> &&
                  (std::is_invocable_v<std::remove_cvref_t<Handler>&, T> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, invocation_t> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, const invocation_t&>))
    auto& dispatch(const decl::Category& category, Handler&& handler) {
        return dispatch(category, adapt_handler(std::forward<Handler>(handler)));
    }

    auto& dispatch(handler_fn_t handler) {
        defaultHandler = std::move(handler);
        return *this;
    }

    template <typename Handler>
        requires (!std::same_as<std::remove_cvref_t<Handler>, handler_fn_t> &&
                  (std::is_invocable_v<std::remove_cvref_t<Handler>&, T> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, invocation_t> ||
                   std::is_invocable_v<std::remove_cvref_t<Handler>&, const invocation_t&>))
    auto& dispatch(Handler&& handler) {
        return dispatch(adapt_handler(std::forward<Handler>(handler)));
    }

    auto& when_err(error_fn_t error_handler) {
        errorHandler = std::move(error_handler);
        return *this;
    }

    auto& when_err(std::ostream& os) {
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

    template <typename Os>
    void usage(Os& os, bool include_help = true) const {
        write_usage_for<T>(os, commandOverview, include_help, renderer_ptr());
    }

    void parse(std::span<std::string> argv) {
        auto res = detail::run_parse_session<T, std::monostate>(
            argv,
            std::monostate{},
            [](auto&, decl::DecoOptionBase&, const backend::ParsedArgumentOwning&, unsigned, auto) {
                return decl::ParseControl::next();
            },
            renderer_ptr());
        return from(res);
    }

    void from(std::expected<invocation_t, ParseError> res) {
        if(res.has_value()) {
            for(auto& item: handlers) {
                if(res->matched(*item.category)) {
                    item.handler(std::move(*res));
                    return;
                }
            }
            defaultHandler(std::move(*res));
        } else {
            errorHandler(std::move(res.error()));
        }
    }

    void operator()(std::span<std::string> argv) {
        return parse(argv);
    }
};

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
    auto& add(const decl::SubCommand& subcommand, Dispatcher<OptTy>& dispatcher) {
        return add(subcommand, [&dispatcher](const match_t& match) { dispatcher(match.args()); });
    }

    template <typename OptTy>
    auto& add(const decl::SubCommand& subcommand, Dispatcher<OptTy>&& dispatcher) {
        return add(subcommand, [dispatcher = std::move(dispatcher)](const match_t& match) mutable {
            dispatcher(match.args());
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
