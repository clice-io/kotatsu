#include <fstream>
#include <string>

#include "kota/zest/zest.h"

namespace kota::zest {

namespace {

std::string read_file(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}

TEST_SUITE(snapshot) {

TEST_CASE(basic_named) {
    check_snapshot("hello snapshot", "basic_named");
}

TEST_CASE(glob_fixtures) {
    ASSERT_SNAPSHOT_GLOB("fixtures/**/*.txt", read_file);
}

};  // TEST_SUITE(snapshot)

}  // namespace

}  // namespace kota::zest
