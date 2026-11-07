// What a field's cli:: attributes declare.

#include <string_view>
#include <type_traits>

#include "kotatsu/cli/cli.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::cli {

namespace {

struct Fields {
    KANNOTATE(&out, cli::names = {"-o", "--out"}, cli::meta_var = "<FILE>", description = "Out")
    std::string out;
    KANNOTATE(&input, cli::positional = true, cli::arg_num = ArgNum(1, 2))
    std::vector<int> input;
    std::string plain;
    KANNOTATE(&version, cli::stop = true, cli::hidden = true)
    bool version = false;
};

ZEST_SUITE(cli_spec) {

ZEST_CASE(attributes_fold_into_the_spec) {
    constexpr const ArgSpec& spec =
        meta::spec_attr_t<ArgSpec, meta::field_attrs_t<Fields, 0>>::value;
    ZSTATIC_EXPECT(spec.names.size() == 2U);
    ZSTATIC_EXPECT(spec.names[1] == "--out");
    ZSTATIC_EXPECT(spec.meta_var == "<FILE>");
    ZSTATIC_EXPECT(!spec.positional);
}

ZEST_CASE(positional_takes_its_count) {
    constexpr const ArgSpec& spec =
        meta::spec_attr_t<ArgSpec, meta::field_attrs_t<Fields, 1>>::value;
    ZSTATIC_EXPECT(spec.positional);
    ZSTATIC_EXPECT(spec.arg_num == ArgNum(1, 2));
}

ZEST_CASE(field_without_attributes_has_no_spec) {
    ZSTATIC_EXPECT(std::is_void_v<meta::spec_attr_t<ArgSpec, meta::field_attrs_t<Fields, 2>>>);
}

ZEST_CASE(flags_fold_into_the_spec) {
    constexpr const ArgSpec& spec =
        meta::spec_attr_t<ArgSpec, meta::field_attrs_t<Fields, 3>>::value;
    ZSTATIC_EXPECT(spec.hidden);
    ZSTATIC_EXPECT(spec.stop);
    ZSTATIC_EXPECT(spec.names.empty());
}

};  // ZEST_SUITE(cli_spec)

}  // namespace

}  // namespace kotatsu::cli
