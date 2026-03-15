#pragma once

#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "eventide/deco/backend.h"
#include "eventide/deco/decl.h"
#include "eventide/deco/descriptor.h"

namespace deco::util {

inline std::vector<std::string> argvify(int argc, const char* const* argv, unsigned skip_num = 1) {
    std::vector<std::string> res;
    for(unsigned i = skip_num; i < static_cast<unsigned>(argc); ++i) {
        res.emplace_back(argv[i]);
    }
    return res;
}

}  // namespace deco::util

namespace deco::cli {

template <typename Signature>
using runtime_callable_t =
#if defined(__APPLE__)
    std::function<Signature>;
#else
    std::move_only_function<Signature>;
#endif

template <typename T>
struct ParsedResult {
    T options;
    std::set<const decl::Category*> matched_categories;
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
    const auto& storage = detail::build_storage<T>();
    std::string err = "";
    // check required options
    storage.visit_fields(options, [&](auto& field, const auto& cfg, std::string_view name, auto) {
        if(matched_categories.contains(cfg.category.ptr()) && cfg.required &&
           !field.value.has_value()) {
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

template <typename T>
std::expected<ParsedResult<T>, ParseError> parse(std::span<std::string> argv) {
    const auto& storage = detail::build_storage<T>();
    backend::OptTable table = storage.make_opt_table();
    ParsedResult<T> res{};
    ParseError err;
    table.parse_args(argv, [&](std::expected<backend::ParsedArgument, std::string> arg) {
        if(!arg.has_value()) {
            err = {ParseError::Type::BackendParsing, std::move(arg.error())};
            return false;
        }

        if(storage.is_unknown_option_id(arg->option_id)) {
            err = {ParseError::Type::BackendParsing,
                   std::format("unknown option '{}'", arg->get_spelling_view())};
            return false;
        }

        auto& raw_parg = *arg;
        void* opt_raw_ptr = nullptr;
        const decl::Category* category = nullptr;

        // check input and trailing input
        if(storage.is_input_argument(raw_parg)) {
            if(!storage.has_input_option()) {
                err = {ParseError::Type::DecoParsing,
                       std::format("unexpected input argument {}", raw_parg.get_spelling_view())};
                return false;
            }
        } else if(storage.is_trailing_argument(raw_parg)) {
            if(!storage.has_trailing_option()) {
                err = {
                    ParseError::Type::DecoParsing,
                    std::format("unexpected trailing argument {}", raw_parg.get_spelling_view())};
                return false;
            } else {
                opt_raw_ptr = storage.trailing_ptr_of(res.options);
                category = storage.trailing_category();
            }
        }

        opt_raw_ptr =
            opt_raw_ptr ? opt_raw_ptr : storage.field_ptr_of(raw_parg.option_id, res.options);
        category = category ? category : storage.category_of(raw_parg.option_id);

        decl::DecoOptionBase* opt_accessor = static_cast<decl::DecoOptionBase*>(opt_raw_ptr);
        if(opt_accessor == nullptr) {
            err = {ParseError::Type::Internal,
                   "no option accessor found for option id " +
                       std::to_string(raw_parg.option_id.id())};
            return false;
        }
        if(auto parse_err = opt_accessor->into(std::move(raw_parg))) {
            err = {ParseError::Type::IntoError, std::move(*parse_err)};
            return false;
        }

        if(category == nullptr) {
            err = {ParseError::Type::Internal,
                   "no category found for option id " + std::to_string(raw_parg.option_id.id())};
            return false;
        }
        res.matched_categories.insert(category);
        return true;
    });
    if(!err.message.empty()) {
        return std::unexpected(std::move(err));
    }
    // check required options
    storage.visit_fields(res.options,
                         [&](auto& field, const auto& cfg, std::string_view name, auto) {
                             if(res.matched_categories.contains(cfg.category.ptr()) &&
                                cfg.required && !field.value.has_value()) {
                                 err = {ParseError::Type::DecoParsing,
                                        std::format("required option {} is missing",
                                                    desc::from_deco_option(cfg, false, name))};
                                 return false;
                             }
                             return true;
                         });
    if(!err.message.empty()) {
        return std::unexpected(std::move(err));
    }

    // check category requirements
    const std::string check_err = check_valid(res.options, res.matched_categories);
    if(!check_err.empty()) {
        err = {ParseError::Type::DecoParsing, std::move(check_err)};
        return std::unexpected(std::move(err));
    }
    return res;
}

template <typename T>
class Dispatcher {
    using handler_fn_t = runtime_callable_t<void(T value)>;
    using error_fn_t = runtime_callable_t<void(ParseError)>;

    handler_fn_t defaultHandler = [](auto) {
        return "nothing we can do with this options";
    };
    error_fn_t errorHandler = [](auto err) {
        std::println(stderr, "{}", err.message);
    };
    std::map<const deco::decl::Category*, handler_fn_t> handlers;
    std::string_view commandOverview;

public:
    Dispatcher(std::string_view command_overview) : commandOverview(command_overview) {}

    auto& dispatch(const decl::Category& category, handler_fn_t handler) {
        handlers[&category] = std::move(handler);
        return *this;
    }

    auto& dispatch(handler_fn_t handler) {
        defaultHandler = std::move(handler);
        return *this;
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

    template <typename Os>
    void usage(Os& os, bool include_help = true) const {
        const auto& storage = detail::build_storage<T>();
        std::map<const decl::Category*, std::vector<std::string>> category_usage_map;
        auto on_option = [&](auto, const auto& opt_fields, std::string_view field_name, auto) {
            category_usage_map[opt_fields.category.ptr()].push_back(
                desc::from_deco_option(opt_fields, include_help, field_name));
            return true;
        };
        storage.visit_fields(T{}, on_option);
        os << "usage: " << commandOverview << "\n\n";
        os << "Options:\n";
        if(category_usage_map.size() == 1 &&
           category_usage_map.begin()->first == &decl::default_category) {
            for(const auto& usage: category_usage_map.begin()->second) {
                os << "  " << usage << "\n";
            }
        } else {
            for(const auto& [category, usages]: category_usage_map) {
                os << "Group " << desc::detail::category_desc(*category);
                if(category->exclusive) {
                    os << ", exclusive with other groups";
                }
                os << ":\n";
                for(const auto& usage: usages) {
                    os << "  " << usage << "\n";
                }
                os << "\n";
            }
        }
    }

    void parse(std::span<std::string> argv) {
        auto res = cli::parse<T>(argv);
        if(res.has_value()) {
            const auto& matched_categories = res->matched_categories;
            for(auto category: matched_categories) {
                auto it = handlers.find(category);
                if(it != handlers.end()) {
                    it->second(std::move(res->options));
                    return;
                }
            }
            defaultHandler(std::move(res->options));
        } else {
            errorHandler(std::move(res.error()));
        }
    }

    void operator()(std::span<std::string> argv) {
        return parse(argv);
    }
};

class SubCommander {
    using handler_fn_t = runtime_callable_t<void(std::span<std::string>)>;
    using error_fn_t = runtime_callable_t<void(SubCommandError)>;

    struct SubCommandHandler {
        std::string name;
        std::string description;
        std::string command;
        handler_fn_t handler;
    };

    error_fn_t errorHandler = [](const SubCommandError& err) {
        std::println(stderr, "{}", err.message);
    };
    std::optional<handler_fn_t> defaultHandler;
    std::vector<SubCommandHandler> handlers;
    std::map<std::string, std::size_t, std::less<>> commandToHandler;

    std::string commandOverview;
    std::string overview;

    static auto command_of(const decl::SubCommand& subcommand) -> std::string {
        if(subcommand.command.has_value()) {
            return std::string(*subcommand.command);
        }
        return std::string(subcommand.name);
    }

    static auto display_name_of(const decl::SubCommand& subcommand, std::string_view command)
        -> std::string {
        if(!subcommand.name.empty()) {
            return std::string(subcommand.name);
        }
        return std::string(command);
    }

public:
    SubCommander(std::string_view command_overview, std::string_view overview = {}) :
        commandOverview(command_overview), overview(overview) {}

    auto& add(const decl::SubCommand& subcommand, handler_fn_t handler) {
        std::string command = command_of(subcommand);
        if(command.empty()) {
            errorHandler(
                {SubCommandError::Type::Internal, "subcommand name/command must not be empty"});
            return *this;
        }

        std::string name = display_name_of(subcommand, command);
        std::string description(subcommand.description);

        if(auto it = commandToHandler.find(command); it != commandToHandler.end()) {
            auto& target = handlers[it->second];
            target.name = std::move(name);
            target.description = std::move(description);
            target.command = std::move(command);
            target.handler = std::move(handler);
            return *this;
        }

        commandToHandler[command] = handlers.size();
        handlers.push_back({
            .name = std::move(name),
            .description = std::move(description),
            .command = std::move(command),
            .handler = std::move(handler),
        });
        return *this;
    }

    template <typename OptTy>
    auto& add(const decl::SubCommand& subcommand, Dispatcher<OptTy>& dispatcher) {
        return add(subcommand,
                   handler_fn_t([&dispatcher](std::span<std::string> argv) { dispatcher(argv); }));
    }

    template <typename OptTy>
    auto& add(const decl::SubCommand& subcommand, Dispatcher<OptTy>&& dispatcher) {
        return add(subcommand,
                   handler_fn_t([dispatcher = std::move(dispatcher)](
                                    std::span<std::string> argv) mutable { dispatcher(argv); }));
    }

    auto& add(handler_fn_t default_handler) {
        defaultHandler = std::move(default_handler);
        return *this;
    }

    auto& when_err(error_fn_t error_handler) {
        errorHandler = std::move(error_handler);
        return *this;
    }

    auto& when_err(std::ostream& os) {
        errorHandler = [&os](const SubCommandError& err) {
            os << err.message << "\n";
        };
        return *this;
    }

    template <typename Os>
    void usage(Os& os) const {
        if(!overview.empty()) {
            os << overview << "\n\n";
        }

        if(defaultHandler.has_value()) {
            os << "usage: " << commandOverview << "\n";
            if(!handlers.empty()) {
                os << "\n";
            }
        }

        if(handlers.empty()) {
            return;
        }

        std::size_t max_name_len = 0;
        for(const auto& it: handlers) {
            if(it.name.size() > max_name_len) {
                max_name_len = it.name.size();
            }
        }

        os << "Subcommands:\n";
        for(const auto& it: handlers) {
            os << "  " << it.name;
            if(!it.description.empty()) {
                os << std::string(max_name_len - it.name.size() + 2, ' ') << it.description;
            }
            if(it.command != it.name) {
                os << " (" << it.command << ")";
            }
            os << "\n";
        }
    }

    void parse(std::span<std::string> argv) {
        if(!argv.empty()) {
            if(auto it = commandToHandler.find(argv.front()); it != commandToHandler.end()) {
                handlers[it->second].handler(argv.subspan(1));
                return;
            }
        }

        if(defaultHandler.has_value()) {
            (*defaultHandler)(argv);
            return;
        }

        if(argv.empty()) {
            errorHandler({SubCommandError::Type::MissingSubCommand, "subcommand is required"});
            return;
        }

        errorHandler({SubCommandError::Type::UnknownSubCommand,
                      std::format("unknown subcommand '{}'", argv.front())});
    }

    void operator()(std::span<std::string> argv) {
        return parse(argv);
    }
};

};  // namespace deco::cli
