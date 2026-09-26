#include <tuple>
#include <type_traits>
#include <variant>

#include "kota/zest/zest.h"
#include "kota/meta/schema.h"

namespace kota::meta {

namespace {

struct full_tag {
    constexpr static auto spec = make_spec(dsl::rename = "userId",
                                           dsl::alias = {"user_id", "uid"},
                                           dsl::description = "User identifier.",
                                           dsl::idx = 7u);
};

struct defaulted_tag {
    constexpr static auto spec = make_spec(dsl::defaulted = true);
};

struct skip_tag {
    constexpr static auto spec = make_spec(dsl::skip = true);
};

struct flatten_tag {
    constexpr static auto spec = make_spec(dsl::flatten = true);
};

struct probe_adapter {};

struct with_tag {
    constexpr static auto spec = make_spec(dsl::with = dsl::type<probe_adapter>);
};

struct custom_pred {
    constexpr bool operator()(const int& value, bool is_serialize) const {
        return is_serialize && value < 0;
    }
};

struct extras_tag {
    constexpr static auto spec =
        make_spec(dsl::description = "Converted field.", dsl::as = dsl::type<std::int64_t>);
};

struct inner_pair {
    int first = 0;
    int second = 0;
};

struct circle_alt {
    double radius = 0;
};

struct rect_alt {
    double width = 0;
};

struct internal_struct_tag {
    constexpr static auto spec =
        make_struct_spec(dsl::tag = "kind", dsl::tag_names = {"circle", "rect"});
};

struct adjacent_struct_tag {
    constexpr static auto spec = make_struct_spec(dsl::tag = "t", dsl::content = "c");
};

struct external_struct_tag {
    constexpr static auto spec = make_struct_spec(dsl::tagged = true);
};

struct config_struct_tag {
    constexpr static auto spec = make_struct_spec(dsl::rename_all = naming::casing::lower_camel,
                                                  dsl::deny_unknown_fields = true);
};

struct config_struct_tag_twin {
    constexpr static auto spec = make_struct_spec(dsl::rename_all = naming::casing::lower_camel,
                                                  dsl::deny_unknown_fields = true);
};

struct noop_struct_tag {
    constexpr static auto spec = make_struct_spec(dsl::deny_unknown_fields = false);
};

struct spec_struct {
    annotate<full_tag>::type<int> user_id;
    annotate<skip_tag>::type<int> internal = 0;
    annotate<flatten_tag>::type<inner_pair> pair;
    annotate<defaulted_tag>::type<int> retries = 0;
};

ZEST_SUITE(meta_spec){

    ZEST_CASE(make_spec_folds_values){constexpr const field_spec& spec = full_tag::spec.value;
STATIC_EXPECT(spec.rename == "userId");
STATIC_EXPECT(spec.description == "User identifier.");
STATIC_EXPECT(spec.idx == 7u);
STATIC_EXPECT(spec.alias.count == 2u);
STATIC_EXPECT(spec.alias.storage[0] == "user_id");
STATIC_EXPECT(spec.alias.storage[1] == "uid");
STATIC_EXPECT(!spec.skip);
STATIC_EXPECT(!spec.flatten);
STATIC_EXPECT(!spec.defaulted);
STATIC_EXPECT(spec.skip_if == skip_when::never);
STATIC_EXPECT(std::tuple_size_v<decltype(full_tag::spec)::extras> == 0);

}  // namespace

ZEST_CASE(make_spec_keeps_type_components_in_type) {
    STATIC_EXPECT(extras_tag::spec.value.description == "Converted field.");
    using extras = decltype(extras_tag::spec)::extras;
    STATIC_EXPECT(std::tuple_size_v<extras> == 1);
    STATIC_EXPECT((std::is_same_v<std::tuple_element_t<0, extras>,
                                  dsl::type_component<dsl::aspect::as, std::int64_t>>));
}

ZEST_CASE(annotate_maps_components_to_behavior_attrs) {
    using annotated = annotate<extras_tag>::type<std::int32_t>;
    STATIC_EXPECT((std::is_same_v<typename annotated::annotated_type, std::int32_t>));
    using attrs_t = typename annotated::attrs;
    STATIC_EXPECT((tuple_has_v<attrs_t, attrs::spec<extras_tag>>));
    STATIC_EXPECT((tuple_has_v<attrs_t, behavior::as<std::int64_t>>));

    using with_extra = annotate<defaulted_tag>::type<int, behavior::skip_if<custom_pred>>;

    using adapted = annotate<with_tag>::type<int>;
    STATIC_EXPECT((tuple_has_v<typename adapted::attrs, behavior::with<probe_adapter>>));
    STATIC_EXPECT((tuple_has_v<typename with_extra::attrs, behavior::skip_if<custom_pred>>));
}

ZEST_CASE(spec_of_reads_annotation_and_defaults_to_empty) {
    using annotated = annotate<full_tag>::type<int>;
    STATIC_EXPECT(field_spec_of<annotated>.rename == "userId");
    STATIC_EXPECT(field_spec_of<int>.rename == "");
    STATIC_EXPECT((&field_spec_of<int> == &empty_field_spec));
}

ZEST_CASE(field_info_carries_spec_values) {
    using schema = virtual_schema<spec_struct>;
    // internal is skipped; pair flattens to two fields.
    STATIC_EXPECT(schema::count == 4u);

    constexpr auto& fields = schema::fields;
    STATIC_EXPECT(fields[0].name == "userId");
    STATIC_EXPECT(fields[0].aliases.size() == 2u);
    STATIC_EXPECT(fields[0].aliases[1] == "uid");
    STATIC_EXPECT(fields[0].idx == 7u);
    STATIC_EXPECT(fields[0].description == "User identifier.");
    STATIC_EXPECT(!fields[0].has_default);

    STATIC_EXPECT(fields[1].name == "first");
    STATIC_EXPECT(fields[2].name == "second");

    STATIC_EXPECT(fields[3].name == "retries");
    STATIC_EXPECT(fields[3].has_default);
    STATIC_EXPECT(fields[3].idx == field_spec::no_idx);
    STATIC_EXPECT(fields[3].description == "");
}

ZEST_CASE(name_only_spec_shares_slot_with_bare_field) {
    // A rename/description-only spec is dropped from encode slots, so the
    // annotated field and a bare int share the slot type.
    using slots = virtual_schema<spec_struct>::slots;
    using first_slot = type_list_element_t<0, slots>;
    STATIC_EXPECT((std::is_same_v<typename first_slot::attrs, std::tuple<>>));

    // A defaulted spec stays in the slot attrs for decode.
    using retries_slot = type_list_element_t<3, slots>;
    STATIC_EXPECT((tuple_has_v<typename retries_slot::attrs, attrs::spec<defaulted_tag>>));
}

ZEST_CASE(make_struct_spec_folds_values_and_derives_tagging) {
    constexpr const struct_spec& internal = internal_struct_tag::spec;
    STATIC_EXPECT(internal.tagging == tag_mode::internal);
    STATIC_EXPECT(internal.tag == "kind");
    STATIC_EXPECT(internal.tag_names.count == 2u);
    STATIC_EXPECT(internal.tag_names.storage[1] == "rect");

    constexpr const struct_spec& adjacent = adjacent_struct_tag::spec;
    STATIC_EXPECT(adjacent.tagging == tag_mode::adjacent);
    STATIC_EXPECT(adjacent.tag == "t");
    STATIC_EXPECT(adjacent.content == "c");

    STATIC_EXPECT(external_struct_tag::spec.tagging == tag_mode::external);

    constexpr const struct_spec& config = config_struct_tag::spec;
    STATIC_EXPECT(config.rename_all == naming::casing::lower_camel);
    STATIC_EXPECT(config.deny_unknown_fields);
    STATIC_EXPECT(config.tagging == tag_mode::none);

    STATIC_EXPECT(make_struct_spec(dsl::tagged = false).tagging == tag_mode::none);
}

ZEST_CASE(casing_maps_to_rename_policies) {
    using naming::casing;
    using naming::rename_policy_t;
    namespace policy = naming::rename_policy;
    STATIC_EXPECT((std::is_same_v<rename_policy_t<casing::identity>, policy::identity>));
    STATIC_EXPECT((std::is_same_v<rename_policy_t<casing::lower_snake>, policy::lower_snake>));
    STATIC_EXPECT((std::is_same_v<rename_policy_t<casing::lower_camel>, policy::lower_camel>));
    STATIC_EXPECT((std::is_same_v<rename_policy_t<casing::upper_camel>, policy::upper_camel>));
    STATIC_EXPECT((std::is_same_v<rename_policy_t<casing::upper_snake>, policy::upper_snake>));
}

ZEST_CASE(annotate_attaches_struct_spec) {
    using shape = annotate<internal_struct_tag>::type<std::variant<circle_alt, rect_alt>>;
    STATIC_EXPECT((tuple_has_v<typename shape::attrs, attrs::struct_spec<internal_struct_tag>>));
    STATIC_EXPECT((&struct_spec_of<typename shape::attrs> == &internal_struct_tag::spec));
    STATIC_EXPECT((&struct_spec_of<std::tuple<>> == &empty_struct_spec));
}

ZEST_CASE(equivalent_untagged_struct_specs_share_type_info) {
    // KOTATSU_ANNOTATE mints a fresh tag per use, so identical untagged
    // struct specs live on distinct tags; the type_info instance is keyed by
    // the spec values and must be shared.
    using lhs = annotate<config_struct_tag>::type<inner_pair>;
    using rhs = annotate<config_struct_tag_twin>::type<inner_pair>;
    STATIC_EXPECT((&type_info_of<lhs>() == &type_info_of<rhs>()));

    // A spec whose values match the defaults collapses to the bare type.
    using noop = annotate<noop_struct_tag>::type<inner_pair>;
    STATIC_EXPECT((&type_info_of<noop>() == &type_info_of<inner_pair>()));
}

ZEST_CASE(variant_type_info_carries_struct_spec) {
    using shape = annotate<internal_struct_tag>::type<std::variant<circle_alt, rect_alt>>;
    const auto& info = static_cast<const variant_type_info&>(type_info_of<shape>());
    EXPECT(info.tagging == tag_mode::internal);
    EXPECT(info.tag_field == "kind");
    EXPECT(info.content_field == "");
    ASSERT(info.alt_names.size() == 2u);
    EXPECT(info.alt_names[0] == "circle");
    EXPECT(info.alt_names[1] == "rect");

    // Without tag_names the alternatives fall back to their type names.
    using adjacent = annotate<adjacent_struct_tag>::type<std::variant<circle_alt, rect_alt>>;
    const auto& adj = static_cast<const variant_type_info&>(type_info_of<adjacent>());
    EXPECT(adj.content_field == "c");
    ASSERT(adj.alt_names.size() == 2u);
    EXPECT(adj.alt_names[0] == "circle_alt");

    // An untagged variant exposes no alternative names.
    const auto& bare =
        static_cast<const variant_type_info&>(type_info_of<std::variant<circle_alt, rect_alt>>());
    EXPECT(bare.tagging == tag_mode::none);
    EXPECT(bare.alt_names.size() == 0u);
}

ZEST_CASE(skip_when_evaluates_builtin_predicates) {
    STATIC_EXPECT(evaluate_skip_when<skip_when::none>(std::optional<int>{}, true));
    STATIC_EXPECT(!evaluate_skip_when<skip_when::none>(std::optional<int>{1}, true));
    STATIC_EXPECT(evaluate_skip_when<skip_when::empty>(std::string_view{}, true));
    STATIC_EXPECT(!evaluate_skip_when<skip_when::empty>(std::string_view{"x"}, true));
    STATIC_EXPECT(evaluate_skip_when<skip_when::default_value>(0, true));
    STATIC_EXPECT(!evaluate_skip_when<skip_when::default_value>(1, true));
    // Deserialization never skips.
    STATIC_EXPECT(!evaluate_skip_when<skip_when::default_value>(0, false));
}

};  // namespace kota::meta

}  // namespace

}  // namespace kota::meta
