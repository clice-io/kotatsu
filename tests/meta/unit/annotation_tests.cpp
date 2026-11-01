#include <cstdint>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/annotation.h"

// Annotations of a module of the test's own, written as KANNOTATE and KANNOTATE_TYPE (in
// codec's macro sheet) expand to.

namespace kotatsu::meta {

namespace {

/// The attributes of the test's module.
enum class knob : std::uint8_t { label, weight, low, high };

/// The spec they set.
struct Range {
    std::string_view label;
    int weight = 0;
    int low = 0;
    int high = 0;
};

}  // namespace

/// low and high override one another as a whole, and label annotates a field alone.
template <>
struct attribute_traits<knob> {
    constexpr static int group(knob kind) {
        return kind == knob::low || kind == knob::high ? 1 : 0;
    }

    constexpr static bool field_only(knob kind) {
        return kind == knob::label;
    }
};

namespace {

[[maybe_unused]] constexpr inline dsl::value_proxy<knob::label, &Range::label, std::string_view>
    label{};
[[maybe_unused]] constexpr inline dsl::value_proxy<knob::weight, &Range::weight, int> weight{};
[[maybe_unused]] constexpr inline dsl::value_proxy<knob::low, &Range::low, int> low{};
[[maybe_unused]] constexpr inline dsl::value_proxy<knob::high, &Range::high, int> high{};

/// A spec of another module, whose attributes member_proxy tells apart by member.
struct Note {
    std::string_view text;
};

[[maybe_unused]] constexpr inline dsl::member_proxy<&Note::text> text{};

struct labeled_tag {
    constexpr static auto attributes = make_annotation(label = "a", weight = 2, text = "n");
};

[[maybe_unused]] constexpr inline auto heavy = make_annotation(label = "bundled", weight = 9);

struct bundled_tag {
    constexpr static auto attributes = make_annotation(heavy, label = "own");
};

struct Weighed;

constexpr auto __kotatsu__type_annotation(std::type_identity<Weighed>) {
    return make_annotation(weight = 1, low = 1, high = 5);
}

struct Weighed {
    int value = 0;
};

struct Holder {
    Weighed plain;

    static consteval auto __kotatsu__annotation(name_key<"narrowed">) {
        return make_annotation(label = "narrowed", low = 2);
    }

    Weighed narrowed;
};

ZEST_SUITE(meta_annotation) {

ZEST_CASE(attributes_fold_into_their_specs) {
    constexpr const Range& range = spec_attr<labeled_tag, Range>::value;
    ZSTATIC_EXPECT(range.label == "a");
    ZSTATIC_EXPECT(range.weight == 2);
    ZSTATIC_EXPECT(spec_attr<labeled_tag, Note>::value.text == "n");
    // One spec_attr for each spec type the attributes set.
    ZSTATIC_EXPECT(
        (std::is_same_v<annotation_attrs_t<labeled_tag>,
                        std::tuple<spec_attr<labeled_tag, Range>, spec_attr<labeled_tag, Note>>>));
}

ZEST_CASE(attributes_written_beside_a_bundle_override_it) {
    constexpr const Range& range = spec_attr<bundled_tag, Range>::value;
    ZSTATIC_EXPECT(range.label == "own");
    ZSTATIC_EXPECT(range.weight == 9);
}

ZEST_CASE(attributes_are_told_apart_by_kind_and_spec) {
    ZSTATIC_EXPECT(detail::attributes_unique<decltype(label = "a"), decltype(weight = 1)>());
    ZSTATIC_EXPECT(!detail::attributes_unique<decltype(label = "a"), decltype(label = "b")>());
    ZSTATIC_EXPECT(detail::attributes_unique<decltype(label = "a"), decltype(text = "b")>());
}

ZEST_CASE(a_group_is_overridden_as_a_whole) {
    constexpr auto composed =
        detail::compose_attributes(make_annotation(low = 1, high = 5, weight = 3),
                                   make_annotation(low = 2));
    constexpr Range range = detail::spec_from<Range>(composed);
    ZSTATIC_EXPECT(range.low == 2);
    ZSTATIC_EXPECT(range.high == 0);
    ZSTATIC_EXPECT(range.weight == 3);
}

ZEST_CASE(a_field_annotation_composes_over_its_type_one) {
    ZSTATIC_EXPECT(spec_in<Range, attrs_of_t<Weighed>>->weight == 1);
    // A plain field keeps its type's attrs as they are.
    ZSTATIC_EXPECT((std::is_same_v<field_attrs_t<Holder, 0>, attrs_of_t<Weighed>>));
    constexpr const Range* narrowed = spec_in<Range, field_attrs_t<Holder, 1>>;
    ZSTATIC_EXPECT(narrowed->label == "narrowed");
    ZSTATIC_EXPECT(narrowed->weight == 1);
    ZSTATIC_EXPECT(narrowed->low == 2);
    ZSTATIC_EXPECT(narrowed->high == 0);
    ZSTATIC_EXPECT(spec_in<Note, field_attrs_t<Holder, 1>> == nullptr);
}

ZEST_CASE(a_type_annotation_takes_no_field_only_attribute) {
    ZSTATIC_EXPECT(detail::type_attributes(
        std::type_identity<std::remove_const_t<decltype(make_annotation(weight = 1))>>{}));
    ZSTATIC_EXPECT(!detail::type_attributes(
        std::type_identity<std::remove_const_t<decltype(make_annotation(label = "a"))>>{}));
}

template <typename Spec>
struct is_note : std::is_same<Spec, Note> {};

ZEST_CASE(spec_refs_find_specs_by_type) {
    constexpr auto refs = spec_refs_of<annotation_attrs_t<labeled_tag>, is_note>;
    ZSTATIC_EXPECT(refs.size() == 1U);
    ZSTATIC_EXPECT(refs[0].holds<Note>());
    ZSTATIC_EXPECT(!refs[0].holds<Range>());
    ZSTATIC_EXPECT(refs[0].get<Note>().text == "n");
}

};  // ZEST_SUITE(meta_annotation)

}  // namespace

}  // namespace kotatsu::meta
