// What a field's cli:: entries declare.

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
    KANNOTATE(&help, cli::help, cli::hidden = true)
    bool help = false;
};

struct Helped {
    KANNOTATE(&help, cli::help)
    bool help = false;
};

ZEST_SUITE(cli_spec) {

ZEST_CASE(entries_fold_into_the_spec) {
    constexpr const auto* spec = codec::extension_of<ArgSpec, Fields, 0>;
    ZSTATIC_EXPECT(spec->names.size() == 2U);
    ZSTATIC_EXPECT(spec->names[1] == "--out");
    ZSTATIC_EXPECT(spec->meta_var == "<FILE>");
    ZSTATIC_EXPECT(!spec->positional);
}

ZEST_CASE(positional_takes_its_count) {
    constexpr const auto* spec = codec::extension_of<ArgSpec, Fields, 1>;
    ZSTATIC_EXPECT(spec->positional);
    ZSTATIC_EXPECT(spec->arg_num == ArgNum(1, 2));
}

ZEST_CASE(field_without_entries_has_no_spec) {
    ZSTATIC_EXPECT(codec::extension_of<ArgSpec, Fields, 2> == nullptr);
}

ZEST_CASE(help_option_stops_and_is_not_required) {
    constexpr const auto* spec = codec::extension_of<ArgSpec, Helped, 0>;
    ZSTATIC_EXPECT(spec->stop);
    // It leaves the presence to cli's format, under which a field is optional.
    constexpr const auto& info =
        static_cast<const codec::struct_type_info&>(codec::type_info_of<Helped, cli::format>());
    ZSTATIC_EXPECT(info.fields[0].presence == codec::presence_kind::optional);
    ZSTATIC_EXPECT(spec->names[0] == "-h");
}

ZEST_CASE(annotated_help_option_keeps_its_names_and_stop) {
    constexpr const auto* spec = codec::extension_of<ArgSpec, Fields, 3>;
    ZSTATIC_EXPECT(spec->hidden);
    ZSTATIC_EXPECT(spec->stop);
    ZSTATIC_EXPECT(spec->names[1] == "--help");
}

};  // ZEST_SUITE(cli_spec)

}  // namespace

}  // namespace kotatsu::cli
