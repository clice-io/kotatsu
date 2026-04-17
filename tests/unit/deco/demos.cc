#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/deco/detail/text.h"
#include "kota/zest/macro.h"

namespace {

struct MyClang {
    enum class Support { cpp, c, cc };
    DecoKV(help = "support ext")
    <Support> support_ext = Support::cpp;
    DecoKV(help = "control optimization")
    <int> optimize;
    DecoFlagAlias(
        names = {"-o1"},
        forward = {"--optimize", "1"},
        required = false,
        help =
            "Basic optimizations. A balance between code size and performance without significantly increasing compile time.";)
        _;
};

}  // namespace

TEST_SUITE(deco_demos) {

TEST_CASE(MyClang) {
    auto cmd = kota::deco::cli::command<MyClang>("Clang [OPTIONS] inputs");
    cmd.render_with(kota::deco::cli::text::ModernRenderer());

    std::stringstream ss;
    cmd.usage(ss);
    EXPECT_TRUE(ss.str().contains("Clang [OPTIONS] inputs"));
    EXPECT_TRUE(ss.str().contains("-o1"));
    cmd.usage(std::cout);

    std::vector<std::string> args = {"-o1", "--support-ext", "cc"};
    auto res = cmd.invoke(args);
    EXPECT_TRUE(res.has_value());
    if(!res.has_value()) {
        return;
    }

    auto opt = res->options;
    EXPECT_TRUE(opt.optimize.value() == 1);
    EXPECT_TRUE(opt.support_ext.value() == MyClang::Support::cc);
}

};  // TEST_SUITE(deco_demos)
