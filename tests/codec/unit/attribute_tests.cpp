#include <compare>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::codec {

using meta::attrs_of_t;
using meta::field_attrs_t;
using meta::make_annotation;
using meta::name_key;

namespace {

struct full_tag {
    constexpr static auto attributes = make_annotation(dsl::rename = "userId",
                                                       dsl::alias = {"user_id", "uid"},
                                                       dsl::description = "User identifier.");
};

struct schema_tag {
    constexpr static auto attributes = make_annotation(dsl::minimum = 1,
                                                       dsl::maximum = 2.5,
                                                       dsl::choices = {"off", "on"},
                                                       dsl::schema_default = false);
};

struct optional_tag {
    constexpr static auto attributes = make_annotation(dsl::presence = presence_kind::optional);
};

struct skip_tag {
    constexpr static auto attributes = make_annotation(dsl::skip = true);
};

struct flatten_tag {
    constexpr static auto attributes = make_annotation(dsl::flatten = true);
};

struct probe_adapter {};

struct with_tag {
    constexpr static auto attributes = make_annotation(dsl::with = dsl::type<probe_adapter>);
};

struct custom_pred {
    constexpr bool operator()(const int& value) const {
        return value < 0;
    }
};

struct extras_tag {
    constexpr static auto attributes =
        make_annotation(dsl::description = "Converted field.", dsl::as = dsl::type<std::int64_t>);
};

/// The spec of an extension, a module beside the codec with attributes of its own.
struct probe_extension {
    std::string_view label;
    int weight = 0;
};

[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&probe_extension::label> label{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&probe_extension::weight> weight{};

struct extension_tag {
    constexpr static auto attributes =
        make_annotation(dsl::description = "Weighed.", label = "probe", weight = 3);
};

// The fields and types below are annotated as KANNOTATE and KANNOTATE_TYPE (in
// codec's macro sheet) expand to.

struct holds_extension {
    static consteval auto __kotatsu__annotation(name_key<"weighed">) {
        return extension_tag::attributes;
    }

    int weighed;

    static consteval auto __kotatsu__annotation(name_key<"plain">) {
        return full_tag::attributes;
    }

    int plain;
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
    constexpr static auto attributes =
        make_annotation(dsl::tag = "kind", dsl::tag_names = {"circle", "rect"});
};

struct adjacent_struct_tag {
    constexpr static auto attributes = make_annotation(dsl::tag = "t", dsl::content = "c");
};

struct external_struct_tag {
    constexpr static auto attributes = make_annotation(dsl::tagged = true);
};

struct config_struct_tag {
    constexpr static auto attributes = make_annotation(dsl::rename_all = naming::Casing::LowerCamel,
                                                       dsl::deny_unknown_fields = true);
};

struct spec_struct {
    static consteval auto __kotatsu__annotation(name_key<"user_id">) {
        return full_tag::attributes;
    }

    int user_id;

    static consteval auto __kotatsu__annotation(name_key<"internal">) {
        return skip_tag::attributes;
    }

    int internal = 0;

    static consteval auto __kotatsu__annotation(name_key<"pair">) {
        return flatten_tag::attributes;
    }

    inner_pair pair;

    static consteval auto __kotatsu__annotation(name_key<"retries">) {
        return optional_tag::attributes;
    }

    int retries = 0;
};

struct behavior_struct {
    static consteval auto __kotatsu__annotation(name_key<"converted">) {
        return extras_tag::attributes;
    }

    std::int32_t converted;

    static consteval auto __kotatsu__annotation(name_key<"adapted">) {
        return with_tag::attributes;
    }

    int adapted;

    static consteval auto __kotatsu__annotation(name_key<"omitted">) {
        return make_annotation(dsl::presence = dsl::type<custom_pred>);
    }

    int omitted;
};

/// Each field merges the same untagged struct spec, the last none at all.
struct policy_fields {
    static consteval auto __kotatsu__annotation(name_key<"lhs">) {
        return config_struct_tag::attributes;
    }

    inner_pair lhs;

    static consteval auto __kotatsu__annotation(name_key<"rhs">) {
        return config_struct_tag::attributes;
    }

    inner_pair rhs;

    static consteval auto __kotatsu__annotation(name_key<"noop">) {
        return make_annotation(dsl::deny_unknown_fields = false);
    }

    inner_pair noop;
};

struct shape;

constexpr auto __kotatsu__type_annotation(std::type_identity<shape>) {
    return internal_struct_tag::attributes;
}

struct shape : std::variant<circle_alt, rect_alt> {
    using variant::variant;
};

struct adjacent_shape;

constexpr auto __kotatsu__type_annotation(std::type_identity<adjacent_shape>) {
    return adjacent_struct_tag::attributes;
}

struct adjacent_shape : std::variant<circle_alt, rect_alt> {
    using variant::variant;
};

ZEST_SUITE(codec_attribute) {

ZEST_CASE(attributes_fold_into_values) {
    constexpr const field_spec& spec = meta::spec_attr<full_tag, field_spec>::value;
    ZSTATIC_EXPECT(spec.rename == "userId");
    ZSTATIC_EXPECT(spec.description == "User identifier.");
    ZSTATIC_EXPECT(spec.alias.size() == 2u);
    ZSTATIC_EXPECT(spec.alias[0] == "user_id");
    ZSTATIC_EXPECT(spec.alias[1] == "uid");
    ZSTATIC_EXPECT(!spec.skip);
    ZSTATIC_EXPECT(!spec.flatten);
    ZSTATIC_EXPECT(!spec.presence.has_value());
    ZSTATIC_EXPECT(std::tuple_size_v<field_attrs_t<spec_struct, 0>> == 1);
}

ZEST_CASE(attributes_fold_into_what_a_schema_states) {
    constexpr const field_spec& spec = meta::spec_attr<schema_tag, field_spec>::value;
    ZSTATIC_EXPECT(std::get<std::int64_t>(spec.minimum) == 1);
    ZSTATIC_EXPECT(std::get<double>(spec.maximum) == 2.5);
    ZSTATIC_EXPECT(spec.choices[1] == "on");
    ZSTATIC_EXPECT(!spec.schema_default);
    // Unset, a field states its default and no bounds.
    ZSTATIC_EXPECT(meta::spec_attr<full_tag, field_spec>::value.schema_default);
    ZSTATIC_EXPECT(std::holds_alternative<std::monostate>(
        meta::spec_attr<full_tag, field_spec>::value.minimum));
    // Unsigned values keep their sign.
    ZSTATIC_EXPECT(
        std::get<std::uint64_t>(
            meta::detail::spec_from<field_spec>(make_annotation(dsl::maximum = ~0ULL)).maximum) ==
        ~0ULL);
}

ZEST_CASE(numbers_compare_exactly) {
    using std::partial_ordering;
    ZSTATIC_EXPECT(compare_numbers(std::int64_t{-1}, std::uint64_t{0}) == partial_ordering::less);
    ZSTATIC_EXPECT(compare_numbers(~0ULL, std::int64_t{-1}) == partial_ordering::greater);
    // An integer against a double, either way round.
    ZSTATIC_EXPECT(compare_numbers(2.5, std::int64_t{2}) == partial_ordering::greater);
    ZSTATIC_EXPECT(compare_numbers(std::int64_t{2}, 2.5) == partial_ordering::less);
    ZSTATIC_EXPECT(compare_numbers(1.5, 2.5) == partial_ordering::less);
    ZSTATIC_EXPECT(compare_numbers(schema_number{}, std::int64_t{1}) ==
                   partial_ordering::unordered);
}

ZEST_CASE(attributes_keep_type_components) {
    ZSTATIC_EXPECT(meta::spec_attr<extras_tag, field_spec>::value.description ==
                   "Converted field.");
    using attributes = std::remove_const_t<decltype(extras_tag::attributes)>;
    ZSTATIC_EXPECT((std::is_same_v<std::tuple_element_t<1, attributes>,
                                   dsl::representation_component<behavior::as<std::int64_t>>>));
}

ZEST_CASE(attributes_keep_extension_specs_beside_field_spec) {
    ZSTATIC_EXPECT(meta::spec_attr<extension_tag, field_spec>::value.description == "Weighed.");
    using probe = meta::spec_attr<extension_tag, probe_extension>;
    ZSTATIC_EXPECT(probe::value.label == "probe");
    ZSTATIC_EXPECT(probe::value.weight == 3);
}

ZEST_CASE(extension_attributes_are_told_apart_by_their_member) {
    using std::type_identity;
    using std::tuple;
    ZSTATIC_EXPECT(meta::detail::attributes_unique(
        type_identity<tuple<decltype(label = "a"), decltype(weight = 1)>>{}));
    ZSTATIC_EXPECT(!meta::detail::attributes_unique(
        type_identity<tuple<decltype(label = "a"), decltype(label = "b")>>{}));
    ZSTATIC_EXPECT(meta::detail::attributes_unique(
        type_identity<tuple<decltype(dsl::rename = "a"), decltype(label = "b")>>{}));
}

ZEST_CASE(a_field_carries_its_extension_specs) {
    ZSTATIC_EXPECT(meta::spec_in<probe_extension, field_attrs_t<holds_extension, 0>>->weight == 3);
    ZSTATIC_EXPECT(meta::spec_in<probe_extension, field_attrs_t<holds_extension, 1>> == nullptr);
    ZSTATIC_EXPECT(meta::spec_in<probe_extension, attrs_of_t<int>> == nullptr);
}

ZEST_CASE(field_info_carries_extensions_and_declared_names) {
    const auto& info = static_cast<const struct_type_info&>(type_info::of<holds_extension>());
    ZASSERT(info.fields.size() == 2u);
    ZEXPECT(info.fields[0].extension<probe_extension>()->label == "probe");
    ZEXPECT(info.fields[1].extension<probe_extension>() == nullptr);
    // The declared name survives a rename.
    ZEXPECT(info.fields[1].name == "userId");
    ZEXPECT(info.fields[1].declared_name == "plain");
}

ZEST_CASE(field_info_finds_extensions_at_compile_time) {
    constexpr const auto& info =
        static_cast<const struct_type_info&>(type_info::of<holds_extension>());
    // The specs of other modules, the codec's own left out.
    ZSTATIC_EXPECT(info.fields[0].extensions.size() == 1U);
    ZSTATIC_EXPECT(info.fields[1].extensions.empty());
    ZSTATIC_EXPECT(info.fields[0].has_extension<probe_extension>());
    ZSTATIC_EXPECT(info.fields[0].extension<probe_extension>()->weight == 3);
    ZSTATIC_EXPECT(!info.fields[1].has_extension<probe_extension>());
}

ZEST_CASE(annotation_maps_components_to_behavior_attrs) {
    using converted = field_attrs_t<behavior_struct, 0>;
    ZSTATIC_EXPECT(meta::spec_in<field_spec, converted> != nullptr);
    ZSTATIC_EXPECT(spec_of<field_spec, converted>.description == "Converted field.");
    ZSTATIC_EXPECT((tuple_has_v<converted, behavior::as<std::int64_t>>));

    ZSTATIC_EXPECT((tuple_has_v<field_attrs_t<behavior_struct, 1>, behavior::with<probe_adapter>>));

    using omitted = field_attrs_t<behavior_struct, 2>;
    ZSTATIC_EXPECT((tuple_has_v<omitted, behavior::omit_if<custom_pred>>));
    ZSTATIC_EXPECT(spec_of<field_spec, omitted>.presence == presence_kind::omit_if);
}

ZEST_CASE(presence_value_and_predicate_are_one_attribute) {
    // So that one annotation cannot write both.
    using value = decltype(dsl::presence = presence_kind::optional);
    using predicate = decltype(dsl::presence = dsl::type<custom_pred>);
    ZSTATIC_EXPECT(
        !meta::detail::attributes_unique(std::type_identity<std::tuple<value, predicate>>{}));
}

ZEST_CASE(spec_of_reads_annotation_and_defaults_to_unset) {
    ZSTATIC_EXPECT(spec_of<field_spec, field_attrs_t<spec_struct, 0>>.rename == "userId");
    ZSTATIC_EXPECT(spec_of<field_spec, attrs_of_t<int>>.rename.empty());
    ZSTATIC_EXPECT(!spec_of<field_spec, attrs_of_t<int>>.presence.has_value());
    ZSTATIC_EXPECT(spec_of<struct_spec, attrs_of_t<int>>.tagging == tag_mode::none);
}

ZEST_CASE(field_info_carries_spec_values) {
    using schema = virtual_schema<spec_struct>;
    // internal is skipped; pair flattens to two fields.
    ZSTATIC_EXPECT(schema::fields.size() == 4u);

    constexpr auto& fields = schema::fields;
    ZSTATIC_EXPECT(fields[0].name == "userId");
    ZSTATIC_EXPECT(fields[0].aliases.size() == 2u);
    ZSTATIC_EXPECT(fields[0].aliases[1] == "uid");
    ZSTATIC_EXPECT(fields[0].description == "User identifier.");
    ZSTATIC_EXPECT(fields[0].presence == presence_kind::required);

    ZSTATIC_EXPECT(fields[1].name == "first");
    ZSTATIC_EXPECT(fields[2].name == "second");

    ZSTATIC_EXPECT(fields[3].name == "retries");
    ZSTATIC_EXPECT(fields[3].presence == presence_kind::optional);
    ZSTATIC_EXPECT(fields[3].description == "");
}

ZEST_CASE(field_spec_stays_out_of_the_slot) {
    // The field's info carries its spec, its presence included, so an
    // annotated field and a bare int share the slot type.
    using slots = virtual_schema<spec_struct>::slots;
    using first_slot = type_list_element_t<0, slots>;
    ZSTATIC_EXPECT((std::is_same_v<typename first_slot::attrs, std::tuple<>>));
    using retries_slot = type_list_element_t<3, slots>;
    ZSTATIC_EXPECT((std::is_same_v<typename retries_slot::attrs, std::tuple<>>));
}

ZEST_CASE(struct_attributes_fold_into_values_and_derive_tagging) {
    constexpr const struct_spec& internal =
        meta::spec_attr<internal_struct_tag, struct_spec>::value;
    ZSTATIC_EXPECT(internal.tagging == tag_mode::internal);
    ZSTATIC_EXPECT(internal.tag == "kind");
    ZSTATIC_EXPECT(internal.tag_names.size() == 2u);
    ZSTATIC_EXPECT(internal.tag_names[1] == "rect");

    constexpr const struct_spec& adjacent =
        meta::spec_attr<adjacent_struct_tag, struct_spec>::value;
    ZSTATIC_EXPECT(adjacent.tagging == tag_mode::adjacent);
    ZSTATIC_EXPECT(adjacent.tag == "t");
    ZSTATIC_EXPECT(adjacent.content == "c");

    ZSTATIC_EXPECT(meta::spec_attr<external_struct_tag, struct_spec>::value.tagging ==
                   tag_mode::external);

    constexpr const struct_spec& config = meta::spec_attr<config_struct_tag, struct_spec>::value;
    ZEXPECT(zest::type_eq<naming::rename_policy_of_t<*config.rename_all>,
                          naming::rename_policy::lower_camel>());
    ZSTATIC_EXPECT(config.deny_unknown_fields);
    ZSTATIC_EXPECT(config.tagging == tag_mode::none);

    ZSTATIC_EXPECT(
        meta::detail::spec_from<struct_spec>(make_annotation(dsl::tagged = false)).tagging ==
        tag_mode::none);
}

ZEST_CASE(type_annotation_attaches_struct_spec) {
    ZSTATIC_EXPECT(meta::spec_in<struct_spec, attrs_of_t<shape>> != nullptr);
    ZSTATIC_EXPECT(spec_of<struct_spec, attrs_of_t<shape>>.tag == "kind");
}

ZEST_CASE(equivalent_untagged_struct_specs_share_type_info) {
    // Each field's annotation is a tag of its own, so identical untagged
    // struct specs live on distinct tags; the type_info instance is keyed by
    // the spec values and must be shared.
    const auto& fields =
        static_cast<const struct_type_info&>(type_info::of<policy_fields>()).fields;
    ZASSERT(fields.size() == 3u);
    ZEXPECT((&fields[0].type() == &fields[1].type()));

    // A spec whose values match the defaults collapses to the bare type.
    ZEXPECT((&fields[2].type() == &type_info::of<inner_pair>()));
}

ZEST_CASE(variant_type_info_carries_struct_spec) {
    const auto& info = static_cast<const variant_type_info&>(type_info::of<shape>());
    ZEXPECT(info.tagging == tag_mode::internal);
    ZEXPECT(info.tag_field == "kind");
    ZEXPECT(info.content_field == "");
    ZASSERT(info.alt_names.size() == 2u);
    ZEXPECT(info.alt_names[0] == "circle");
    ZEXPECT(info.alt_names[1] == "rect");

    // Without tag_names the alternatives fall back to their type names.
    const auto& adj = static_cast<const variant_type_info&>(type_info::of<adjacent_shape>());
    ZEXPECT(adj.content_field == "c");
    ZASSERT(adj.alt_names.size() == 2u);
    ZEXPECT(adj.alt_names[0] == "circle_alt");

    // An untagged variant exposes no alternative names.
    const auto& bare =
        static_cast<const variant_type_info&>(type_info::of<std::variant<circle_alt, rect_alt>>());
    ZEXPECT(bare.tagging == tag_mode::none);
    ZEXPECT(bare.alt_names.size() == 0u);
}

};  // ZEST_SUITE(codec_attribute)

}  // namespace

}  // namespace kotatsu::codec
