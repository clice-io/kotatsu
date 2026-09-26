#include <chrono>
#include <tuple>
#include <utility>

#include "compile_graph.h"
#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"

namespace kota {

namespace {

using namespace std::literals;

/// The example's graph: main.cpp over parser and codegen, each over headers.
/// `started` is set whenever a unit starts its work.
CompileGraph make_test_graph(event& started) {
    CompileGraph graph([&started] {
        started.set();
        return 2ms;
    });

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

ZEST_SUITE(async_build_system, test::LoopFixture) {

ZEST_CASE(normal_compilation_completes) {
    event started;
    auto graph = make_test_graph(started);

    auto [compiled] = run(graph.compile("main.cpp", loop));
    ASSERT(compiled.has_value());
    EXPECT(*compiled);
}

// The update comes while main.cpp's first dependency, parser.h, is at work.
ZEST_CASE(update_cancels_in_flight) {
    event started;
    auto graph = make_test_graph(started);
    auto updater = [&]() -> task<> {
        co_await started.wait();
        graph.update("parser.h");
    };

    auto [compiled, updated] = run(graph.compile("main.cpp", loop), updater());
    EXPECT(compiled.is_cancelled());
}

// lexer.h has not started when it changes; parser.cpp, which depends on it,
// is cancelled all the same.
ZEST_CASE(chain_cancel_propagates) {
    event started;
    auto graph = make_test_graph(started);
    auto updater = [&]() -> task<> {
        co_await started.wait();
        graph.update("lexer.h");
    };

    auto [compiled, updated] = run(graph.compile("parser.cpp", loop), updater());
    EXPECT(compiled.is_cancelled());
}

ZEST_CASE(recompile_after_update) {
    event started;
    auto graph = make_test_graph(started);
    auto twice = [&]() -> task<std::pair<bool, bool>> {
        bool first = co_await graph.compile("lexer.cpp", loop);
        graph.update("lexer.cpp");
        bool second = co_await graph.compile("lexer.cpp", loop);
        co_return std::pair{first, second};
    };

    auto [compiled] = run(twice());
    ASSERT(compiled.has_value());
    EXPECT(*compiled == std::pair{true, true});
}

ZEST_CASE(independent_compilations_unaffected) {
    event started;
    auto graph = make_test_graph(started);
    auto updater = [&]() -> task<> {
        co_await started.wait();
        graph.update("parser.h");
    };

    auto [parser, codegen, updated] =
        run(graph.compile("parser.cpp", loop), graph.compile("codegen.cpp", loop), updater());
    EXPECT(parser.is_cancelled());
    ASSERT(codegen.has_value());
    EXPECT(*codegen);
}

// a.cpp and b.cpp both depend on common.h, which is compiled once: three
// compilations, not four.
ZEST_CASE(shared_dependency_compiled_once) {
    int compile_count = 0;
    CompileGraph graph([&] {
        compile_count += 1;
        return 2ms;
    });
    graph.add_unit("common.h");
    graph.add_unit("a.cpp", {"common.h"});
    graph.add_unit("b.cpp", {"common.h"});
    auto both = [&]() -> task<std::tuple<bool, bool>> {
        co_return co_await when_all(graph.compile("a.cpp", loop), graph.compile("b.cpp", loop));
    };

    auto [compiled] = run(both());
    ASSERT(compiled.has_value());
    EXPECT(*compiled == std::tuple{true, true});
    EXPECT(compile_count == 3);
}

};  // ZEST_SUITE(async_build_system)

}  // namespace

}  // namespace kota
