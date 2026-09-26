// "kota/deco/macro.h" is what a downstream consuming kotatsu as a module has to
// include: modules cannot export macros, so the Deco* macros must arrive through
// a textual include that drags in no deco declarations of its own. Including it
// as the very first header here keeps that contract honest — if it ever grows a
// dependency on a facade header, this translation unit stops compiling.
#include "kota/deco/macro.h"

#if !defined(DECO_CFG) || !defined(DecoFlag) || !defined(DecoKV) || !defined(DecoInput) ||         \
    !defined(DecoPack) || !defined(DecoMulti) || !defined(DecoComma) || !defined(DecoFlagAlias)
#error "kota/deco/macro.h must define the deco declaration macros on its own"
#endif

#include <string>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {
namespace {

// Declared with the macros already in scope from the standalone include above,
// which is the order a module consumer ends up with.
struct StandaloneCfg {
    DecoFlag(names = {"-v", "--verbose"}; help = "verbose")
    verbose;
    DecoKV(help = "output")
    <std::string> output = "a.out";
};

ZEST_SUITE(deco_macro_standalone){

    ZEST_CASE(DeclaresOptions){auto cmd = cli::command<StandaloneCfg>("app [OPTIONS]");

std::vector<std::string> args = {"--verbose", "--output", "b.out"};
auto res = cmd.invoke(args);
ASSERT(res.has_value());

EXPECT(res->options.verbose.value());
EXPECT(res->options.output.value() == "b.out");

}  // namespace

};  // namespace kota::deco

}  // namespace
}  // namespace kota::deco
