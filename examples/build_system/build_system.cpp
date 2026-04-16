#include <cstdio>
#include <string>

#include "compile_graph.h"
#include "kota/async/async.h"

using namespace kota;
using namespace std::chrono_literals;

static CompileGraph build_graph() {
    CompileGraph graph([] { return 100ms; });

    graph.add_unit("ast.h");
    graph.add_unit("lexer.h");
    graph.add_unit("parser.h");
    graph.add_unit("codegen.h");
    graph.add_unit("lexer.cpp", {"lexer.h"});
    graph.add_unit("ast.cpp", {"ast.h"});
    graph.add_unit("parser.cpp", {"parser.h", "lexer.h"});
    graph.add_unit("codegen.cpp", {"codegen.h", "ast.h"});
    graph.add_unit("main.cpp", {"parser.h", "codegen.h"});

    return graph;
}

static task<> run_shell(event_loop& loop) {
    auto graph = build_graph();

    auto stdin_result = console::open(0, console::options(true));
    if(!stdin_result.has_value()) {
        std::fprintf(stderr, "Failed to open stdin\n");
        co_return;
    }
    auto input = std::move(*stdin_result);

    auto stdout_result = console::open(1);
    if(!stdout_result.has_value()) {
        std::fprintf(stderr, "Failed to open stdout\n");
        co_return;
    }
    auto output = std::move(*stdout_result);

    auto prompt = [&]() -> task<> {
        co_await output.write("> ");
    };

    co_await prompt();

    while(true) {
        auto chunk = co_await input.read();
        if(!chunk.has_value()) {
            break;
        }

        auto line = *chunk;
        // Trim trailing newline
        while(!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }

        if(line.empty()) {
            co_await prompt();
            continue;
        }

        if(line == "quit" || line == "exit") {
            co_await output.write("Bye!\n");
            loop.stop();
            co_return;
        }

        // Parse command
        auto space = line.find(' ');
        auto cmd = line.substr(0, space);
        auto arg = (space != std::string::npos) ? line.substr(space + 1) : std::string{};

        if(cmd == "compile" && !arg.empty()) {
            auto msg = std::string("Compiling ") + arg + "...\n";
            co_await output.write(msg);

            auto result = co_await graph.compile(arg, loop).catch_cancel();
            if(result.has_value() && *result) {
                co_await output.write("Compilation succeeded.\n");
            } else {
                co_await output.write("Compilation cancelled or failed.\n");
            }
        } else if(cmd == "update" && !arg.empty()) {
            graph.update(arg);
            auto msg = std::string("Updated ") + arg + " (invalidated dependents).\n";
            co_await output.write(msg);
        } else if(cmd == "help") {
            co_await output.write("Commands:\n");
            co_await output.write("  compile <file>  - Compile a file and its dependencies\n");
            co_await output.write(
                "  update <file>   - Mark file as modified (cancels in-flight)\n");
            co_await output.write("  quit            - Exit\n");
        } else {
            co_await output.write("Unknown command. Type 'help' for usage.\n");
        }

        co_await prompt();
    }
}

int main() {
    std::printf("Build System Example\n");
    std::printf("Type 'help' for available commands.\n\n");

    event_loop loop;
    auto shell = run_shell(loop);
    loop.schedule(shell);
    loop.run();
    return 0;
}
