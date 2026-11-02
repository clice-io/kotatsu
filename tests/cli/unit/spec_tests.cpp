// What a field's cli:: attributes declare.

#include <string_view>

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
    constexpr const auto* spec = meta::spec_in<ArgSpec, meta::field_attrs_t<Fields, 0>>;
    ZSTATIC_EXPECT(spec->names.size() == 2U);
    ZSTATIC_EXPECT(spec->names[1] == "--out");
    ZSTATIC_EXPECT(spec->meta_var == "<FILE>");
    ZSTATIC_EXPECT(!spec->positional);
}

ZEST_CASE(positional_takes_its_count) {
    constexpr const auto* spec = meta::spec_in<ArgSpec, meta::field_attrs_t<Fields, 1>>;
    ZSTATIC_EXPECT(spec->positional);
    ZSTATIC_EXPECT(spec->arg_num == ArgNum(1, 2));
}

ZEST_CASE(field_without_attributes_has_no_spec) {
    ZSTATIC_EXPECT(meta::spec_in<ArgSpec, meta::field_attrs_t<Fields, 2>> == nullptr);
}

ZEST_CASE(flags_fold_into_the_spec) {
    constexpr const auto* spec = meta::spec_in<ArgSpec, meta::field_attrs_t<Fields, 3>>;
    ZSTATIC_EXPECT(spec->hidden);
    ZSTATIC_EXPECT(spec->stop);
    ZSTATIC_EXPECT(spec->names.empty());
}

};  // ZEST_SUITE(cli_spec)

}  // namespace

}  // namespace kotatsu::cli
