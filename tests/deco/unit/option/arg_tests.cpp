#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "kota/deco/option/arg.h"
#include "kota/zest/zest.h"

namespace kota::option {

namespace {

ZEST_SUITE(deco_option_arg) {

ZEST_CASE(args_ref_reads_strings) {
    const std::vector<std::string> argv = {"-o", "out"};
    const ArgsRef args(argv);
    ASSERT(args.size() == 2U);
    EXPECT(args[0] == "-o");
    EXPECT(args[1] == "out");
}

ZEST_CASE(args_ref_reads_views) {
    constexpr std::array<std::string_view, 3> argv = {"a", "b", "c"};
    const ArgsRef args(argv);
    ASSERT(args.size() == 3U);
    EXPECT(args[2] == "c");
}

ZEST_CASE(args_ref_reads_through_its_accessor) {
    // Every element reads as the text at `data`, whatever its index.
    constexpr std::string_view text = "same";
    const ArgsRef args(&text, 4, [](const void* data, std::uint32_t) {
        return *static_cast<const std::string_view*>(data);
    });
    ASSERT(args.size() == 4U);
    EXPECT(args[3] == "same");
}

ZEST_CASE(args_ref_default_is_empty) {
    EXPECT(ArgsRef().size() == 0U);
}

ZEST_CASE(parsed_arg_clear_resets_every_field) {
    ParsedArg arg{.id = 3, .index = 1, .next_index = 2, .spelling = "-o", .values = {}};
    arg.add_value("out");
    ASSERT(arg.values.size() == 1U);
    EXPECT(arg.values[0] == "out");

    arg.clear();
    EXPECT(arg.id == 0U);
    EXPECT(arg.index == 0U);
    EXPECT(arg.next_index == 0U);
    EXPECT(arg.spelling.empty());
    EXPECT(arg.values.empty());
}

ZEST_CASE(prefixes_start_with_the_one_written) {
    EXPECT(pfx_none.empty());
    EXPECT(std::vector(pfx_dash.begin(), pfx_dash.end()) == std::vector<std::string_view>{"-"});
    EXPECT(std::vector(pfx_double.begin(), pfx_double.end()) ==
           std::vector<std::string_view>{"--"});
    EXPECT(std::vector(pfx_dash_double.begin(), pfx_dash_double.end()) ==
           std::vector<std::string_view>{"-", "--"});
    EXPECT(std::vector(pfx_slash_dash.begin(), pfx_slash_dash.end()) ==
           std::vector<std::string_view>{"/", "-"});
    EXPECT(std::vector(pfx_all.begin(), pfx_all.end()) ==
           std::vector<std::string_view>{"--", "/", "-"});
}

};  // ZEST_SUITE(deco_option_arg)

}  // namespace

}  // namespace kota::option
