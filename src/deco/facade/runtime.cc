#include <iostream>
#include <utility>

#include "kota/deco/deco.h"

namespace kota::deco::util {

std::vector<std::string> argvify(int argc, const char* const* argv, unsigned skip_num) {
    std::vector<std::string> res;
    if(argc <= 0) {
        return res;
    }
    for(unsigned i = skip_num; i < static_cast<unsigned>(argc); ++i) {
        res.emplace_back(argv[i]);
    }
    return res;
}

}  // namespace kota::deco::util

namespace kota::deco::cli {

auto SubCommander::command_of(const decl::SubCommand& subcommand) -> std::string {
    if(subcommand.command.has_value()) {
        return std::string(*subcommand.command);
    }
    return std::string(subcommand.name);
}

auto SubCommander::display_name_of(const decl::SubCommand& subcommand, std::string_view command)
    -> std::string {
    if(!subcommand.name.empty()) {
        return std::string(subcommand.name);
    }
    return std::string(command);
}

SubCommander::SubCommander(std::string_view command_overview, std::string_view overview) :
    command_overview(command_overview), overview(overview) {}

auto SubCommander::add(const decl::SubCommand& subcommand, SubCommander::handler_fn_t handler)
    -> SubCommander& {
    std::string command = command_of(subcommand);
    if(command.empty()) {
        error_handler(SubCommandError{SubCommandError::Type::Internal,
                                      "subcommand name/command must not be empty"});
        return *this;
    }

    SubCommandHandler entry{
        .name = display_name_of(subcommand, command),
        .description = std::string(subcommand.description),
        .command = command,
        .handler = std::move(handler),
    };
    // Adding a command again replaces it where it stands.
    if(auto [it, inserted] = command_to_handler.try_emplace(command, handlers.size()); !inserted) {
        handlers[it->second] = std::move(entry);
    } else {
        handlers.push_back(std::move(entry));
    }
    return *this;
}

auto SubCommander::add(SubCommander::handler_fn_t handler) -> SubCommander& {
    default_handler = std::move(handler);
    return *this;
}

auto SubCommander::when_err(std::ostream& os) -> SubCommander& {
    return when_err([&os](const SubCommandError& err) { os << err.message << "\n"; });
}

auto SubCommander::enable_help() -> SubCommander& {
    help_enabled = true;
    return *this;
}

void SubCommander::usage(std::ostream& os) const {
    const auto active_config = resolved_config();
    std::optional<text::Renderer> fallback_renderer;
    if(renderer_ptr() == nullptr && text::explicit_default_renderer() == nullptr) {
        fallback_renderer.emplace(text::CompatibleRenderer(active_config.render.compatible));
    }
    text::SubCommandDocument document{
        .overview = overview,
        .usage_line = command_overview,
        .has_usage_line = default_handler.has_value(),
        .entries = {},
    };
    document.entries.reserve(handlers.size());
    for(const auto& item: handlers) {
        document.entries.push_back(text::SubCommandEntry{
            .name = item.name,
            .description = item.description,
            .command = item.command,
        });
    }
    os << text::render_subcommands(document,
                                   fallback_renderer.has_value() ? &*fallback_renderer
                                                                 : renderer_ptr());
}

auto SubCommander::match(std::span<std::string> argv) const
    -> std::expected<SubCommander::match_t, SubCommandError> {
    auto positioned_error = [&](SubCommandError::Type type,
                                unsigned begin,
                                unsigned end,
                                std::string message) -> std::expected<match_t, SubCommandError> {
        const auto argv_view = std::span<const std::string>(argv.data(), argv.size());
        return std::unexpected(SubCommandError{
            type,
            text::render_diagnostic(text::diagnostic_at(argv_view, begin, end, std::move(message)),
                                    renderer_ptr()),
        });
    };

    if(!argv.empty()) {
        if(auto it = command_to_handler.find(argv.front()); it != command_to_handler.end()) {
            const auto& handler = handlers[it->second];
            return match_t{
                .kind = match_t::Kind::Command,
                .original_argv = argv,
                .remaining_argv = argv.subspan(1),
                .token = argv.front(),
                .name = handler.name,
                .command = handler.command,
            };
        }
    }

    if(default_handler.has_value()) {
        return match_t{
            .kind = match_t::Kind::Default,
            .original_argv = argv,
            .remaining_argv = argv,
            .token = argv.empty() ? std::string_view{} : std::string_view(argv.front()),
        };
    }

    if(argv.empty()) {
        return positioned_error(SubCommandError::Type::MissingSubCommand,
                                0,
                                0,
                                "subcommand is required");
    }

    return positioned_error(SubCommandError::Type::UnknownSubCommand,
                            0,
                            1,
                            std::format("unknown subcommand '{}'", argv.front()));
}

auto SubCommander::parse(std::span<std::string> argv) -> int {
    if(help_enabled && !argv.empty() && (argv.front() == "-h" || argv.front() == "--help")) {
        usage(std::cout);
        return 0;
    }
    auto matched = match(argv);
    if(!matched.has_value()) {
        return error_handler(std::move(matched.error()));
    }
    if(matched->is_command()) {
        auto& handler = handlers[command_to_handler.find(matched->command)->second].handler;
        return handler(std::move(*matched));
    }
    return (*default_handler)(std::move(*matched));
}

auto SubCommander::operator()(std::span<std::string> argv) -> int {
    return parse(argv);
}

}  // namespace kota::deco::cli
