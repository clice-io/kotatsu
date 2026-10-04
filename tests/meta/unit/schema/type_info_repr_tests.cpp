#include <cstdint>
#include <string>
#include <variant>

#include "fixtures/attrs.h"
#include "fixtures/repr.h"
#include "kota/zest/zest.h"
#include "kota/meta/schema.h"

// How type_info follows meta::repr: through chains, annotations and tagging
// nested in declared representations, and format scopes. The codec follows
// the same resolution when it encodes, which the codec kit's repr area checks.

namespace kota::meta {

namespace {

struct MergePoint {
    int x_val = 0;
};

struct CamelOnly {
    constexpr static auto spec = make_struct_spec(dsl::rename_all = naming::Casing::LowerCamel);
};

struct DenyOnly {
    constexpr static auto spec = make_struct_spec(dsl::deny_unknown_fields = true);
};

struct DefaultedOnly {
    constexpr static auto spec = make_struct_spec(dsl::defaulted_fields = true);
};

/// A struct whose field merges a policy of its own below a defaulted one.
struct RenamedInside {
    annotate<CamelOnly>::type<MergePoint> inner;
};

/// "camel, then deny" across two chained reprs, and both on one.
struct DenyStep {};

struct ChainedPolicies {};

struct FlatPolicies {};

/// A format tag no backend declares.
struct TestFormat {};

}  // namespace

template <>
struct repr<DenyStep> {
    using type = annotate<DenyOnly>::type<MergePoint>;
};

template <>
struct repr<ChainedPolicies> {
    using type = annotate<CamelOnly>::type<DenyStep>;
};

template <>
struct repr<FlatPolicies> {
    using type = annotate<test::StrictCamelTag>::type<MergePoint>;
};

namespace {

ZEST_SUITE(meta_schema_type_info_repr) {

ZEST_CASE(equivalent_merge_chains_share_type_info) {
    // Both routes merge to one config, so they share one type_info and
    // describe one document.
    const auto& chained = type_info_of<ChainedPolicies>();
    const auto& flat = type_info_of<FlatPolicies>();
    ZEXPECT(&chained == &flat);

    ZASSERT(chained.kind == type_kind::structure);
    const auto& info = static_cast<const struct_type_info&>(chained);
    ZEXPECT(info.deny_unknown);
    ZASSERT(info.fields.size() == 1U);
    ZEXPECT(info.fields[0].name == "xVal");
}

ZEST_CASE(defaulted_fields_stays_through_a_deeper_merge) {
    // Like deny, defaulted is sticky: the rename merged below it keeps it.
    const auto& info = static_cast<const struct_type_info&>(
        type_info_of<annotate<DefaultedOnly>::type<RenamedInside>>());
    ZASSERT(info.fields.size() == 1U);
    ZEXPECT(info.fields[0].has_default);
    const auto& inner = static_cast<const struct_type_info&>(info.fields[0].type());
    ZASSERT(inner.fields.size() == 1U);
    ZEXPECT(inner.fields[0].name == "xVal");
    ZEXPECT(inner.fields[0].has_default);
}

ZEST_CASE(annotation_in_declared_repr_resolves) {
    ZEXPECT(zest::type_eq<resolved_repr_t<test::BasisPoints>, double>());
}

ZEST_CASE(tagging_in_declared_repr_reaches_type_info) {
    const auto& info = type_info_of<test::LoadResult>();
    ZASSERT(info.kind == type_kind::variant);
    const auto& variant = static_cast<const variant_type_info&>(info);
    ZEXPECT(variant.tagging == tag_mode::adjacent);
    ZEXPECT(variant.tag_field == "status");
}

ZEST_CASE(outer_policy_reaches_repr_alternatives) {
    const auto& info = type_info_of<annotate<test::StrictCamelTag>::type<test::LoadResult>>();
    ZASSERT(info.kind == type_kind::variant);
    const auto& variant = static_cast<const variant_type_info&>(info);
    ZEXPECT(variant.tagging == tag_mode::adjacent);
    ZASSERT(variant.alternatives.size() == 2U);
    const auto& ok = static_cast<const struct_type_info&>(variant.alternatives[0]());
    ZEXPECT(ok.deny_unknown);
    ZASSERT(ok.fields.size() == 1U);
    ZEXPECT(ok.fields[0].name == "byteCount");
}

ZEST_CASE(adapter_beats_tagging_in_resolution) {
    ZEXPECT(zest::type_eq<resolved_repr_t<test::AdaptedChoice>, std::string>());
}

ZEST_CASE(tagging_beats_the_variants_repr_in_resolution) {
    ZEXPECT(zest::type_eq<resolved_repr_t<test::TaggedStampOrNote>, test::StampOrNote>());
    ZEXPECT(resolves_to_tagged_variant<test::TaggedStampOrNote>);
    // Untagged, the variant's repr applies.
    ZEXPECT(zest::type_eq<resolved_repr_t<test::StampOrNote>, std::string>());
}

ZEST_CASE(format_scoped_repr_resolves_under_its_format) {
    ZEXPECT(zest::type_eq<resolved_repr_t<test::Journal>, std::string>());
    ZEXPECT(zest::type_eq<resolved_repr_t<test::Journal, TestFormat>, std::int64_t>());
}

ZEST_CASE(rename_all_on_untagged_variant_is_inert) {
    // rename_all merges only at a reflected struct; an untagged variant is not
    // one, so its alternatives keep their names.
    using choice = annotate<CamelOnly>::type<std::variant<test::RenameTarget, int>>;
    const auto& erased = type_info_of<choice>();
    ZASSERT(erased.kind == type_kind::variant);
    const auto& info = static_cast<const variant_type_info&>(erased);
    ZASSERT(info.alternatives.size() == 2U);
    const auto& alternative = static_cast<const struct_type_info&>(info.alternatives[0]());
    ZASSERT(alternative.fields.size() == 2U);
    ZEXPECT(alternative.fields[0].name == "user_name");
}

};  // ZEST_SUITE(meta_schema_type_info_repr)

}  // namespace

}  // namespace kota::meta
