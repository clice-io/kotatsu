#include <string>
#include <string_view>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

struct Fields {
    DECO_CFG(required = false);
    DecoKV(names = {"-o"};)
    <std::string> output;
    DecoFlagAlias(names = {"-O1"}; forward = {"-o", "1"};) _;
    int plain;
};

using ConfigWrapper = refl::field_type<Fields, 0>;
using Output = decltype(Fields::output);
using Alias = decltype(Fields::_);

ZEST_SUITE(deco_facade_ty) {

ZEST_CASE(config_field_is_its_wrapper) {
    ZSTATIC_EXPECT(ty::is_config_field<ConfigWrapper>);
    ZSTATIC_EXPECT(ty::is_config_field<const ConfigWrapper&>);
    ZSTATIC_EXPECT(!ty::is_config_field<Output>);
}

ZEST_CASE(alias_field_is_its_wrapper) {
    ZSTATIC_EXPECT(ty::is_alias_field<Alias>);
    ZSTATIC_EXPECT(!ty::is_alias_field<Output>);
    ZSTATIC_EXPECT(!ty::deco_option_like<Alias>);
}

ZEST_CASE(option_is_an_option_with_its_declaration) {
    ZSTATIC_EXPECT(ty::deco_option_like<Output>);
    ZSTATIC_EXPECT(ty::deco_option_like<Output&>);
    ZSTATIC_EXPECT(!ty::deco_option_like<int>);
    ZSTATIC_EXPECT(!ty::deco_option_like<decl::ScalarOption<int>>);
}

ZEST_CASE(option_field_is_a_declaration_of_a_kind) {
    ZSTATIC_EXPECT(ty::is_option_field<ty::field_ty_of<Output>>);
    ZSTATIC_EXPECT(ty::is_option_field<decl::FlagFields>);
    ZSTATIC_EXPECT(!ty::is_option_field<decl::ConfigFields>);
    ZSTATIC_EXPECT(!ty::is_option_field<decl::NamedOptionFields>);
}

ZEST_CASE(deco_field_is_any_declaration) {
    ZSTATIC_EXPECT(ty::is_deco_field<decl::KVFields>);
    ZSTATIC_EXPECT(ty::is_deco_field<ConfigWrapper>);
    ZSTATIC_EXPECT(ty::is_deco_field<Alias>);
    ZSTATIC_EXPECT(!ty::is_deco_field<int>);
    ZSTATIC_EXPECT(ty::is_deco_field_or_option<Output>);
    ZSTATIC_EXPECT(ty::is_decoed<Output>);
    ZSTATIC_EXPECT(ty::is_decoed<Alias>);
    ZSTATIC_EXPECT(!ty::is_decoed<decl::KVFields>);
}

ZEST_CASE(dyn_cast_gives_the_declaration) {
    const Fields fields{};
    const auto option = ty::dyn_cast(fields.output);
    ZEXPECT(zest::type_eq<decltype(option), const ty::field_ty_of<Output>>());
    ZEXPECT(option.names == std::vector<std::string_view>{"-o"});

    const auto alias = ty::dyn_cast(fields._);
    ZEXPECT(zest::type_eq<decltype(alias), const ty::alias_ty_of<Alias>>());
    ZEXPECT(alias.names == std::vector<std::string_view>{"-O1"});

    decl::KVFields declaration;
    declaration.style = decl::KVStyle::Joined;
    ZEXPECT(ty::dyn_cast(declaration).style == decl::KVStyle::Joined);
}

};  // ZEST_SUITE(deco_facade_ty)

}  // namespace

}  // namespace kota::deco
