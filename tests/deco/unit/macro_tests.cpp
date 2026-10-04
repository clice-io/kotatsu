// "kota/deco/macro.h" is what a downstream consuming kotatsu as a module has to
// include: modules cannot export macros, so the Deco* macros must arrive through
// a textual include that drags in no deco declarations of its own. Including it
// as the very first header here keeps that contract honest: if it ever grows a
// dependency on a facade header, this translation unit stops compiling.
#include "kota/deco/macro.h"

#if !defined(DECO_CFG) || !defined(DECO_CFG_START) || !defined(DECO_CFG_END) ||                    \
    !defined(DecoFlag) || !defined(DecoFlagN) || !defined(DecoInput) || !defined(DecoPack) ||      \
    !defined(DecoKV) || !defined(DecoKVStyled) || !defined(DecoComma) || !defined(DecoMulti) ||    \
    !defined(DecoFlagAlias) || !defined(DecoKVAlias) || !defined(DecoKVAliasStyled) ||             \
    !defined(DecoCommaAlias) || !defined(DecoMultiAlias)
#error "kota/deco/macro.h must define the deco declaration macros on its own"
#endif

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

constexpr decl::Category verbose_category{
    .exclusive = true,
    .name = "verbose",
    .description = "verbose-only mode",
};

auto forward_pair(const ParsedArgOwning&) -> decl::AliasForwardResult {
    return std::vector<std::string>{"--pair", "a", "b"};
}

// Declared with the macros already in scope from the standalone include above,
// which is the order a module consumer ends up with.
struct Declared {
    DecoFlag(names = {"-v", "--verbose"}; help = "verbose"; required = false;
             category = verbose_category;)
    verbose;

    DecoFlagN(names = {"-n"};)
    count;

    DecoInput(meta_var = "FILE";)
    <int> input = 42;

    DecoPack()
    <> pack;

    DecoKV()
    <> path;

    DecoKVStyled(decl::KVStyle::Joined, help = "joined";)
    <int> joined = 7;

    DecoComma(names = {"-T"};)
    <> tags = std::vector<std::string>{"x", "y"};

    DecoMulti(2, names = {"-P"};)
    <std::vector<int>> pair = std::vector<int>{1, 2};

    DecoFlag(after_parsed = Action::stop;)
    stops;
};

struct Aliases {
    DecoFlagAlias(names = {"-O1"}; forward = {"--optimize", "1"};) _;

    DecoKVAlias(names = {"--define-alias"};
                forward = std::vector<std::string_view>{"--define"};) __;

    DecoKVAliasStyled(decl::KVStyle::Joined, names = {"-D"}; forward = {"--define"};) ___;

    DecoCommaAlias(names = {"--tags-alias"}; forward = {"--tags"};) ____;

    DecoMultiAlias(2, names = {"--pair-alias"}; forward = forward_pair;) _____;
};

struct Configured {
    DECO_CFG_START(required = false; help = "scoped");
    DECO_CFG(category = verbose_category);
    DECO_CFG_END();
};

template <typename Option>
using cfg_of = ty::field_ty_of<Option>;

template <std::size_t I>
using config_at = ty::cfg_ty_of<refl::field_type<Configured, I>>;

ZEST_SUITE(deco_macro) {

ZEST_CASE(flag_declares_a_bool_flag) {
    ZEXPECT(zest::type_eq<decltype(Declared::verbose)::result_type, bool>());
    const cfg_of<decltype(Declared::verbose)> cfg;
    ZEXPECT(cfg.deco_field_ty == decl::DecoType::Flag);
    ZEXPECT(cfg.names == std::vector<std::string_view>{"-v", "--verbose"});
    ZEXPECT(cfg.help == "verbose");
    ZEXPECT(!cfg.required);
    ZEXPECT(cfg.category.ptr() == &verbose_category);
}

ZEST_CASE(flag_n_declares_a_counting_flag) {
    ZEXPECT(zest::type_eq<decltype(Declared::count)::result_type, std::uint32_t>());
    ZEXPECT(cfg_of<decltype(Declared::count)>::deco_field_ty == decl::DecoType::Flag);
}

ZEST_CASE(input_takes_the_type_given) {
    ZEXPECT(zest::type_eq<decltype(Declared::input)::result_type, int>());
    const cfg_of<decltype(Declared::input)> cfg;
    ZEXPECT(cfg.deco_field_ty == decl::DecoType::Input);
    ZEXPECT(cfg.meta_var.value == "FILE");
    ZEXPECT(cfg.meta_var.is_explicit());
}

ZEST_CASE(options_default_to_strings) {
    ZEXPECT(zest::type_eq<decltype(Declared::pack)::result_type, std::vector<std::string>>());
    ZEXPECT(cfg_of<decltype(Declared::pack)>::deco_field_ty == decl::DecoType::TrailingInput);
    ZEXPECT(zest::type_eq<decltype(Declared::path)::result_type, std::string>());
    ZEXPECT(zest::type_eq<decltype(Declared::tags)::result_type, std::vector<std::string>>());
}

ZEST_CASE(kv_is_separate_unless_styled) {
    const cfg_of<decltype(Declared::path)> path;
    ZEXPECT(path.deco_field_ty == decl::DecoType::KV);
    ZEXPECT(path.style == decl::KVStyle::Separate);
    const cfg_of<decltype(Declared::joined)> joined;
    ZEXPECT(joined.style == decl::KVStyle::Joined);
    ZEXPECT(joined.help == "joined");
}

ZEST_CASE(comma_and_multi_declare_their_kinds) {
    const cfg_of<decltype(Declared::tags)> tags;
    ZEXPECT(tags.deco_field_ty == decl::DecoType::CommaJoined);
    ZEXPECT(tags.names == std::vector<std::string_view>{"-T"});
    const cfg_of<decltype(Declared::pair)> pair;
    ZEXPECT(pair.deco_field_ty == decl::DecoType::Multi);
    ZEXPECT(pair.arg_num == 2U);
}

ZEST_CASE(options_are_required_by_default) {
    const cfg_of<decltype(Declared::path)> cfg;
    ZEXPECT(cfg.required);
    ZEXPECT(cfg.category.ptr() == &decl::default_category);
    ZEXPECT(cfg.help == "not provided");
    ZEXPECT(!cfg.meta_var.is_explicit());
}

ZEST_CASE(after_parsed_is_empty_unless_given) {
    ZEXPECT(cfg_of<decltype(Declared::path)>{}.after_parsed == nullptr);
    ZEXPECT(cfg_of<decltype(Declared::stops)>{}.after_parsed ==
            &decl::OptionCallbackField<bool>::Action::stop);
}

ZEST_CASE(option_starts_with_its_default) {
    const Declared declared{};
    ZASSERT(declared.input.has_value());
    ZEXPECT(*declared.input == 42);
    ZASSERT(declared.joined.has_value());
    ZEXPECT(*declared.joined == 7);
    ZEXPECT(declared.pair.as_optional() == std::optional(std::vector<int>{1, 2}));
    ZEXPECT(declared.tags.as_optional() == std::optional(std::vector<std::string>{"x", "y"}));
    ZEXPECT(!declared.path.has_value());
    ZEXPECT(!declared.verbose.has_value());
}

ZEST_CASE(alias_macros_declare_forwards) {
    const ty::alias_ty_of<decltype(Aliases::_)> flag;
    ZEXPECT(flag.deco_field_ty == decl::DecoType::Flag);
    ZEXPECT(flag.forward.kind == decl::AliasForwardField::Kind::Static);
    ZEXPECT(flag.forward.static_tokens == std::vector<std::string_view>{"--optimize", "1"});

    const ty::alias_ty_of<decltype(Aliases::__)> kv;
    ZEXPECT(kv.deco_field_ty == decl::DecoType::KV);
    ZEXPECT(kv.style == decl::KVStyle::Separate);
    ZEXPECT(kv.forward.static_tokens == std::vector<std::string_view>{"--define"});

    const ty::alias_ty_of<decltype(Aliases::___)> joined;
    ZEXPECT(joined.style == decl::KVStyle::Joined);

    const ty::alias_ty_of<decltype(Aliases::____)> comma;
    ZEXPECT(comma.deco_field_ty == decl::DecoType::CommaJoined);

    const ty::alias_ty_of<decltype(Aliases::_____)> multi;
    ZEXPECT(multi.deco_field_ty == decl::DecoType::Multi);
    ZEXPECT(multi.arg_num == 2U);
    ZEXPECT(multi.forward.kind == decl::AliasForwardField::Kind::Dynamic);
    ZEXPECT(multi.forward.dynamic == &forward_pair);
}

ZEST_CASE(config_macros_declare_scoped_overrides) {
    const config_at<0> start;
    ZEXPECT(start.type == decl::ConfigFields::Type::Start);
    ZEXPECT(start.required.is_overridden());
    ZEXPECT(!start.required.get());
    ZEXPECT(start.help.is_overridden());
    ZEXPECT(start.help.get() == "scoped");
    ZEXPECT(!start.category.is_overridden());

    const config_at<1> next;
    ZEXPECT(next.type == decl::ConfigFields::Type::Next);
    ZEXPECT(next.category.is_overridden());
    ZEXPECT(next.category.get().ptr() == &verbose_category);
    ZEXPECT(!next.required.is_overridden());

    const config_at<2> end;
    ZEXPECT(end.type == decl::ConfigFields::Type::End);
}

};  // ZEST_SUITE(deco_macro)

}  // namespace

}  // namespace kota::deco
