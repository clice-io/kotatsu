#include <utility>

#include "kota/deco/deco.h"

namespace deco::util {

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

}  // namespace deco::util

namespace deco::cli {

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
    commandOverview(command_overview), overview(overview) {}

auto SubCommander::add(const decl::SubCommand& subcommand, SubCommander::handler_fn_t handler)
    -> SubCommander& {
    std::string command = command_of(subcommand);
    if(command.empty()) {
        errorHandler(SubCommandError{SubCommandError::Type::Internal,
                                     "subcommand name/command must not be empty"});
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

auto SubCommander::add(SubCommander::handler_fn_t default_handler) -> SubCommander& {
    defaultHandler = std::move(default_handler);
    return *this;
}

auto SubCommander::when_err(SubCommander::error_fn_t error_handler) -> SubCommander& {
    errorHandler = std::move(error_handler);
    return *this;
}

auto SubCommander::when_err(std::ostream& os) -> SubCommander& {
    errorHandler = [&os](const SubCommandError& err) {
        os << err.message << "\n";
    };
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
        .usage_line = commandOverview,
        .has_usage_line = defaultHandler.has_value(),
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
        if(auto it = commandToHandler.find(argv.front()); it != commandToHandler.end()) {
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

    if(defaultHandler.has_value()) {
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

void SubCommander::parse(std::span<std::string> argv) {
    auto matched = match(argv);
    if(!matched.has_value()) {
        errorHandler(std::move(matched.error()));
        return;
    }

    if(matched->is_command()) {
        if(auto it = commandToHandler.find(matched->command); it != commandToHandler.end()) {
            handlers[it->second].handler(std::move(*matched));
            return;
        }
        errorHandler(SubCommandError{
            SubCommandError::Type::Internal,
            std::format("missing handler for subcommand '{}'", matched->command),
        });
        return;
    }

    if(defaultHandler.has_value()) {
        (*defaultHandler)(std::move(*matched));
        return;
    }

    errorHandler(
        SubCommandError{SubCommandError::Type::Internal, "default route resolved without handler"});
}

void SubCommander::operator()(std::span<std::string> argv) {
    parse(argv);
}

}  // namespace deco::cli
