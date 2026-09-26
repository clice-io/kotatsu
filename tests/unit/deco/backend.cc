#include <expected>
#include <string>
#include <type_traits>
#include <vector>

#include "test_util.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {
namespace {

namespace option = ::kota::option;

constexpr decl::Category sharedCategory = {
    .exclusive = false,
    .name = "shared",
    .description = "all nested output options must be set together",
};

constexpr decl::Category versionCategory = {
    .exclusive = true,
    .name = "version",
    .description = "version-only mode",
};

constexpr decl::Category requestCategory = {
    .exclusive = true,
    .name = "request",
    .description = "request-only mode",
};

constexpr decl::Category topCategory = {
    .exclusive = false,
    .name = "top",
    .description = "top config group",
};

constexpr decl::Category innerCategory = {
    .exclusive = false,
    .name = "inner",
    .description = "nested config group",
};

constexpr decl::Category inputCategory = {
    .exclusive = false,
    .name = "input",
    .description = "single positional input",
};

constexpr decl::Category trailingCategory = {
    .exclusive = false,
    .name = "trailing",
    .description = "arguments after --",
};

struct NestedOpt {
    DecoKV(help = "output"; required = false; category = sharedCategory;)
    <std::string> out_path;
    DecoComma(names = {"--T"}; required = false; category = sharedCategory;)
    <std::vector<std::string>> tags;
};

struct ParseAllOpt {
    DECO_CFG_START(required = false);
    DecoFlag(names = {"-V", "--version"}; required = false; category = versionCategory;)
    verbose;
    DecoInput(help = "input"; required = false;)
    <std::string> input;
    DecoKVStyled(decl::KVStyle::Joined, required = false; category = sharedCategory;)
    <int> opt;
    DECO_CFG_END();

    DECO_CFG(category = sharedCategory; required = false;);
    NestedOpt nested;
    DecoMulti(2, {
        names = {"-P", "--pair"};
        required = false;
        category = sharedCategory;
    })
    <std::vector<std::string>> pair;
};

struct ParsePackOpt {
    DecoFlag()
    d;
    DecoPack(help = "pack"; required = false;)
    <std::vector<std::string>> pack = {};
};

struct InputThenPackOpt {
    DecoInput(required = false; category = inputCategory;)
    <std::string> input;
    DecoPack(required = false; category = trailingCategory;)
    <std::vector<std::string>> pack;
};

struct PackThenInputOpt {
    DecoPack(required = false; category = trailingCategory;)
    <std::vector<std::string>> pack;
    DecoInput(required = false; category = inputCategory;)
    <std::string> input;
};

struct RequiredOpt {
    DecoKV(required = true; help = "required integer option";)
    <int> must;
};

struct KVSplitStyleByNameOpt {
    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, names = {"--test=", "--a"}; required = false;)
    <int> level;
};

struct KVDefaultNameSplitStyleOpt {
    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, required = false;)
    <int> level;
};

struct KVAliasSplitStyleByNameOpt {
    DecoKVAliasStyled(decl::KVStyle::JoinedOrSeparate, names = {"--target=", "--target-alias"};
                      required = false;
                      forward = std::vector<std::string_view>{"--target"};) _;
};

struct KVAliasDefaultNameSplitStyleOpt {
    DecoKVAliasStyled(decl::KVStyle::JoinedOrSeparate, required = false;
                      forward = std::vector<std::string_view>{"--target"};) target_alias;
};

struct DeepCfgInner {
    DECO_CFG_START(required = false; category = innerCategory;);
    DecoKV()
    <int> a;
    DECO_CFG_END();
};

struct DeepCfgOpt {
    DECO_CFG_START(required = false; category = topCategory;);
    DecoKV()
    <int> top;
    DECO_CFG_START(required = false; category = innerCategory;);
    DeepCfgInner inner;
    DecoKV()
    <int> mid;
    DECO_CFG_END();
    DecoKV()
    <int> tail;
    DECO_CFG_END();
};

struct NextScopedNested {
    DecoKV()
    <int> left;
    DecoKV()
    <int> right;
};

struct NextOnNestedOpt {
    DECO_CFG(required = false; category = sharedCategory;);
    NextScopedNested nested;
    DecoKV(required = false;)
    <int> tail;
};

struct ExclusiveCategoryOpt {
    DecoFlag(required = false; category = versionCategory;)
    version;
    DecoFlag(required = false; category = sharedCategory;)
    shared;
};

struct MultiExclusiveCategoryOpt {
    DecoFlag(required = false; category = versionCategory;)
    version;
    DecoFlag(required = false; category = requestCategory;)
    request;
};

auto backend_alias_forward_fn(const deco::ParsedArgOwning&)
    -> std::expected<std::vector<std::string>, std::string> {
    return std::vector<std::string>{"--pair", "left", "right"};
}

struct AliasBackendOpt {
    DecoFlagAlias(names = {"-O1", "--optimize-one"}; required = false; category = versionCategory;
                  forward = {"--optimize", "1"};) _;

    DecoKVAlias(names = {"--define-alias", "--define-alias-alt"}; required = false;
                category = sharedCategory;
                forward = std::vector<std::string_view>{"--define"};) __;

    DecoMultiAlias(2, names = {"--pair-alias", "--pair-alias-alt"}; required = false;
                   category = requestCategory;
                   forward = backend_alias_forward_fn;) ___;
};

using ParseAllStorage = std::remove_cvref_t<decltype(detail::build_storage<ParseAllOpt>())>;
static_assert(
    std::is_base_of_v<detail::DecoStructConsumer<ParseAllStorage, ParseAllOpt>, ParseAllStorage>);
static_assert(!std::is_copy_constructible_v<ParseAllStorage>);
static_assert(!std::is_move_constructible_v<ParseAllStorage>);
static_assert(std::is_same_v<
              ParseAllStorage,
              detail::LLVMOptGenerator<ParseAllOpt, detail::BuildStorage<ParseAllOpt>::record>>);

using Parsed = option::ParsedArg;
using option::test::ParsedArgs;
using option::test::parse_with;

ZEST_SUITE(deco_backend){

    ZEST_CASE(storage_keeps_dummy_alignment_for_id_map){
        const auto& built = detail::build_storage<ParseAllOpt>();

EXPECT(built.opt_size() > 1);
EXPECT(built.id_map().size() == built.option_infos().size() + 1);
EXPECT(built.category_map().size() == built.id_map().size());
EXPECT(built.id_map()[0] == nullptr);
EXPECT(built.category_map()[0] == nullptr);
EXPECT(built.option_infos().size() == built.opt_size());
for(size_t i = 0; i < built.option_infos().size(); ++i) {
    EXPECT(built.option_infos()[i].id == i + 1);
    if(built.option_infos()[i].kind == option::Kind::Unknown) {
        EXPECT(built.id_map()[i + 1] == nullptr);
        EXPECT(built.category_map()[i + 1] == nullptr);
    } else {
        EXPECT(built.id_map()[i + 1] != nullptr);
        EXPECT(built.category_map()[i + 1] != nullptr);
    }
}

}  // namespace

ZEST_CASE(parse_covers_flag_input_kv_comma_multi) {
    const auto& built = detail::build_storage<ParseAllOpt>();
    std::vector<std::string> argv = {"--version",
                                     "--opt42",
                                     "--out-path",
                                     "a.out",
                                     "--T,x,y",
                                     "-P",
                                     "left",
                                     "right",
                                     "main.cc",
                                     "--",
                                     "tail1",
                                     "tail2"};

    auto parsed_args = parse_with(built, argv);
    EXPECT(parsed_args.has_value());
    if(!parsed_args.has_value()) {
        return;
    }
    auto args = std::move(parsed_args.value());
    ParseAllOpt opt{};

    EXPECT(args.size() == 9);
    if(args.size() != 9) {
        return;
    }

    EXPECT(args[0].spelling == "--version");
    EXPECT(args[0].values.empty());
    EXPECT(built.field_ptr_of(args[0].id, opt) == static_cast<void*>(&opt.verbose));

    EXPECT(args[1].spelling == "--opt");
    EXPECT(args[1].values.size() == 1);
    EXPECT(args[1].values[0] == "42");
    EXPECT(built.field_ptr_of(args[1].id, opt) == static_cast<void*>(&opt.opt));

    EXPECT(args[2].spelling == "--out-path");
    EXPECT(args[2].values.size() == 1);
    EXPECT(args[2].values[0] == "a.out");
    EXPECT(built.field_ptr_of(args[2].id, opt) == static_cast<void*>(&opt.nested.out_path));
    EXPECT((built.category_of(args[2].id) == &sharedCategory));

    EXPECT(args[3].spelling == "--T");
    EXPECT(args[3].values.size() == 2);
    EXPECT(args[3].values[0] == "x");
    EXPECT(args[3].values[1] == "y");
    EXPECT(built.field_ptr_of(args[3].id, opt) == static_cast<void*>(&opt.nested.tags));

    EXPECT(args[4].spelling == "-P");
    EXPECT(args[4].values.size() == 2);
    EXPECT(args[4].values[0] == "left");
    EXPECT(args[4].values[1] == "right");
    EXPECT(built.field_ptr_of(args[4].id, opt) == static_cast<void*>(&opt.pair));

    EXPECT(args[5].spelling == "main.cc");
    EXPECT(args[5].values.empty());
    EXPECT(built.field_ptr_of(args[5].id, opt) == static_cast<void*>(&opt.input));

    EXPECT(args[6].spelling == "--");
    EXPECT(args[6].values.empty());
    EXPECT(built.field_ptr_of(args[6].id, opt) == nullptr);
    EXPECT(built.category_of(args[6].id) == nullptr);

    EXPECT(args[7].spelling == "tail1");
    EXPECT(args[7].values.empty());
    EXPECT(built.field_ptr_of(args[7].id, opt) == static_cast<void*>(&opt.input));

    EXPECT(args[8].spelling == "tail2");
    EXPECT(args[8].values.empty());
    EXPECT(built.field_ptr_of(args[8].id, opt) == static_cast<void*>(&opt.input));
}

ZEST_CASE(parse_pack_covers_trailing_input_option) {
    const auto& built = detail::build_storage<ParsePackOpt>();
    std::vector<std::string> argv = {"-d", "--", "a", "b", "c"};

    auto parsed_args = parse_with(built, argv);
    EXPECT(parsed_args.has_value());
    if(!parsed_args.has_value()) {
        return;
    }
    auto args = std::move(parsed_args.value());
    ParsePackOpt opt{};

    EXPECT(args.size() == 2);

    EXPECT(args[0].spelling == "-d");
    EXPECT(args[0].values.empty());
    EXPECT(built.field_ptr_of(args[0].id, opt) == static_cast<void*>(&opt.d));

    EXPECT(args[1].spelling == "--");
    EXPECT(args[1].values.size() == 3);
    EXPECT(args[1].values[0] == "a");
    EXPECT(args[1].values[1] == "b");
    EXPECT(args[1].values[2] == "c");
    EXPECT(built.field_ptr_of(args[1].id, opt) == static_cast<void*>(&opt.pack));
}

ZEST_CASE(parse_input_and_pack_can_coexist) {
    const auto& built = detail::build_storage<InputThenPackOpt>();
    auto parsed_args = parse_with(built, {"front", "--", "a", "b"});
    EXPECT(parsed_args.has_value());
    if(!parsed_args.has_value()) {
        return;
    }
    auto args = std::move(parsed_args.value());
    InputThenPackOpt opt{};

    EXPECT(args.size() == 2);
    EXPECT(!built.is_trailing_argument(args[0]));
    EXPECT(built.field_ptr_of(args[0].id, opt) == static_cast<void*>(&opt.input));
    EXPECT((built.category_of(args[0].id) == &inputCategory));

    EXPECT(built.is_trailing_argument(args[1]));
    EXPECT(built.trailing_ptr_of(opt) == static_cast<void*>(&opt.pack));
    EXPECT((built.trailing_category() == &trailingCategory));
}

ZEST_CASE(parse_pack_then_input_rebinds_input_id_map) {
    const auto& built = detail::build_storage<PackThenInputOpt>();
    auto parsed_args = parse_with(built, {"front", "--", "a", "b"});
    EXPECT(parsed_args.has_value());
    if(!parsed_args.has_value()) {
        return;
    }
    auto args = std::move(parsed_args.value());
    PackThenInputOpt opt{};

    EXPECT(args.size() == 2);
    EXPECT(!built.is_trailing_argument(args[0]));
    EXPECT(built.field_ptr_of(args[0].id, opt) == static_cast<void*>(&opt.input));
    EXPECT((built.category_of(args[0].id) == &inputCategory));

    EXPECT(built.is_trailing_argument(args[1]));
    EXPECT(built.field_ptr_of(args[1].id, opt) == static_cast<void*>(&opt.input));
    EXPECT(built.trailing_ptr_of(opt) == static_cast<void*>(&opt.pack));
    EXPECT((built.trailing_category() == &trailingCategory));
}

ZEST_CASE(parse_kv_supports_joined_and_separate_styles) {
    const auto& built = detail::build_storage<KVSplitStyleByNameOpt>();
    EXPECT(built.option_infos().size() == 3);
    EXPECT(built.option_infos()[1].kind == option::Kind::Joined);
    EXPECT(built.option_infos()[2].kind == option::Kind::Separate);

    auto joined_args = parse_with(built, {"--test=42"});
    EXPECT(joined_args.has_value());
    if(!joined_args.has_value()) {
        return;
    }
    EXPECT(joined_args->size() == 1);
    if(joined_args->size() != 1) {
        return;
    }
    EXPECT((*joined_args)[0].spelling == "--test=");
    EXPECT((*joined_args)[0].values.size() == 1);
    EXPECT((*joined_args)[0].values[0] == "42");

    auto separate_args = parse_with(built, {"--a", "7"});
    EXPECT(separate_args.has_value());
    if(!separate_args.has_value()) {
        return;
    }
    EXPECT(separate_args->size() == 1);
    if(separate_args->size() != 1) {
        return;
    }
    EXPECT((*separate_args)[0].spelling == "--a");
    EXPECT((*separate_args)[0].values.size() == 1);
    EXPECT((*separate_args)[0].values[0] == "7");
}

ZEST_CASE(parse_kv_default_name_adds_joined_equals_alias_when_style_includes_joined) {
    const auto& built = detail::build_storage<KVDefaultNameSplitStyleOpt>();
    EXPECT(built.option_infos().size() == 3);
    EXPECT(built.option_infos()[1].kind == option::Kind::Separate);
    EXPECT(built.option_infos()[2].kind == option::Kind::Joined);

    auto separate_args = parse_with(built, {"--level", "7"});
    EXPECT(separate_args.has_value());
    if(!separate_args.has_value()) {
        return;
    }
    EXPECT(separate_args->size() == 1);
    if(separate_args->size() != 1) {
        return;
    }
    EXPECT((*separate_args)[0].spelling == "--level");
    EXPECT((*separate_args)[0].values.size() == 1);
    EXPECT((*separate_args)[0].values[0] == "7");

    auto joined_args = parse_with(built, {"--level=42"});
    EXPECT(joined_args.has_value());
    if(!joined_args.has_value()) {
        return;
    }
    EXPECT(joined_args->size() == 1);
    if(joined_args->size() != 1) {
        return;
    }
    EXPECT((*joined_args)[0].spelling == "--level=");
    EXPECT((*joined_args)[0].values.size() == 1);
    EXPECT((*joined_args)[0].values[0] == "42");
}

ZEST_CASE(parse_kv_alias_supports_joined_and_separate_styles) {
    const auto& built = detail::build_storage<KVAliasSplitStyleByNameOpt>();
    EXPECT(built.option_infos().size() == 3);
    EXPECT(built.option_infos()[1].kind == option::Kind::Joined);
    EXPECT(built.option_infos()[2].kind == option::Kind::Separate);

    auto joined_args = parse_with(built, {"--target=42"});
    EXPECT(joined_args.has_value());
    if(!joined_args.has_value()) {
        return;
    }
    EXPECT(joined_args->size() == 1);
    if(joined_args->size() != 1) {
        return;
    }
    EXPECT((*joined_args)[0].spelling == "--target=");
    EXPECT((*joined_args)[0].values.size() == 1);
    EXPECT((*joined_args)[0].values[0] == "42");

    auto separate_args = parse_with(built, {"--target-alias", "7"});
    EXPECT(separate_args.has_value());
    if(!separate_args.has_value()) {
        return;
    }
    EXPECT(separate_args->size() == 1);
    if(separate_args->size() != 1) {
        return;
    }
    EXPECT((*separate_args)[0].spelling == "--target-alias");
    EXPECT((*separate_args)[0].values.size() == 1);
    EXPECT((*separate_args)[0].values[0] == "7");
}

ZEST_CASE(parse_kv_alias_default_name_adds_joined_equals_alias_when_style_includes_joined) {
    const auto& built = detail::build_storage<KVAliasDefaultNameSplitStyleOpt>();
    EXPECT(built.option_infos().size() == 3);
    EXPECT(built.option_infos()[1].kind == option::Kind::Separate);
    EXPECT(built.option_infos()[2].kind == option::Kind::Joined);

    auto separate_args = parse_with(built, {"--target-alias", "7"});
    EXPECT(separate_args.has_value());
    if(!separate_args.has_value()) {
        return;
    }
    EXPECT(separate_args->size() == 1);
    if(separate_args->size() != 1) {
        return;
    }
    EXPECT((*separate_args)[0].spelling == "--target-alias");
    EXPECT((*separate_args)[0].values.size() == 1);
    EXPECT((*separate_args)[0].values[0] == "7");

    auto joined_args = parse_with(built, {"--target-alias=42"});
    EXPECT(joined_args.has_value());
    if(!joined_args.has_value()) {
        return;
    }
    EXPECT(joined_args->size() == 1);
    if(joined_args->size() != 1) {
        return;
    }
    EXPECT((*joined_args)[0].spelling == "--target-alias=");
    EXPECT((*joined_args)[0].values.size() == 1);
    EXPECT((*joined_args)[0].values[0] == "42");
}

ZEST_CASE(category_map_assigns_expected_categories_for_parsed_args) {
    const auto& built = detail::build_storage<ParseAllOpt>();
    auto parsed_args = parse_with(built,
                                  {"--version",
                                   "--opt1",
                                   "main.cc",
                                   "--out-path",
                                   "a.out",
                                   "--T,x,y",
                                   "-P",
                                   "left",
                                   "right"});
    EXPECT(parsed_args.has_value());
    if(!parsed_args.has_value()) {
        return;
    }
    const auto& args = parsed_args.value();
    EXPECT(args.size() == 6);

    std::size_t default_count = 0;
    std::size_t shared_count = 0;
    std::size_t version_count = 0;
    for(const auto& arg: args) {
        const auto* category = built.category_of(arg.id);
        EXPECT(category != nullptr);
        const auto spelling = arg.spelling;
        if(spelling == "--version") {
            EXPECT((category == &versionCategory));
            version_count += 1;
        } else if(spelling == "main.cc") {
            EXPECT((category == &decl::default_category));
            default_count += 1;
        } else {
            EXPECT((category == &sharedCategory));
            shared_count += 1;
        }
    }
    EXPECT(default_count == 1);
    EXPECT(shared_count == 4);
    EXPECT(version_count == 1);
}

ZEST_CASE(category_map_keeps_alias_category_consistent) {
    const auto& built = detail::build_storage<ParseAllOpt>();
    auto short_args = parse_with(built, {"-V"});
    auto long_args = parse_with(built, {"--version"});
    EXPECT(short_args.has_value());
    EXPECT(long_args.has_value());
    if(!short_args.has_value() || !long_args.has_value()) {
        return;
    }
    EXPECT(short_args->size() == 1);
    EXPECT(long_args->size() == 1);
    if(short_args->size() != 1 || long_args->size() != 1) {
        return;
    }
    EXPECT((built.category_of((*short_args)[0].id) == &versionCategory));
    EXPECT((built.category_of((*long_args)[0].id) == &versionCategory));
}

ZEST_CASE(category_map_supports_deep_nested_cfg_areas) {
    const auto& built = detail::build_storage<DeepCfgOpt>();
    auto parsed_args = parse_with(built, {"--top", "1", "--tail", "2", "-a", "3", "--mid", "4"});
    EXPECT(parsed_args.has_value());
    if(!parsed_args.has_value()) {
        return;
    }
    const auto& args = parsed_args.value();
    EXPECT(args.size() == 4);
    std::size_t top_count = 0;
    std::size_t inner_count = 0;
    for(const auto& arg: args) {
        const auto* category = built.category_of(arg.id);
        EXPECT(category != nullptr);
        const auto spelling = arg.spelling;
        if(spelling == "--top" || spelling == "--tail") {
            EXPECT((category == &topCategory));
            top_count += 1;
        } else if(spelling == "-a" || spelling == "--mid") {
            EXPECT((category == &innerCategory));
            inner_count += 1;
        }
    }
    EXPECT(top_count == 2);
    EXPECT(inner_count == 2);
}

ZEST_CASE(category_map_supports_multiple_exclusive_category_definitions) {
    const auto& built = detail::build_storage<MultiExclusiveCategoryOpt>();
    auto parsed_args = parse_with(built, {"--version", "--request"});
    EXPECT(parsed_args.has_value());
    if(!parsed_args.has_value()) {
        return;
    }
    EXPECT(parsed_args->size() == 2);
    if(parsed_args->size() != 2) {
        return;
    }
    EXPECT((built.category_of((*parsed_args)[0].id) == &versionCategory));
    EXPECT((built.category_of((*parsed_args)[1].id) == &requestCategory));
}

ZEST_CASE(alias_entries_have_backend_metadata_without_accessor) {
    const auto& built = detail::build_storage<AliasBackendOpt>();

    auto parsed = parse_with(
        built,
        {"--optimize-one", "--define-alias-alt", "NAME=VALUE", "--pair-alias-alt", "a", "b"});
    EXPECT(parsed.has_value());
    if(!parsed.has_value()) {
        return;
    }

    AliasBackendOpt opt{};
    EXPECT(parsed->size() == 3);
    if(parsed->size() != 3) {
        return;
    }

    const auto* flag_meta = built.alias_meta_of((*parsed)[0].id);
    using alias_meta_t = std::remove_cvref_t<decltype(*flag_meta)>;
    EXPECT(flag_meta != nullptr);
    EXPECT(built.is_alias_option_id((*parsed)[0].id));
    EXPECT(built.field_ptr_of((*parsed)[0].id, opt) == nullptr);
    EXPECT((built.category_of((*parsed)[0].id) == &versionCategory));
    EXPECT(flag_meta->kind == alias_meta_t::Kind::Flag);
    EXPECT(flag_meta->forward_kind == decl::AliasForwardField::Kind::Static);
    EXPECT(flag_meta->static_tokens.size() == 2);
    EXPECT(flag_meta->static_tokens[0] == "--optimize");
    EXPECT(flag_meta->static_tokens[1] == "1");

    const auto* kv_meta = built.alias_meta_of((*parsed)[1].id);
    EXPECT(kv_meta != nullptr);
    EXPECT(built.field_ptr_of((*parsed)[1].id, opt) == nullptr);
    EXPECT((built.category_of((*parsed)[1].id) == &sharedCategory));
    EXPECT(kv_meta->kind == alias_meta_t::Kind::KV);
    EXPECT(kv_meta->forward_kind == decl::AliasForwardField::Kind::Static);
    EXPECT(kv_meta->static_tokens.size() == 1);
    EXPECT(kv_meta->static_tokens[0] == "--define");

    const auto* multi_meta = built.alias_meta_of((*parsed)[2].id);
    EXPECT(multi_meta != nullptr);
    EXPECT(built.field_ptr_of((*parsed)[2].id, opt) == nullptr);
    EXPECT((built.category_of((*parsed)[2].id) == &requestCategory));
    EXPECT(multi_meta->kind == alias_meta_t::Kind::Multi);
    EXPECT(multi_meta->forward_kind == decl::AliasForwardField::Kind::Dynamic);
    EXPECT(multi_meta->dynamic != nullptr);
    EXPECT(multi_meta->arg_num == 2);
}

ZEST_CASE(visit_fields_applies_next_cfg_to_nested_struct_fields) {
    const auto& built = detail::build_storage<NextOnNestedOpt>();

    auto partial_nested_args = parse_with(built, {"--left", "1"});
    EXPECT(partial_nested_args.has_value());
    if(!partial_nested_args.has_value()) {
        return;
    }

    EXPECT(!partial_nested_args.value().empty());
    EXPECT((built.category_of(partial_nested_args.value()[0].id) == &sharedCategory));

    NextOnNestedOpt default_opt{};
    std::size_t nested_cfg_count = 0;
    std::size_t tail_cfg_count = 0;
    const bool visited =
        built.visit_fields(default_opt,
                           [&](const auto&, const auto& cfg, std::string_view field_name, auto) {
                               if(field_name == "left" || field_name == "right") {
                                   EXPECT(cfg.required == false);
                                   EXPECT((cfg.category.ptr() == &sharedCategory));
                                   nested_cfg_count += 1;
                               }
                               if(field_name == "tail") {
                                   EXPECT(cfg.required == false);
                                   EXPECT((cfg.category.ptr() == &decl::default_category));
                                   tail_cfg_count += 1;
                               }
                               return true;
                           });
    EXPECT(visited);
    EXPECT(nested_cfg_count == 2);
    EXPECT(tail_cfg_count == 1);
}

};  // namespace kota::deco

}  // namespace
}  // namespace kota::deco
