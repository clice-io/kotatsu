#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/annotation.h"

namespace kotatsu::meta {

namespace {

/// An aggregate with a std::formatter of its own.
struct Labeled {
    int value;
};

}  // namespace

}  // namespace kotatsu::meta

template <>
struct std::formatter<kotatsu::meta::Labeled> : std::formatter<int> {
    template <typename Context>
    auto format(const kotatsu::meta::Labeled& labeled, Context& ctx) const {
        ctx.advance_to(std::format_to(ctx.out(), "labeled "));
        return std::formatter<int>::format(labeled.value, ctx);
    }
};

namespace kotatsu::meta {

namespace {

/// Has no std::formatter.
struct Unformattable {
    int value;
};

/// The spec of an extension, a module beside meta with entries of its own.
struct Probe {
    std::string_view label;
    int weight = 0;
};

[[maybe_unused]] constexpr inline dsl::member_proxy<&Probe::label> label{};
[[maybe_unused]] constexpr inline dsl::member_proxy<&Probe::weight> weight{};

struct inner_tag {
    constexpr static auto entries = make_annotation(dsl::description = "inner",
                                                    dsl::alias = {"in"},
                                                    dsl::as = dsl::type<std::int64_t>,
                                                    label = "probe",
                                                    weight = 3);
};

struct outer_tag {
    constexpr static auto entries =
        make_annotation(dsl::description = "outer", dsl::rename = "renamed", weight = 5);
};

struct Adapter {};

struct with_tag {
    constexpr static auto entries = make_annotation(dsl::with = dsl::type<Adapter>);
};

struct skip_empty_tag {
    constexpr static auto entries = make_annotation(dsl::skip_if = skip_when::empty);
};

struct Negative {
    constexpr bool operator()(const int& value) const {
        return value < 0;
    }
};

struct skip_negative_tag {
    constexpr static auto entries = make_annotation(dsl::skip_if = dsl::type<Negative>);
};

struct ten_aliases_tag {
    constexpr static auto entries =
        make_annotation(dsl::alias = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"});
};

template <int I>
struct Alt {};

struct nine_names_tag {
    constexpr static auto entries =
        make_annotation(dsl::tagged = true,
                        dsl::tag_names = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8"});
};

/// Field entries and struct entries in one annotation.
struct mixed_tag {
    constexpr static auto entries = make_annotation(dsl::rename = "shape", dsl::tag = "kind");
};

/// Annotated above it, as KANNOTATE_TYPE (in codec's macro sheet) writes it.
struct Weighed;

constexpr auto kotatsu_type_annotation(std::type_identity<Weighed>) {
    return make_annotation(dsl::description = "weighed", label = "type", weight = 1);
}

struct Weighed {
    int value = 0;
};

struct HoldsWeighed {
    Weighed plain;
};

struct heavier_tag {
    constexpr static auto entries = make_annotation(dsl::description = "heavier", weight = 9);
};

struct never_skipped_tag {
    constexpr static auto entries = make_annotation(dsl::skip_if = skip_when::never);
};

struct kind_tag {
    constexpr static auto entries = make_annotation(dsl::tag = "kind");
};

struct externally_tagged_tag {
    constexpr static auto entries = make_annotation(dsl::tagged = true);
};

ZEST_SUITE(meta_annotation) {

ZEST_CASE(class_wrapper_takes_the_wrapped_value) {
    std::optional<std::string> text = "a";
    annotation<std::optional<std::string>> copied = text;
    ZASSERT(copied);
    ZEXPECT(*copied == "a");

    annotation<std::unique_ptr<int>> owned = std::make_unique<int>(1);
    ZASSERT(owned != nullptr);
    ZEXPECT(*owned == 1);

    static_assert(
        !std::is_constructible_v<annotation<std::unique_ptr<int>>, const std::unique_ptr<int>&>);
}

ZEST_CASE(formats_as_the_annotated_value) {
    // A wrapped scalar, and a class it inherits from, with the value's spec.
    annotation<int> count = 42;
    ZEXPECT(std::format("{:>4}", count) == "  42");
    annotation<bool> flag = true;
    ZEXPECT(std::format("{}", flag) == "true");
    annotation<std::string> name = std::string("clice");
    ZEXPECT(std::format("{}|{:*^9}", name, name) == "clice|**clice**");
    ZSTATIC_EXPECT(!std::formattable<annotation<Unformattable>, char>);
}

ZEST_CASE(annotation_of_an_annotation_annotates_the_type_inside) {
    using twice = annotate<outer_tag>::type<annotate<inner_tag>::type<int>>;
    ZSTATIC_EXPECT((std::is_same_v<typename twice::annotated_type, int>));
    ZSTATIC_EXPECT((std::is_same_v<raw_type_t<twice>, int>));
}

ZEST_CASE(outer_entries_override_inner_ones) {
    using twice = annotate<outer_tag>::type<annotate<inner_tag>::type<int>>;
    constexpr const field_spec& spec = spec_of<attrs_of_t<twice>>;
    ZSTATIC_EXPECT(spec.description == "outer");
    // What only one of them says stays.
    ZSTATIC_EXPECT(spec.rename == "renamed");
    ZSTATIC_EXPECT(spec.alias.size() == 1u);
    ZSTATIC_EXPECT(spec.alias[0] == "in");
    ZSTATIC_EXPECT(extension_in<Probe, attrs_of_t<twice>>->label == "probe");
    ZSTATIC_EXPECT(extension_in<Probe, attrs_of_t<twice>>->weight == 5);
    ZSTATIC_EXPECT((tuple_has_v<typename twice::attrs, behavior::as<std::int64_t>>));
}

ZEST_CASE(outer_representation_overrides_inner_one) {
    using twice = annotate<with_tag>::type<annotate<inner_tag>::type<int>>;
    using attrs_t = typename twice::attrs;
    ZSTATIC_EXPECT((tuple_has_v<attrs_t, behavior::with<Adapter>>));
    ZSTATIC_EXPECT(!(tuple_has_v<attrs_t, behavior::as<std::int64_t>>));
}

ZEST_CASE(outer_skip_condition_overrides_inner_one) {
    using predicate_over_value =
        annotate<skip_negative_tag>::type<annotate<skip_empty_tag>::type<int>>;
    ZSTATIC_EXPECT(spec_of<attrs_of_t<predicate_over_value>>.skip_if == skip_when::never);
    ZSTATIC_EXPECT(
        (tuple_has_v<typename predicate_over_value::attrs, behavior::skip_if<Negative>>));

    using value_over_predicate =
        annotate<skip_empty_tag>::type<annotate<skip_negative_tag>::type<int>>;
    ZSTATIC_EXPECT(spec_of<attrs_of_t<value_over_predicate>>.skip_if == skip_when::empty);
    ZSTATIC_EXPECT(
        !(tuple_has_v<typename value_over_predicate::attrs, behavior::skip_if<Negative>>));
}

ZEST_CASE(annotation_of_a_bare_annotation_keeps_its_behavior) {
    using twice = annotate<outer_tag>::type<annotation<int, behavior::as<std::int64_t>>>;
    ZSTATIC_EXPECT((tuple_has_v<typename twice::attrs, behavior::as<std::int64_t>>));
    ZSTATIC_EXPECT(spec_of<attrs_of_t<twice>>.rename == "renamed");
}

ZEST_CASE(annotation_of_an_annotation_reads_the_value_inside) {
    annotate<outer_tag>::type<annotate<inner_tag>::type<int>> value = 7;
    ZEXPECT(annotated_value(value) == 7);
    value = 8;
    ZEXPECT(annotated_value(value) == 8);
}

ZEST_CASE(name_lists_take_any_number_of_names) {
    constexpr const field_spec& spec = attrs::spec<ten_aliases_tag>::value;
    ZSTATIC_EXPECT(spec.alias.size() == 10u);
    ZSTATIC_EXPECT(spec.alias[9] == "j");
    constexpr auto names = resolve_tag_names<attrs::struct_spec<nine_names_tag>,
                                             Alt<0>,
                                             Alt<1>,
                                             Alt<2>,
                                             Alt<3>,
                                             Alt<4>,
                                             Alt<5>,
                                             Alt<6>,
                                             Alt<7>,
                                             Alt<8>>();
    ZSTATIC_EXPECT(names[8] == "a8");
}

ZEST_CASE(one_annotation_takes_field_and_struct_entries) {
    using shape = annotate<mixed_tag>::type<std::variant<Alt<0>, Alt<1>>>;
    ZSTATIC_EXPECT(spec_of<attrs_of_t<shape>>.rename == "shape");
    ZSTATIC_EXPECT(struct_spec_of<typename shape::attrs>.tagging == tag_mode::internal);
}

ZEST_CASE(field_annotation_overrides_its_type_annotation) {
    ZSTATIC_EXPECT(spec_of<attrs_of_t<Weighed>>.description == "weighed");
    ZSTATIC_EXPECT(extension_in<Probe, attrs_of_t<Weighed>>->weight == 1);
    using heavier = annotate<heavier_tag>::type<Weighed>;
    ZSTATIC_EXPECT(spec_of<attrs_of_t<heavier>>.description == "heavier");
    ZSTATIC_EXPECT(extension_in<Probe, attrs_of_t<heavier>>->label == "type");
    ZSTATIC_EXPECT(extension_in<Probe, attrs_of_t<heavier>>->weight == 9);
}

ZEST_CASE(type_annotation_reaches_a_plain_field) {
    constexpr const auto& info = static_cast<const struct_type_info&>(type_info_of<HoldsWeighed>());
    ZSTATIC_EXPECT(info.fields[0].has_extension<Probe>());
    ZEXPECT(info.fields[0].extension<Probe>()->weight == 1);
}

ZEST_CASE(outer_never_overrides_inner_skip_predicate) {
    using twice = annotate<never_skipped_tag>::type<annotate<skip_negative_tag>::type<int>>;
    ZSTATIC_EXPECT(!(tuple_has_v<typename twice::attrs, behavior::skip_if<Negative>>));
    ZSTATIC_EXPECT(spec_of<attrs_of_t<twice>>.skip_if == skip_when::never);
}

ZEST_CASE(bare_skip_predicate_overrides_inner_skip_condition) {
    using twice = annotation<annotate<skip_empty_tag>::type<int>, behavior::skip_if<Negative>>;
    ZSTATIC_EXPECT((tuple_has_v<typename twice::attrs, behavior::skip_if<Negative>>));
    ZSTATIC_EXPECT(spec_of<attrs_of_t<twice>>.skip_if == skip_when::never);
}

ZEST_CASE(outer_tagging_overrides_inner_one) {
    using shape = annotate<kind_tag>::type<std::variant<Alt<0>, Alt<1>>>;
    using twice = annotate<externally_tagged_tag>::type<shape>;
    ZSTATIC_EXPECT(struct_spec_of<typename twice::attrs>.tagging == tag_mode::external);
    ZSTATIC_EXPECT(struct_spec_of<typename twice::attrs>.tag.empty());
}

ZEST_CASE(annotation_of_an_annotation_formats_as_the_value_inside) {
    annotation<annotation<Labeled>> twice{};
    twice.value = 3;
    ZEXPECT(std::format("{}", twice) == "labeled 3");
}

};  // ZEST_SUITE(meta_annotation)

}  // namespace

}  // namespace kotatsu::meta
