#include <compare>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/schema.h"

namespace kotatsu::meta {

namespace {

struct full_tag {
    constexpr static auto entries = make_annotation(dsl::rename = "userId",
                                                    dsl::alias = {"user_id", "uid"},
                                                    dsl::description = "User identifier.",
                                                    dsl::idx = 7u);
};

/// A member whose default constructor is explicit.
struct explicit_member {
    explicit explicit_member() = default;

    auto operator==(const explicit_member&) const -> bool = default;
};

/// Only value-initialization makes it.
struct only_value_initialized {
    explicit_member member;
    int count = 0;

    auto operator==(const only_value_initialized&) const -> bool = default;
};

struct schema_tag {
    constexpr static auto entries = make_annotation(dsl::minimum = 1,
                                                    dsl::maximum = 2.5,
                                                    dsl::choices = {"off", "on"},
                                                    dsl::schema_default = false);
};

struct defaulted_tag {
    constexpr static auto entries = make_annotation(dsl::defaulted = true);
};

struct skip_tag {
    constexpr static auto entries = make_annotation(dsl::skip = true);
};

struct flatten_tag {
    constexpr static auto entries = make_annotation(dsl::flatten = true);
};

struct probe_adapter {};

struct with_tag {
    constexpr static auto entries = make_annotation(dsl::with = dsl::type<probe_adapter>);
};

struct custom_pred {
    constexpr bool operator()(const int& value, bool is_serialize) const {
        return is_serialize && value < 0;
    }
};

/// Takes only the value, so it speaks for encoding.
struct is_zero {
    constexpr bool operator()(const int& value) const {
        return value == 0;
    }
};

struct extras_tag {
    constexpr static auto entries =
        make_annotation(dsl::description = "Converted field.", dsl::as = dsl::type<std::int64_t>);
};

/// The spec of an extension, a module beside meta with entries of its own.
struct probe_extension {
    std::string_view label;
    int weight = 0;
};

[[maybe_unused]] constexpr inline dsl::member_proxy<&probe_extension::label> label{};
[[maybe_unused]] constexpr inline dsl::member_proxy<&probe_extension::weight> weight{};

struct extension_tag {
    constexpr static auto entries =
        make_annotation(dsl::description = "Weighed.", label = "probe", weight = 3);
};

struct holds_extension {
    annotate<extension_tag>::type<int> weighed;
    annotate<full_tag>::type<int> plain;
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
    constexpr static auto entries =
        make_annotation(dsl::tag = "kind", dsl::tag_names = {"circle", "rect"});
};

struct adjacent_struct_tag {
    constexpr static auto entries = make_annotation(dsl::tag = "t", dsl::content = "c");
};

struct external_struct_tag {
    constexpr static auto entries = make_annotation(dsl::tagged = true);
};

struct config_struct_tag {
    constexpr static auto entries = make_annotation(dsl::rename_all = naming::Casing::LowerCamel,
                                                    dsl::deny_unknown_fields = true);
};

struct config_struct_tag_twin {
    constexpr static auto entries = make_annotation(dsl::rename_all = naming::Casing::LowerCamel,
                                                    dsl::deny_unknown_fields = true);
};

struct noop_struct_tag {
    constexpr static auto entries = make_annotation(dsl::deny_unknown_fields = false);
};

struct spec_struct {
    annotate<full_tag>::type<int> user_id;
    annotate<skip_tag>::type<int> internal = 0;
    annotate<flatten_tag>::type<inner_pair> pair;
    annotate<defaulted_tag>::type<int> retries = 0;
};

ZEST_SUITE(meta_spec) {

ZEST_CASE(entries_fold_into_values) {
    constexpr const field_spec& spec = attrs::spec<full_tag>::value;
    ZSTATIC_EXPECT(spec.rename == "userId");
    ZSTATIC_EXPECT(spec.description == "User identifier.");
    ZSTATIC_EXPECT(spec.idx == 7u);
    ZSTATIC_EXPECT(spec.alias.size() == 2u);
    ZSTATIC_EXPECT(spec.alias[0] == "user_id");
    ZSTATIC_EXPECT(spec.alias[1] == "uid");
    ZSTATIC_EXPECT(!spec.skip);
    ZSTATIC_EXPECT(!spec.flatten);
    ZSTATIC_EXPECT(!spec.defaulted);
    ZSTATIC_EXPECT(spec.skip_if == skip_when::never);
    ZSTATIC_EXPECT(std::tuple_size_v<typename annotate<full_tag>::type<int>::attrs> == 1);
}

ZEST_CASE(entries_fold_into_what_a_schema_states) {
    constexpr const field_spec& spec = attrs::spec<schema_tag>::value;
    ZSTATIC_EXPECT(std::get<std::int64_t>(spec.minimum) == 1);
    ZSTATIC_EXPECT(std::get<double>(spec.maximum) == 2.5);
    ZSTATIC_EXPECT(spec.choices[1] == "on");
    ZSTATIC_EXPECT(!spec.schema_default);
    // Unset, a field states its default and no bounds.
    ZSTATIC_EXPECT(attrs::spec<full_tag>::value.schema_default);
    ZSTATIC_EXPECT(std::holds_alternative<std::monostate>(attrs::spec<full_tag>::value.minimum));
    // Unsigned values keep their sign.
    ZSTATIC_EXPECT(std::get<std::uint64_t>(
                       detail::field_spec_from(make_annotation(dsl::maximum = ~0ULL)).maximum) ==
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

ZEST_CASE(entries_keep_type_components) {
    ZSTATIC_EXPECT(attrs::spec<extras_tag>::value.description == "Converted field.");
    using entries = std::remove_const_t<decltype(extras_tag::entries)>;
    ZSTATIC_EXPECT((std::is_same_v<std::tuple_element_t<1, entries>,
                                   dsl::type_component<dsl::aspect::as, std::int64_t>>));
}

ZEST_CASE(entries_keep_extension_specs_beside_field_spec) {
    using spec = attrs::spec<extension_tag>;
    ZSTATIC_EXPECT(spec::value.description == "Weighed.");
    ZSTATIC_EXPECT(std::get<probe_extension>(spec::extensions).label == "probe");
    ZSTATIC_EXPECT(std::get<probe_extension>(spec::extensions).weight == 3);
    // An annotation without extension entries carries none.
    ZSTATIC_EXPECT(std::tuple_size_v<decltype(attrs::spec<full_tag>::extensions)> == 0);
}

ZEST_CASE(extension_entries_are_told_apart_by_their_member) {
    ZSTATIC_EXPECT(detail::component_kinds_unique<decltype(label = "a"), decltype(weight = 1)>());
    ZSTATIC_EXPECT(!detail::component_kinds_unique<decltype(label = "a"), decltype(label = "b")>());
    ZSTATIC_EXPECT(
        detail::component_kinds_unique<decltype(dsl::rename = "a"), decltype(label = "b")>());
}

ZEST_CASE(extension_of_finds_a_field_s_extension) {
    using weighed_t = annotate<extension_tag>::type<int>;
    ZSTATIC_EXPECT(extension_of<probe_extension, weighed_t>->weight == 3);
    ZSTATIC_EXPECT(extension_of<probe_extension, annotate<full_tag>::type<int>> == nullptr);
    ZSTATIC_EXPECT(extension_of<probe_extension, int> == nullptr);
}

ZEST_CASE(field_info_carries_extensions_and_declared_names) {
    const auto& info = static_cast<const struct_type_info&>(type_info_of<holds_extension>());
    ZASSERT(info.fields.size() == 2u);
    ZEXPECT(info.fields[0].extension<probe_extension>()->label == "probe");
    ZEXPECT(info.fields[1].extension<probe_extension>() == nullptr);
    // The declared name survives a rename.
    ZEXPECT(info.fields[1].name == "userId");
    ZEXPECT(info.fields[1].declared_name == "plain");
}

ZEST_CASE(field_info_finds_extensions_at_compile_time) {
    constexpr const auto& info =
        static_cast<const struct_type_info&>(type_info_of<holds_extension>());
    ZSTATIC_EXPECT(info.fields[0].has_extension<probe_extension>());
    ZSTATIC_EXPECT(info.fields[0].extension<probe_extension>()->weight == 3);
    ZSTATIC_EXPECT(!info.fields[1].has_extension<probe_extension>());
}

ZEST_CASE(annotate_maps_components_to_behavior_attrs) {
    using annotated = annotate<extras_tag>::type<std::int32_t>;
    ZSTATIC_EXPECT((std::is_same_v<typename annotated::annotated_type, std::int32_t>));
    using attrs_t = typename annotated::attrs;
    ZSTATIC_EXPECT((tuple_has_v<attrs_t, attrs::spec<extras_tag>>));
    ZSTATIC_EXPECT((tuple_has_v<attrs_t, behavior::as<std::int64_t>>));

    using with_extra = annotate<defaulted_tag>::type<int, behavior::skip_if<custom_pred>>;

    using adapted = annotate<with_tag>::type<int>;
    ZSTATIC_EXPECT((tuple_has_v<typename adapted::attrs, behavior::with<probe_adapter>>));
    ZSTATIC_EXPECT((tuple_has_v<typename with_extra::attrs, behavior::skip_if<custom_pred>>));
}

ZEST_CASE(skip_predicate_decides_by_direction) {
    ZSTATIC_EXPECT(evaluate_skip_predicate<custom_pred>(-1, true));
    ZSTATIC_EXPECT(!evaluate_skip_predicate<custom_pred>(-1, false));
    // A one-argument predicate holds only when encoding.
    ZSTATIC_EXPECT(evaluate_skip_predicate<is_zero>(0, true));
    ZSTATIC_EXPECT(!evaluate_skip_predicate<is_zero>(0, false));
    ZSTATIC_EXPECT(!evaluate_skip_predicate<is_zero>(1, true));
}

ZEST_CASE(spec_of_reads_annotation_and_defaults_to_empty) {
    using annotated = annotate<full_tag>::type<int>;
    ZSTATIC_EXPECT(field_spec_of<annotated>.rename == "userId");
    ZSTATIC_EXPECT(field_spec_of<int>.rename == "");
    ZSTATIC_EXPECT((&field_spec_of<int> == &empty_field_spec));
}

ZEST_CASE(field_info_carries_spec_values) {
    using schema = virtual_schema<spec_struct>;
    // internal is skipped; pair flattens to two fields.
    ZSTATIC_EXPECT(schema::count == 4u);

    constexpr auto& fields = schema::fields;
    ZSTATIC_EXPECT(fields[0].name == "userId");
    ZSTATIC_EXPECT(fields[0].aliases.size() == 2u);
    ZSTATIC_EXPECT(fields[0].aliases[1] == "uid");
    ZSTATIC_EXPECT(fields[0].idx == 7u);
    ZSTATIC_EXPECT(fields[0].description == "User identifier.");
    ZSTATIC_EXPECT(!fields[0].has_default);

    ZSTATIC_EXPECT(fields[1].name == "first");
    ZSTATIC_EXPECT(fields[2].name == "second");

    ZSTATIC_EXPECT(fields[3].name == "retries");
    ZSTATIC_EXPECT(fields[3].has_default);
    ZSTATIC_EXPECT(fields[3].idx == field_spec::no_idx);
    ZSTATIC_EXPECT(fields[3].description == "");
}

ZEST_CASE(name_only_spec_shares_slot_with_bare_field) {
    // A rename/description-only spec is dropped from encode slots, so the
    // annotated field and a bare int share the slot type.
    using slots = virtual_schema<spec_struct>::slots;
    using first_slot = type_list_element_t<0, slots>;
    ZSTATIC_EXPECT((std::is_same_v<typename first_slot::attrs, std::tuple<>>));

    // A defaulted spec stays in the slot attrs for decode.
    using retries_slot = type_list_element_t<3, slots>;
    ZSTATIC_EXPECT((tuple_has_v<typename retries_slot::attrs, attrs::spec<defaulted_tag>>));
}

ZEST_CASE(struct_entries_fold_into_values_and_derive_tagging) {
    constexpr const struct_spec& internal = attrs::struct_spec<internal_struct_tag>::value;
    ZSTATIC_EXPECT(internal.tagging == tag_mode::internal);
    ZSTATIC_EXPECT(internal.tag == "kind");
    ZSTATIC_EXPECT(internal.tag_names.size() == 2u);
    ZSTATIC_EXPECT(internal.tag_names[1] == "rect");

    constexpr const struct_spec& adjacent = attrs::struct_spec<adjacent_struct_tag>::value;
    ZSTATIC_EXPECT(adjacent.tagging == tag_mode::adjacent);
    ZSTATIC_EXPECT(adjacent.tag == "t");
    ZSTATIC_EXPECT(adjacent.content == "c");

    ZSTATIC_EXPECT(attrs::struct_spec<external_struct_tag>::value.tagging == tag_mode::external);

    constexpr const struct_spec& config = attrs::struct_spec<config_struct_tag>::value;
    ZSTATIC_EXPECT(config.rename_all == naming::Casing::LowerCamel);
    ZSTATIC_EXPECT(config.deny_unknown_fields);
    ZSTATIC_EXPECT(config.tagging == tag_mode::none);

    ZSTATIC_EXPECT(detail::struct_spec_from(make_annotation(dsl::tagged = false)).tagging ==
                   tag_mode::none);
}

ZEST_CASE(casing_maps_to_rename_policies) {
    using naming::Casing;
    using naming::rename_policy_t;
    namespace policy = naming::rename_policy;
    ZSTATIC_EXPECT((std::is_same_v<rename_policy_t<Casing::Identity>, policy::identity>));
    ZSTATIC_EXPECT((std::is_same_v<rename_policy_t<Casing::LowerSnake>, policy::lower_snake>));
    ZSTATIC_EXPECT((std::is_same_v<rename_policy_t<Casing::LowerCamel>, policy::lower_camel>));
    ZSTATIC_EXPECT((std::is_same_v<rename_policy_t<Casing::UpperCamel>, policy::upper_camel>));
    ZSTATIC_EXPECT((std::is_same_v<rename_policy_t<Casing::UpperSnake>, policy::upper_snake>));
}

ZEST_CASE(annotate_attaches_struct_spec) {
    using shape = annotate<internal_struct_tag>::type<std::variant<circle_alt, rect_alt>>;
    ZSTATIC_EXPECT((tuple_has_v<typename shape::attrs, attrs::struct_spec<internal_struct_tag>>));
    ZSTATIC_EXPECT((&struct_spec_of<typename shape::attrs> ==
                    &attrs::struct_spec<internal_struct_tag>::value));
    ZSTATIC_EXPECT((&struct_spec_of<std::tuple<>> == &empty_struct_spec));
}

ZEST_CASE(equivalent_untagged_struct_specs_share_type_info) {
    // KANNOTATE mints a fresh tag per use, so identical untagged
    // struct specs live on distinct tags; the type_info instance is keyed by
    // the spec values and must be shared.
    using lhs = annotate<config_struct_tag>::type<inner_pair>;
    using rhs = annotate<config_struct_tag_twin>::type<inner_pair>;
    ZSTATIC_EXPECT((&type_info_of<lhs>() == &type_info_of<rhs>()));

    // A spec whose values match the defaults collapses to the bare type.
    using noop = annotate<noop_struct_tag>::type<inner_pair>;
    ZSTATIC_EXPECT((&type_info_of<noop>() == &type_info_of<inner_pair>()));
}

ZEST_CASE(variant_type_info_carries_struct_spec) {
    using shape = annotate<internal_struct_tag>::type<std::variant<circle_alt, rect_alt>>;
    const auto& info = static_cast<const variant_type_info&>(type_info_of<shape>());
    ZEXPECT(info.tagging == tag_mode::internal);
    ZEXPECT(info.tag_field == "kind");
    ZEXPECT(info.content_field == "");
    ZASSERT(info.alt_names.size() == 2u);
    ZEXPECT(info.alt_names[0] == "circle");
    ZEXPECT(info.alt_names[1] == "rect");

    // Without tag_names the alternatives fall back to their type names.
    using adjacent = annotate<adjacent_struct_tag>::type<std::variant<circle_alt, rect_alt>>;
    const auto& adj = static_cast<const variant_type_info&>(type_info_of<adjacent>());
    ZEXPECT(adj.content_field == "c");
    ZASSERT(adj.alt_names.size() == 2u);
    ZEXPECT(adj.alt_names[0] == "circle_alt");

    // An untagged variant exposes no alternative names.
    const auto& bare =
        static_cast<const variant_type_info&>(type_info_of<std::variant<circle_alt, rect_alt>>());
    ZEXPECT(bare.tagging == tag_mode::none);
    ZEXPECT(bare.alt_names.size() == 0u);
}

ZEST_CASE(skip_when_evaluates_builtin_predicates) {
    ZSTATIC_EXPECT(evaluate_skip_when<skip_when::none>(std::optional<int>{}, true));
    ZSTATIC_EXPECT(!evaluate_skip_when<skip_when::none>(std::optional<int>{1}, true));
    ZSTATIC_EXPECT(evaluate_skip_when<skip_when::empty>(std::string_view{}, true));
    ZSTATIC_EXPECT(!evaluate_skip_when<skip_when::empty>(std::string_view{"x"}, true));
    ZSTATIC_EXPECT(evaluate_skip_when<skip_when::default_value>(0, true));
    ZSTATIC_EXPECT(!evaluate_skip_when<skip_when::default_value>(1, true));
    // Deserialization never skips.
    ZSTATIC_EXPECT(!evaluate_skip_when<skip_when::default_value>(0, false));
    // The default value is value-initialized: `{}` would copy-list-initialize
    // the explicit member.
    auto fresh = only_value_initialized();
    ZEXPECT(evaluate_skip_when<skip_when::default_value>(fresh, true));
    fresh.count = 1;
    ZEXPECT(!evaluate_skip_when<skip_when::default_value>(fresh, true));
}

};  // ZEST_SUITE(meta_spec)

}  // namespace

}  // namespace kotatsu::meta
