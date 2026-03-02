#include <cstdio>
#include <iostream>
#include <print>
#include <string>
#include <string_view>

#include "eventide/deco/decl.h"
#include "eventide/deco/macro.h"
#include "eventide/deco/runtime.h"
#include "eventide/zest/runner.h"
#include "eventide/zest/zest.h"
#include "eventide/async/loop.h"
#include "eventide/async/stream.h"

struct UnitestOpt {
    DecoKVStyled(style = deco::decl::KVStyle::Joined | deco::decl::KVStyle::Separate,
                 meta_var = "<PATTERN>",
                 help = "test name filters, SUITE or SUITE.TEST or SUITE.* or *",
                 required = false)
    <std::string> test_filter = "";
};

int main(int argc, char** argv) {
    std::string filter;
    auto args = deco::util::argvify(argc, argv);
    deco::cli::Dispatcher<UnitestOpt> dispatcher("unitest [options] Run unit tests");
    dispatcher.dispatch([&filter](UnitestOpt opt) { filter = std::move(*opt.test_filter); })
        .when_err([&](const deco::cli::ParseError& err) {
            std::cerr << "Error parsing options: " << err.message << "\n";
            dispatcher.usage(std::cerr);
            std::exit(1);
        });
    dispatcher(args);
    return eventide::zest::Runner::instance().run_tests(filter);
}
