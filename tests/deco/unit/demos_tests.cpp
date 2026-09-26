#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {
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

ZEST_SUITE(deco_demos) {

ZEST_CASE(MyClang) {
    auto cmd = cli::command<MyClang>("Clang [OPTIONS] inputs");
    cmd.render_with(cli::text::ModernRenderer());

    std::stringstream ss;
    cmd.usage(ss);
    EXPECT(ss.str().contains("Clang [OPTIONS] inputs"));
    EXPECT(ss.str().contains("-o1"));
    cmd.usage(std::cout);

    std::vector<std::string> args = {"-o1", "--support-ext", "cc"};
    auto res = cmd.invoke(args);
    EXPECT(res);
    if(!res.has_value()) {
        return;
    }

    auto opt = res->options;
    EXPECT(opt.optimize.value() == 1);
    EXPECT(opt.support_ext.value() == MyClang::Support::cc);
}

};  // ZEST_SUITE(deco_demos)

}  // namespace
}  // namespace kota::deco
