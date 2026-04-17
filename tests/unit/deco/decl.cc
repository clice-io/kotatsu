#include <string>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {
namespace {

namespace option = ::kota::option;

enum class BuiltinEnumValue {
    Alpha,
    Beta,
    Gamma,
};

enum class BuiltinSpelledEnum {
    myValue,
    Delete_,
    V123,
};

static_assert(trait::ScalarResultType<bool>);
static_assert(trait::ScalarResultType<int>);
static_assert(trait::ScalarResultType<std::string>);
static_assert(trait::ScalarResultType<BuiltinEnumValue>);
static_assert(!trait::ScalarResultType<std::string_view>);
static_assert(!trait::ScalarResultType<const char*>);
static_assert(!trait::ScalarResultType<std::vector<int>>);

static_assert(trait::VectorResultType<std::vector<int>>);
static_assert(trait::VectorResultType<std::vector<std::string>>);
static_assert(trait::VectorResultType<std::vector<BuiltinEnumValue>>);
static_assert(!trait::VectorResultType<std::vector<std::string_view>>);
static_assert(!trait::VectorResultType<std::span<const std::string>>);

static_assert(trait::InputResultType<int>);
static_assert(trait::InputResultType<std::string>);
static_assert(trait::InputResultType<BuiltinEnumValue>);
static_assert(trait::InputResultType<std::vector<int>>);
static_assert(trait::InputResultType<std::vector<std::string>>);
static_assert(trait::InputResultType<std::vector<BuiltinEnumValue>>);
static_assert(!trait::InputResultType<std::string_view>);
static_assert(!trait::InputResultType<std::vector<std::string_view>>);
static_assert(!trait::InputResultType<const char*>);

constexpr decl::Category verboseCategory = {
    .exclusive = true,
    .name = "verbose",
    .description = "verbose-only mode",
};

constexpr decl::Category packCategory = {
    .exclusive = false,
    .name = "pack",
    .description = "pack option group",
};

struct CustomScalarResult {
    constexpr CustomScalarResult() = default;
    constexpr ~CustomScalarResult() = default;

    std::string value;

    std::optional<std::string> into(std::string_view input) {
        value = std::string(input);
        return std::nullopt;
    }
};

auto make_parsed_arg(std::string_view spelling, std::vector<std::string_view> values = {}) {
    return option::ParsedArgument{
        .option_id = option::OptSpecifier(1u),
        .spelling = spelling,
        .values = std::move(values),
        .index = 0,
    };
}

struct DeclOpt {
    DecoFlag({
        help = "flag";
        required = false;
        category = verboseCategory;
    })
    verbose = true;

    DECO_CFG(required = true);
    DecoInput(help = "input")
    <int> input = 42;

    DecoPack(help = "pack"; category = packCategory;)
    <std::vector<std::string>> pack = std::vector<std::string>{"a", "b"};

    DecoKVStyled(decl::KVStyle::Joined, help = "joined-kv";)
    <int> joined = 7;
    DecoKV(help = "separate-kv";)
    <std::string> path = "entry.js";
    DecoComma(help = "comma"; names = {"-T"};)
    <std::vector<std::string>> tags = std::vector<std::string>{"x", "y"};
    DecoMulti(2, help = "multi"; names = {"-P"};)
    <std::vector<int>> pair = std::vector<int>{1, 2};
};

auto alias_decl_forward_fn(const option::ParsedArgumentOwning&)
    -> std::expected<std::vector<std::string>, std::string> {
    return std::vector<std::string>{"--target", "value"};
}

auto alias_decl_forward_with_context_fn(const option::ParsedArgumentOwning&,
                                        const decl::IntoContext&)
    -> std::expected<std::vector<std::string>, std::string> {
    return std::vector<std::string>{"--target", "value"};
}

struct AliasDeclOpt {
    DecoFlagAlias(names = {"-O1"}; forward = {"--optimize", "1"};) _;

    DecoKVAlias(names = {"--define-alias"};
                forward = std::vector<std::string_view>{"--define"};) __;

    DecoCommaAlias(names = {"--tags-alias"}; forward = {"--tags"};) ___;

    DecoMultiAlias(2, names = {"--pair-alias"}; forward = alias_decl_forward_fn;) ____;

    DecoFlagAlias(names = {"--ctx"}; forward = alias_decl_forward_with_context_fn;) _____;
};

static_assert(std::is_same_v<decltype(DeclOpt{}.pack)::result_type, std::vector<std::string>>);
static_assert(std::is_base_of_v<decl::DecoOptionBase, decltype(DeclOpt{}.input)>);

TEST_SUITE(deco_decl) {

TEST_CASE(option_declaration_has_expected_shape_and_default_assignment) {
    DeclOpt opt{};

    using VerboseCfg = typename decltype(opt.verbose)::__deco_field_ty;
    VerboseCfg verbose_cfg{};
    EXPECT_TRUE(verbose_cfg.names.empty());
    EXPECT_TRUE(verbose_cfg.required == false);
    EXPECT_TRUE(verbose_cfg.category->exclusive == true);
    EXPECT_TRUE(verbose_cfg.category.ptr() == &verboseCategory);
    EXPECT_TRUE(opt.verbose.has_value());
    EXPECT_TRUE(opt.verbose.value() == true);
    opt.verbose = false;
    EXPECT_TRUE(opt.verbose.has_value());
    EXPECT_TRUE(opt.verbose.value() == false);

    EXPECT_TRUE(opt.input.has_value());
    EXPECT_TRUE(opt.input.value() == 42);
    opt.input = 64;
    EXPECT_TRUE(opt.input.value() == 64);

    using PackCfg = typename decltype(opt.pack)::__deco_field_ty;
    PackCfg pack_cfg{};
    EXPECT_TRUE(opt.pack.has_value());
    EXPECT_TRUE(opt.pack.value().size() == 2);
    EXPECT_TRUE(opt.pack.value()[0] == "a");
    EXPECT_TRUE(opt.pack.value()[1] == "b");
    EXPECT_TRUE(pack_cfg.category.ptr() == &packCategory);
    opt.pack = std::vector<std::string>{"tail"};
    EXPECT_TRUE(opt.pack.value().size() == 1);
    EXPECT_TRUE(opt.pack.value()[0] == "tail");

    using JoinedCfg = typename decltype(opt.joined)::__deco_field_ty;
    JoinedCfg joined_cfg{};
    EXPECT_TRUE(joined_cfg.style == decl::KVStyle::Joined);
    EXPECT_TRUE(opt.joined.has_value());
    EXPECT_TRUE(opt.joined.value() == 7);
    opt.joined = 11;
    EXPECT_TRUE(opt.joined.value() == 11);

    using PathCfg = typename decltype(opt.path)::__deco_field_ty;
    PathCfg path_cfg{};
    EXPECT_TRUE(path_cfg.style == decl::KVStyle::Separate);
    EXPECT_TRUE(opt.path.has_value());
    EXPECT_TRUE(opt.path.value() == "entry.js");
    opt.path = std::string("run.js");
    EXPECT_TRUE(opt.path.value() == "run.js");

    using TagsCfg = typename decltype(opt.tags)::__deco_field_ty;
    TagsCfg tags_cfg{};
    EXPECT_TRUE(tags_cfg.names.size() == 1);
    EXPECT_TRUE(tags_cfg.names[0] == "-T");
    EXPECT_TRUE(opt.tags.has_value());
    EXPECT_TRUE(opt.tags.value().size() == 2);
    EXPECT_TRUE(opt.tags.value()[0] == "x");
    EXPECT_TRUE(opt.tags.value()[1] == "y");
    opt.tags = std::vector<std::string>{"only"};
    EXPECT_TRUE(opt.tags.value().size() == 1);
    EXPECT_TRUE(opt.tags.value()[0] == "only");

    using PairCfg = typename decltype(opt.pair)::__deco_field_ty;
    PairCfg pair_cfg{};
    EXPECT_TRUE(pair_cfg.arg_num == 2);
    EXPECT_TRUE(pair_cfg.names.size() == 1);
    EXPECT_TRUE(pair_cfg.names[0] == "-P");
    EXPECT_TRUE(opt.pair.has_value());
    EXPECT_TRUE(opt.pair.value().size() == 2);
    EXPECT_TRUE(opt.pair.value()[0] == 1);
    EXPECT_TRUE(opt.pair.value()[1] == 2);
    opt.pair = std::vector<int>{9, 8};
    EXPECT_TRUE(opt.pair.value().size() == 2);
    EXPECT_TRUE(opt.pair.value()[0] == 9);
    EXPECT_TRUE(opt.pair.value()[1] == 8);
}

TEST_CASE(alias_declaration_has_expected_shape) {
    decl::FlagAliasFields flag_cfg{};
    flag_cfg.names = {"-O1"};
    flag_cfg.forward = {"--optimize", "1"};

    decl::KVAliasFields kv_cfg{};
    kv_cfg.names = {"--define-alias"};
    kv_cfg.forward = std::vector<std::string_view>{"--define"};

    decl::CommaJoinedAliasFields comma_cfg{};
    comma_cfg.names = {"--tags-alias"};
    comma_cfg.forward = {"--tags"};

    decl::MultiAliasFields multi_cfg{};
    multi_cfg.names = {"--pair-alias"};
    multi_cfg.arg_num = 2;
    multi_cfg.forward = alias_decl_forward_fn;

    decl::FlagAliasFields ctx_cfg{};
    ctx_cfg.names = {"--ctx"};
    ctx_cfg.forward = alias_decl_forward_with_context_fn;

    static_assert(!std::is_base_of_v<decl::DecoOptionBase, decl::FlagAliasFields>);

    EXPECT_TRUE(flag_cfg.names.size() == 1);
    EXPECT_TRUE(flag_cfg.names[0] == "-O1");
    EXPECT_TRUE(flag_cfg.forward.kind == decl::AliasForwardField::Kind::Static);
    EXPECT_TRUE(flag_cfg.forward.static_tokens.size() == 2);
    EXPECT_TRUE(flag_cfg.forward.static_tokens[0] == "--optimize");
    EXPECT_TRUE(flag_cfg.forward.static_tokens[1] == "1");

    EXPECT_TRUE(kv_cfg.forward.kind == decl::AliasForwardField::Kind::Static);
    EXPECT_TRUE(kv_cfg.forward.static_tokens.size() == 1);
    EXPECT_TRUE(kv_cfg.forward.static_tokens[0] == "--define");
    EXPECT_TRUE(kv_cfg.style == decl::KVStyle::Separate);

    EXPECT_TRUE(comma_cfg.forward.kind == decl::AliasForwardField::Kind::Static);
    EXPECT_TRUE(comma_cfg.forward.static_tokens.size() == 1);
    EXPECT_TRUE(comma_cfg.forward.static_tokens[0] == "--tags");

    EXPECT_TRUE(multi_cfg.forward.kind == decl::AliasForwardField::Kind::Dynamic);
    EXPECT_TRUE(multi_cfg.forward.dynamic != nullptr);
    EXPECT_TRUE(multi_cfg.arg_num == 2);

    EXPECT_TRUE(ctx_cfg.forward.kind == decl::AliasForwardField::Kind::DynamicWithContext);
    EXPECT_TRUE(ctx_cfg.forward.dynamic_with_context != nullptr);
}

TEST_CASE(option_into_assigns_values_by_option_kind) {
    decl::FlagOption<bool> flag{};
    auto flag_ok = flag.into(make_parsed_arg("--verbose"));
    EXPECT_TRUE(!flag_ok.has_value());
    EXPECT_TRUE(flag.has_value());
    EXPECT_TRUE(flag.value() == true);
    auto flag_err = flag.into(make_parsed_arg("--verbose", {"1"}));
    EXPECT_TRUE(flag_err.has_value());

    decl::ScalarOption<int> scalar{};
    auto scalar_ok = scalar.into(make_parsed_arg("--count", {"42"}));
    EXPECT_TRUE(!scalar_ok.has_value());
    EXPECT_TRUE(scalar.has_value());
    EXPECT_TRUE(scalar.value() == 42);
    auto scalar_err = scalar.into(make_parsed_arg("--count", {"not-a-number"}));
    EXPECT_TRUE(scalar_err.has_value());

    decl::InputOption<int> input{};
    auto input_ok = input.into(make_parsed_arg("123"));
    EXPECT_TRUE(!input_ok.has_value());
    EXPECT_TRUE(input.has_value());
    EXPECT_TRUE(input.value() == 123);
    auto input_err = input.into(make_parsed_arg("bad-int"));
    EXPECT_TRUE(input_err.has_value());

    decl::InputOption<std::vector<int>> input_vector{};
    auto input_vector_ok = input_vector.into(make_parsed_arg("7"));
    EXPECT_TRUE(!input_vector_ok.has_value());
    EXPECT_TRUE(input_vector.has_value());
    EXPECT_TRUE(input_vector.value() == std::vector<int>{7});
    auto input_vector_ok2 = input_vector.into(make_parsed_arg("8"));
    EXPECT_TRUE(!input_vector_ok2.has_value());
    EXPECT_TRUE(input_vector.value() == std::vector<int>{7, 8});
    auto input_vector_err = input_vector.into(make_parsed_arg("bad-int"));
    EXPECT_TRUE(input_vector_err.has_value());
    EXPECT_TRUE(input_vector.value() == std::vector<int>{7, 8});

    decl::ScalarOption<float> float_opt{};
    auto float_ok = float_opt.into(make_parsed_arg("--ratio", {"3.14"}));
    EXPECT_TRUE(!float_ok.has_value());
    EXPECT_TRUE(float_opt.has_value());
    EXPECT_TRUE(*float_opt > 3.13f && *float_opt < 3.15f);
    auto float_err = float_opt.into(make_parsed_arg("--ratio", {"3.14x"}));
    EXPECT_TRUE(float_err.has_value());

    decl::ScalarOption<BuiltinEnumValue> enum_opt{};
    auto enum_ok = enum_opt.into(make_parsed_arg("--mode", {"Beta"}));
    EXPECT_TRUE(!enum_ok.has_value());
    EXPECT_TRUE(enum_opt.has_value());
    EXPECT_TRUE(enum_opt.value() == BuiltinEnumValue::Beta);
    auto enum_err = enum_opt.into(make_parsed_arg("--mode", {"Delta"}));
    EXPECT_TRUE(enum_err.has_value());
    EXPECT_TRUE(enum_err->contains("invalid enum value: Delta"));
    EXPECT_TRUE(enum_err->contains("supported: alpha, beta, gamma"));

    decl::InputOption<BuiltinEnumValue> enum_input{};
    auto enum_input_ok = enum_input.into(make_parsed_arg("Gamma"));
    EXPECT_TRUE(!enum_input_ok.has_value());
    EXPECT_TRUE(enum_input.has_value());
    EXPECT_TRUE(enum_input.value() == BuiltinEnumValue::Gamma);

    decl::ScalarOption<BuiltinSpelledEnum> spelled_enum{};
    EXPECT_TRUE(!spelled_enum.into(make_parsed_arg("--kind", {"my_value"})).has_value());
    EXPECT_TRUE(spelled_enum.value() == BuiltinSpelledEnum::myValue);
    EXPECT_TRUE(!spelled_enum.into(make_parsed_arg("--kind", {"Delete"})).has_value());
    EXPECT_TRUE(spelled_enum.value() == BuiltinSpelledEnum::Delete_);
    EXPECT_TRUE(!spelled_enum.into(make_parsed_arg("--kind", {"123"})).has_value());
    EXPECT_TRUE(spelled_enum.value() == BuiltinSpelledEnum::V123);

    decl::ScalarOption<double> double_opt{};
    auto double_err = double_opt.into(make_parsed_arg("--precise", {"3.14"}));
    EXPECT_FALSE(double_err.has_value());
    EXPECT_TRUE(double_opt.value() == 3.14);

    decl::VectorOption<std::vector<int>> vector_opt{};
    auto vector_ok = vector_opt.into(make_parsed_arg("-P", {"7", "8"}));
    EXPECT_TRUE(!vector_ok.has_value());
    EXPECT_TRUE(vector_opt.has_value());
    EXPECT_TRUE(vector_opt.value().size() == 2);
    EXPECT_TRUE(vector_opt.value()[0] == 7);
    EXPECT_TRUE(vector_opt.value()[1] == 8);
    auto vector_err = vector_opt.into(make_parsed_arg("-P", {"7", "x"}));
    EXPECT_TRUE(vector_err.has_value());

    decl::VectorOption<std::vector<BuiltinEnumValue>> enum_vector{};
    auto enum_vector_ok = enum_vector.into(make_parsed_arg("-M", {"Alpha", "Gamma"}));
    EXPECT_TRUE(!enum_vector_ok.has_value());
    EXPECT_TRUE(enum_vector.has_value());
    EXPECT_TRUE(enum_vector.value().size() == 2);
    EXPECT_TRUE(enum_vector.value()[0] == BuiltinEnumValue::Alpha);
    EXPECT_TRUE(enum_vector.value()[1] == BuiltinEnumValue::Gamma);
    auto enum_vector_err = enum_vector.into(make_parsed_arg("-M", {"Alpha", "Delta"}));
    EXPECT_TRUE(enum_vector_err.has_value());
    EXPECT_TRUE(enum_vector_err->contains("invalid vector value at index 1"));
    EXPECT_TRUE(enum_vector_err->contains("invalid enum value: Delta"));
    EXPECT_TRUE(enum_vector_err->contains("supported: alpha, beta, gamma"));

    decl::ScalarOption<CustomScalarResult> custom_scalar{};
    auto custom_scalar_ok = custom_scalar.into(make_parsed_arg("--name", {"alice"}));
    EXPECT_TRUE(!custom_scalar_ok.has_value());
    EXPECT_TRUE(custom_scalar.has_value());
    EXPECT_TRUE(custom_scalar->value == "alice");

    struct CustomVectorResult {
        // constexpr CustomVectorResult() = default;
        // constexpr ~CustomVectorResult() = default;

        std::vector<std::string> values;

        std::optional<std::string> into(const std::vector<std::string_view>& input) {
            values.assign(input.begin(), input.end());
            return std::nullopt;
        }
    };

    decl::VectorOption<CustomVectorResult> custom_vector{};
    auto custom_vector_ok = custom_vector.into(make_parsed_arg("--tags", {"x", "y"}));
    EXPECT_TRUE(!custom_vector_ok.has_value());
    EXPECT_TRUE(custom_vector.has_value());
    EXPECT_TRUE(custom_vector->values.size() == 2);
    EXPECT_TRUE(custom_vector->values[0] == "x");
    EXPECT_TRUE(custom_vector->values[1] == "y");
}

};  // TEST_SUITE(deco_decl)

}  // namespace
}  // namespace kota::deco
