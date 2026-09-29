#include <map>
#include <optional>
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

/// What an option is declared with once the configs in scope are applied.
struct Declared {
    bool required;
    const decl::Category* category;
    std::string help;
};

using Fields = std::map<std::string, Declared>;

/// The help of an option that sets none.
constexpr auto unset = "not provided";

/// What each option of T is declared with, by field name.
template <typename T>
Fields declared() {
    Fields fields;
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
    EXPECT(declared<NextApplies>() ==
           (Fields{
               {"first",  {false, &decl::default_category, "first only"} },
               {"second", {true, &decl::default_category, "not provided"}},
    }));
}

ZEST_CASE(scope_applies_between_start_and_end) {
    EXPECT(declared<ScopeApplies>() == (Fields{
                                           {"before",      {true, &decl::default_category, unset}},
                                           {"inside",      {false, &top_category, unset}         },
                                           {"also_inside", {false, &top_category, unset}         },
                                           {"after",       {true, &decl::default_category, unset}},
    }));
}

ZEST_CASE(next_config_on_a_nested_struct_applies_to_its_options) {
    EXPECT(declared<NextOnNested>() == (Fields{
                                           {"left",  {true, &inner_category, unset}        },
                                           {"right", {true, &inner_category, unset}        },
                                           {"tail",  {true, &decl::default_category, unset}},
    }));
}

ZEST_CASE(scopes_nest_across_structs) {
    // The inner scope ends inside the nested struct; the outer one still applies after it.
    EXPECT(declared<DeepScopes>() == (Fields{
                                         {"top",        {false, &top_category, unset}  },
                                         {"deep",       {false, &inner_category, unset}},
                                         {"after_deep", {false, &top_category, unset}  },
                                         {"tail",       {false, &top_category, unset}  },
    }));
}

ZEST_CASE(later_config_wins_until_it_ends) {
    EXPECT(declared<LaterWins>() == (Fields{
                                        {"first",  {true, &decl::default_category, "next"}  },
                                        {"second", {true, &decl::default_category, "scoped"}},
    }));
}

ZEST_CASE(next_config_passes_over_a_plain_member) {
    EXPECT(declared<PlainMember>() == (Fields{
                                          {"flag", {true, &decl::default_category, "for the flag"}},
    }));
}

ZEST_CASE(config_meta_var_is_explicit) {
    std::optional<decl::MetaVarField> meta_var;
    detail::generator_of<MetaVar>().visit_fields(MetaVar{},
                                                 [&](const auto&, const auto& cfg, auto, auto) {
                                                     meta_var = cfg.meta_var;
                                                     return true;
                                                 });
    ASSERT(meta_var.has_value());
    EXPECT(meta_var->value == "N");
    EXPECT(meta_var->is_explicit());
}

ZEST_CASE(schema_visit_applies_the_same_configs) {
    Fields fields;
    detail::generator_of<DeepScopes>().visit_schema_fields(
        [&](auto, const auto& cfg, std::string_view name, auto) {
            fields[std::string(name)] = Declared{
                .required = cfg.required,
                .category = cfg.category.ptr(),
                .help = std::string(cfg.help),
            };
            return true;
        });
    EXPECT(fields == declared<DeepScopes>());
}

};  // ZEST_SUITE(deco_facade_backend_config)

}  // namespace

}  // namespace kota::deco
