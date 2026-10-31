#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kotatsu/cli/table/arg.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

ZEST_SUITE(cli_table_arg) {

ZEST_CASE(parsed_arg_clear_resets_every_field) {
    ParsedArg arg{.id = 3, .index = 1, .next_index = 2, .spelling = "-o", .values = {}};
    arg.add_value("out");
    ZASSERT(arg.values.size() == 1U);
    ZEXPECT(arg.values[0] == "out");

    arg.clear();
    ZEXPECT(arg.id == 0U);
    ZEXPECT(arg.index == 0U);
    ZEXPECT(arg.next_index == 0U);
    ZEXPECT(arg.spelling.empty());
    ZEXPECT(arg.values.empty());
}

ZEST_CASE(prefixes_start_with_the_one_written) {
    ZEXPECT(pfx_none.empty());
    ZEXPECT(std::vector(pfx_dash.begin(), pfx_dash.end()) == std::vector<std::string_view>{"-"});
    ZEXPECT(std::vector(pfx_double.begin(), pfx_double.end()) ==
            std::vector<std::string_view>{"--"});
    ZEXPECT(std::vector(pfx_dash_double.begin(), pfx_dash_double.end()) ==
            std::vector<std::string_view>{"-", "--"});
    ZEXPECT(std::vector(pfx_slash_dash.begin(), pfx_slash_dash.end()) ==
            std::vector<std::string_view>{"/", "-"});
    ZEXPECT(std::vector(pfx_all.begin(), pfx_all.end()) ==
            std::vector<std::string_view>{"--", "/", "-"});
}

};  // ZEST_SUITE(cli_table_arg)

}  // namespace

}  // namespace kotatsu::cli
