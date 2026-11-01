#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

#include "fixtures/attrs.h"
#include "fixtures/repr.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/type_info.h"

// How type_info follows codec::repr: through chains, annotations and tagging
// nested in declared representations, and format scopes. The codec follows
// the same resolution when it encodes, which the codec kit's repr area checks.
// The annotations are written as KANNOTATE and KANNOTATE_TYPE (in codec's
// macro sheet) expand to.

namespace kotatsu::codec {

using meta::field_attrs_t;
using meta::make_annotation;
using meta::name_key;
using meta::type_kind;

namespace {

struct MergePoint {
    int x_val = 0;
};

/// A struct whose field merges a policy of its own below its presence_all.
struct RenamedInside;

constexpr auto __kotatsu__type_annotation(std::type_identity<RenamedInside>) {
    return make_annotation(dsl::presence_all = presence_kind::optional);
}

struct RenamedInside {
    static consteval auto __kotatsu__annotation(name_key<"inner">) {
        return make_annotation(dsl::rename_all = naming::Casing::LowerCamel);
    }

    MergePoint inner;
};

/// "camel, then deny" across two chained reprs, and both on one: each step
/// is annotated, and declares the next.
struct DenyStep;

constexpr auto __kotatsu__type_annotation(std::type_identity<DenyStep>) {
    return make_annotation(dsl::deny_unknown_fields = true);
}

struct DenyStep {};

struct CamelStep;

constexpr auto __kotatsu__type_annotation(std::type_identity<CamelStep>) {
    return make_annotation(dsl::rename_all = naming::Casing::LowerCamel);
}

struct CamelStep {};

struct StrictStep;

constexpr auto __kotatsu__type_annotation(std::type_identity<StrictStep>) {
    return test::StrictCamelTag;
}

struct StrictStep {};

struct ChainedPolicies {};

struct FlatPolicies {};

/// LoadResult under a field's rename_all and deny_unknown_fields.
struct StrictLoad {
    static consteval auto __kotatsu__annotation(name_key<"value">) {
        return test::StrictCamelTag;
    }

    test::LoadResult value;
};

/// rename_all on a variant that is not tagged.
struct CamelChoice;

constexpr auto __kotatsu__type_annotation(std::type_identity<CamelChoice>) {
    return make_annotation(dsl::rename_all = naming::Casing::LowerCamel);
}

struct CamelChoice : std::variant<test::RenameTarget, int> {
    using variant::variant;
};

/// A format tag no backend declares.
struct TestFormat {};

}  // namespace

template <>
struct repr<DenyStep> {
    using type = MergePoint;
};

template <>
struct repr<CamelStep> {
    using type = DenyStep;
};

template <>
struct repr<ChainedPolicies> {
    using type = CamelStep;
};

template <>
struct repr<StrictStep> {
    using type = MergePoint;
};

template <>
struct repr<FlatPolicies> {
    using type = StrictStep;
};

namespace {

ZEST_SUITE(codec_schema_type_info_repr) {

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

ZEST_CASE(struct_level_attributes_reach_their_own_members_only) {
    // RenamedInside's presence_all marks its own members, not those of the
    // struct below, which the field's rename_all names.
    const auto& info = static_cast<const struct_type_info&>(type_info_of<RenamedInside>());
    ZASSERT(info.fields.size() == 1U);
    ZEXPECT(info.fields[0].presence == presence_kind::optional);
    const auto& inner = static_cast<const struct_type_info&>(info.fields[0].type());
    ZASSERT(inner.fields.size() == 1U);
    ZEXPECT(inner.fields[0].name == "xVal");
    ZEXPECT(inner.fields[0].presence == presence_kind::required);
}

ZEST_CASE(tagging_in_declared_repr_reaches_type_info) {
    const auto& info = type_info_of<test::LoadResult>();
    ZASSERT(info.kind == type_kind::variant);
    const auto& variant = static_cast<const variant_type_info&>(info);
    ZEXPECT(variant.tagging == tag_mode::adjacent);
    ZEXPECT(variant.tag_field == "status");
}

ZEST_CASE(outer_policy_reaches_repr_alternatives) {
    const auto& holder = static_cast<const struct_type_info&>(type_info_of<StrictLoad>());
    ZASSERT(holder.fields.size() == 1U);
    const auto& info = holder.fields[0].type();
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
    using resolved = decltype(detail::resolve_node<std::variant<int, std::string>,
                                                   field_attrs_t<test::AdaptedChoice, 0>>());
    ZEXPECT(zest::type_eq<typename resolved::type, std::string>());
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
    const auto& erased = type_info_of<CamelChoice>();
    ZASSERT(erased.kind == type_kind::variant);
    const auto& info = static_cast<const variant_type_info&>(erased);
    ZASSERT(info.alternatives.size() == 2U);
    const auto& alternative = static_cast<const struct_type_info&>(info.alternatives[0]());
    ZASSERT(alternative.fields.size() == 2U);
    ZEXPECT(alternative.fields[0].name == "user_name");
}

};  // ZEST_SUITE(codec_schema_type_info_repr)

}  // namespace

}  // namespace kotatsu::codec
