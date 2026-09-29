#include <map>
#include <string>
#include <string_view>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

constexpr decl::Category top_category{.exclusive = false,
                                      .required = false,
                                      .name = "top",
                                      .description = "the whole struct"};
constexpr decl::Category inner_category{.exclusive = false,
                                        .required = false,
                                        .name = "inner",
                                        .description = "a part of it"};

struct NextApplies {
    DECO_CFG(required = false; help = "first only");
    DecoFlag()
    first;
    DecoFlag()
    second;
};

struct ScopeApplies {
    DecoFlag()
    before;
    DECO_CFG_START(required = false; category = top_category);
    DecoFlag()
    inside;
    DecoKV()
    <std::string> also_inside;
    DECO_CFG_END();
    DecoFlag()
    after;
};

struct Leaves {
    DecoKV()
    <std::string> left;
    DecoKV()
    <std::string> right;
};

struct NextOnNested {
    DECO_CFG(category = inner_category);
    Leaves leaves;
    DecoKV()
    <std::string> tail;
};

struct DeepInner {
    DECO_CFG_START(category = inner_category);
    DecoKV()
    <std::string> deep;
    DECO_CFG_END();
    DecoKV()
    <std::string> after_deep;
};

struct DeepScopes {
    DECO_CFG_START(required = false; category = top_category);
    DecoKV()
    <std::string> top;
    DeepInner inner;
    DecoKV()
    <std::string> tail;
    DECO_CFG_END();
};

struct LaterWins {
    DECO_CFG_START(help = "scoped");
    DECO_CFG(help = "next");
    DecoFlag()
    first;
    DecoFlag()
    second;
    DECO_CFG_END();
};

struct PlainMember {
    DECO_CFG(help = "for the flag");
    int plain = 0;
    DecoFlag()
    flag;
};

struct MetaVar {
    DECO_CFG(meta_var = "N");
    DecoKV()
    <std::string> count;
};

/// What each option of T is declared with once the configs in scope are applied, by name.
struct Declared {
    bool required;
    const decl::Category* category;
    std::string help;
};

template <typename T>
std::map<std::string, Declared> declared() {
    std::map<std::string, Declared> fields;
    detail::generator_of<T>().visit_fields(
        T{},
        [&](const auto&, const auto& cfg, std::string_view name, auto) {
            fields[std::string(name)] = Declared{
                .required = cfg.required,
                .category = cfg.category.ptr(),
                .help = std::string(cfg.help),
            };
            return true;
        });
    return fields;
}

ZEST_SUITE(deco_facade_backend_config) {

ZEST_CASE(next_config_applies_to_the_next_option) {
    const auto fields = declared<NextApplies>();
    ASSERT(fields.contains("first"));
    ASSERT(fields.contains("second"));
    EXPECT(!fields.at("first").required);
    EXPECT(fields.at("first").help == "first only");
    EXPECT(fields.at("second").required);
    EXPECT(fields.at("second").help == "not provided");
}

ZEST_CASE(scope_applies_between_start_and_end) {
    const auto fields = declared<ScopeApplies>();
    ASSERT(fields.size() == 4U);
    EXPECT(fields.at("before").required);
    EXPECT(!fields.at("inside").required);
    EXPECT(fields.at("inside").category == &top_category);
    EXPECT(!fields.at("also_inside").required);
    EXPECT(fields.at("after").required);
    EXPECT(fields.at("after").category == &decl::default_category);
}

ZEST_CASE(next_config_on_a_nested_struct_applies_to_its_options) {
    const auto fields = declared<NextOnNested>();
    ASSERT(fields.size() == 3U);
    EXPECT(fields.at("left").category == &inner_category);
    EXPECT(fields.at("right").category == &inner_category);
    EXPECT(fields.at("tail").category == &decl::default_category);
}

ZEST_CASE(scopes_nest_across_structs) {
    const auto fields = declared<DeepScopes>();
    ASSERT(fields.size() == 4U);
    EXPECT(fields.at("top").category == &top_category);
    EXPECT(fields.at("deep").category == &inner_category);
    // The inner scope ends inside the nested struct; the outer one still applies.
    EXPECT(fields.at("after_deep").category == &top_category);
    EXPECT(fields.at("tail").category == &top_category);
    EXPECT(!fields.at("deep").required);
}

ZEST_CASE(later_config_wins_until_it_ends) {
    const auto fields = declared<LaterWins>();
    EXPECT(fields.at("first").help == "next");
    EXPECT(fields.at("second").help == "scoped");
}

ZEST_CASE(next_config_passes_over_a_plain_member) {
    const auto fields = declared<PlainMember>();
    ASSERT(fields.size() == 1U);
    EXPECT(fields.at("flag").help == "for the flag");
}

ZEST_CASE(config_meta_var_is_explicit) {
    bool is_explicit = false;
    detail::generator_of<MetaVar>().visit_fields(MetaVar{},
                                                 [&](const auto&, const auto& cfg, auto, auto) {
                                                     is_explicit = cfg.meta_var.is_explicit();
                                                     EXPECT(cfg.meta_var.value == "N");
                                                     return true;
                                                 });
    EXPECT(is_explicit);
}

ZEST_CASE(schema_visit_applies_the_same_configs) {
    std::map<std::string, bool> required;
    detail::generator_of<ScopeApplies>().visit_schema_fields(
        [&](auto, const auto& cfg, std::string_view name, auto) {
            required[std::string(name)] = cfg.required;
            return true;
        });
    const auto fields = declared<ScopeApplies>();
    ASSERT(required.size() == fields.size());
    for(const auto& [name, field]: fields) {
        ZEST_CONTEXT("option {}", name);
        EXPECT(required.at(name) == field.required);
    }
}

};  // ZEST_SUITE(deco_facade_backend_config)

}  // namespace

}  // namespace kota::deco
