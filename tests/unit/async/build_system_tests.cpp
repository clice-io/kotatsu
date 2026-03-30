#include <chrono>

#include "compile_graph.h"
#include "loop_fixture.h"
#include "eventide/zest/zest.h"

namespace eventide {

namespace {

using namespace std::chrono_literals;

static CompileGraph make_test_graph() {
    CompileGraph graph([] { return 2ms; });

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

TEST_SUITE(build_system, loop_fixture) {

TEST_CASE(normal_compilation_completes) {
    auto graph = make_test_graph();

    auto test = [&]() -> task<> {
        auto result = co_await graph.compile("main.cpp", loop).catch_cancel();
        EXPECT_TRUE(result.has_value());
        EXPECT_TRUE(*result);
    };

    auto t = test();
    schedule_all(t);
}

TEST_CASE(update_cancels_in_flight) {
    auto graph = make_test_graph();
    bool compile_cancelled = false;

    auto compiler = [&]() -> task<> {
        auto res = co_await graph.compile("main.cpp", loop).catch_cancel();
        compile_cancelled = !res.has_value();
    };

    auto updater = [&]() -> task<> {
        co_await sleep(1ms, loop);
        graph.update("parser.h");
    };

    auto c = compiler();
    auto u = updater();
    schedule_all(c, u);

    EXPECT_TRUE(compile_cancelled);
}

TEST_CASE(chain_cancel_propagates) {
    auto graph = make_test_graph();
    bool compile_cancelled = false;

    auto compiler = [&]() -> task<> {
        auto res = co_await graph.compile("parser.cpp", loop).catch_cancel();
        compile_cancelled = !res.has_value();
    };

    auto updater = [&]() -> task<> {
        co_await sleep(1ms, loop);
        graph.update("lexer.h");
    };

    auto c = compiler();
    auto u = updater();
    schedule_all(c, u);

    EXPECT_TRUE(compile_cancelled);
}

TEST_CASE(recompile_after_update) {
    auto graph = make_test_graph();

    auto test = [&]() -> task<> {
        auto result1 = co_await graph.compile("lexer.cpp", loop).catch_cancel();
        EXPECT_TRUE(result1.has_value());
        EXPECT_TRUE(*result1);

        graph.update("lexer.cpp");
        auto result2 = co_await graph.compile("lexer.cpp", loop).catch_cancel();
        EXPECT_TRUE(result2.has_value());
        EXPECT_TRUE(*result2);
    };

    auto t = test();
    schedule_all(t);
}

TEST_CASE(independent_compilations_unaffected) {
    auto graph = make_test_graph();
    bool parser_cancelled = false;
    bool codegen_ok = false;

    auto compile_parser = [&]() -> task<> {
        auto res = co_await graph.compile("parser.cpp", loop).catch_cancel();
        parser_cancelled = !res.has_value();
    };

    auto compile_codegen = [&]() -> task<> {
        auto res = co_await graph.compile("codegen.cpp", loop).catch_cancel();
        codegen_ok = res.has_value() && *res;
    };

    auto updater = [&]() -> task<> {
        co_await sleep(1ms, loop);
        graph.update("parser.h");
    };

    auto cp = compile_parser();
    auto cc = compile_codegen();
    auto u = updater();
    schedule_all(cp, cc, u);

    EXPECT_TRUE(parser_cancelled);
    EXPECT_TRUE(codegen_ok);
}

TEST_CASE(shared_dependency_compiled_once) {
    int compile_count = 0;

    // Use a side-effecting delay_fn to count actual compilations
    CompileGraph graph([&] {
        compile_count += 1;
        return 2ms;
    });

    graph.add_unit("common.h");
    graph.add_unit("a.cpp", {"common.h"});
    graph.add_unit("b.cpp", {"common.h"});

    auto test = [&]() -> task<> {
        // Both a.cpp and b.cpp depend on common.h.
        // Without deduplication, common.h would be compiled twice.
        co_await when_all(graph.compile("a.cpp", loop), graph.compile("b.cpp", loop));
    };

    auto t = test();
    schedule_all(t);

    // common.h (1) + a.cpp (1) + b.cpp (1) = 3
    // Without dedup this would be 4 (common.h compiled twice).
    EXPECT_EQ(compile_count, 3);
}

};  // TEST_SUITE(build_system)

}  // namespace

}  // namespace eventide
