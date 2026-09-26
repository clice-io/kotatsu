// "kota/zest/macro.h" is what a downstream consuming kotatsu as a module has to
// include: modules cannot export macros, so the test macros must arrive through
// a textual include that drags in no zest declarations of its own. Including it
// as the very first header here keeps that contract honest — if it ever grows a
// dependency on another zest header, this translation unit stops compiling.
#include "kota/zest/macro.h"

#if !defined(ZEST_SUITE) || !defined(ZEST_CASE) || !defined(EXPECT) || !defined(ASSERT) ||         \
    !defined(STATIC_EXPECT) || !defined(ZEST_CONTEXT) || !defined(EXPECT_SNAPSHOT) ||              \
    !defined(EXPECT_SNAPSHOT_JSON)
#error "kota/zest/macro.h must define the zest test macros on its own"
#endif

#include "kota/zest/zest.h"

namespace kota::zest {

namespace {

// Written against the macros already in scope from the standalone include above,
// which is the order a module consumer ends up with.
ZEST_SUITE(zest_macro_standalone) {

ZEST_CASE(macros_usable_without_declaration_headers) {
    STATIC_EXPECT(1 + 1 == 2);
    ASSERT(true);
    EXPECT(std::string("a") == std::string("a"));
}

};  // ZEST_SUITE(zest_macro_standalone)

}  // namespace

}  // namespace kota::zest
