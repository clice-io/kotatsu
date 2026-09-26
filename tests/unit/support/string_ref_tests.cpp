#include <format>

#include "kota/zest/zest.h"
#include "kota/support/string_ref.h"

namespace kota {
namespace {

ZEST_SUITE(string_ref) {

ZEST_CASE(std_format) {
    EXPECT(std::format("{}", string_ref("abc")) == "abc");
}

};  // ZEST_SUITE(string_ref)

}  // namespace
}  // namespace kota
