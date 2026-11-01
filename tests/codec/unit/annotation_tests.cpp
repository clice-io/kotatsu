#include <string_view>
#include <type_traits>
#include <variant>

#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/codec/type_info.h"

// Annotations are written below as KANNOTATE and KANNOTATE_TYPE (in codec's
// macro sheet) expand to.

namespace kotatsu::codec {

using meta::attrs_of_t;
using meta::field_attrs_t;
using meta::make_annotation;
using meta::name_key;

namespace {

/// The spec of an extension, a module beside the codec with attributes of its own.
struct Probe {
    std::string_view label;
    int weight = 0;
};

[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&Probe::label> label{};
[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&Probe::weight> weight{};

struct ten_aliases_tag {
    constexpr static auto attributes =
        make_annotation(dsl::alias = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"});
};

template <int I>
struct Alt {};

struct nine_names_tag {
    constexpr static auto attributes =
        make_annotation(dsl::tagged = true,
                        dsl::tag_names = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8"});
};

struct Weighed;

constexpr auto __kotatsu__type_annotation(std::type_identity<Weighed>) {
    return make_annotation(dsl::description = "weighed",
                           dsl::presence = presence_kind::optional,
                           label = "type",
                           weight = 1);
}

struct Weighed {
    int value = 0;
};

struct HoldsWeighed {
    Weighed plain;

    static consteval auto __kotatsu__annotation(name_key<"heavier">) {
        return make_annotation(dsl::description = "heavier", dsl::rename = "renamed", weight = 9);
    }

    Weighed heavier;
};

/// Holds when the value it judges is negative.
struct Negative {
    template <typename T>
    constexpr bool operator()(const T& held) const {
        return held.value < 0;
    }
};

/// Left out where it holds its default.
struct Plain;

constexpr auto __kotatsu__type_annotation(std::type_identity<Plain>) {
    return make_annotation(dsl::presence = presence_kind::omit_if_default);
}

struct Plain {
    int value = 0;

    auto operator==(const Plain&) const -> bool = default;
};

/// Left out where it is negative.
struct Signed;

constexpr auto __kotatsu__type_annotation(std::type_identity<Signed>) {
    return make_annotation(dsl::presence = dsl::type<Negative>);
}

struct Signed {
    int value = 0;
};

struct PresenceOverrides {
    static consteval auto __kotatsu__annotation(name_key<"predicate_over_value">) {
        return make_annotation(dsl::presence = dsl::type<Negative>);
    }

    Plain predicate_over_value;

    static consteval auto __kotatsu__annotation(name_key<"value_over_predicate">) {
        return make_annotation(dsl::presence = presence_kind::omit_if_default);
    }

    Signed value_over_predicate;

    static consteval auto __kotatsu__annotation(name_key<"required_over_predicate">) {
        return make_annotation(dsl::presence = presence_kind::required);
    }

    Signed required_over_predicate;

    static consteval auto __kotatsu__annotation(name_key<"renamed">) {
        return make_annotation(dsl::rename = "renamed");
    }

    Signed renamed;
};

/// Internally tagged wherever it is.
struct Shape;

constexpr auto __kotatsu__type_annotation(std::type_identity<Shape>) {
    return make_annotation(dsl::tag = "kind");
}

struct Shape : std::variant<Alt<0>, Alt<1>> {
    using variant::variant;
};

struct HoldsShapes {
    Shape internal;

    static consteval auto __kotatsu__annotation(name_key<"external">) {
        return make_annotation(dsl::tagged = true);
    }

    Shape external;

    /// Field attributes and struct attributes in one annotation.
    static consteval auto __kotatsu__annotation(name_key<"mixed">) {
        return make_annotation(dsl::rename = "shape", dsl::tag = "kind");
    }

    std::variant<Alt<0>, Alt<1>> mixed;
};

ZEST_SUITE(codec_annotation) {

ZEST_CASE(name_lists_take_any_number_of_names) {
    constexpr const field_spec& spec = meta::spec_attr<ten_aliases_tag, field_spec>::value;
    ZSTATIC_EXPECT(spec.alias.size() == 10u);
    ZSTATIC_EXPECT(spec.alias[9] == "j");
    constexpr auto names = resolve_tag_names<meta::spec_attr<nine_names_tag, struct_spec>,
                                             format,
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

ZEST_CASE(one_annotation_takes_field_and_struct_attributes) {
    using attrs_t = field_attrs_t<HoldsShapes, 2>;
    ZSTATIC_EXPECT(spec_of<field_spec, attrs_t>.rename == "shape");
    ZSTATIC_EXPECT(spec_of<struct_spec, attrs_t>.tagging == tag_mode::internal);
}

ZEST_CASE(field_annotation_overrides_its_type_annotation) {
    ZSTATIC_EXPECT(spec_of<field_spec, attrs_of_t<Weighed>>.description == "weighed");
    ZSTATIC_EXPECT(meta::spec_in<Probe, attrs_of_t<Weighed>>->weight == 1);
    using heavier = field_attrs_t<HoldsWeighed, 1>;
    ZSTATIC_EXPECT(spec_of<field_spec, heavier>.description == "heavier");
    ZSTATIC_EXPECT(meta::spec_in<Probe, heavier>->weight == 9);
    // What only one of them says stays.
    ZSTATIC_EXPECT(spec_of<field_spec, heavier>.rename == "renamed");
    ZSTATIC_EXPECT(spec_of<field_spec, heavier>.presence == presence_kind::optional);
    ZSTATIC_EXPECT(meta::spec_in<Probe, heavier>->label == "type");
}

ZEST_CASE(type_annotation_reaches_a_plain_field) {
    constexpr const auto& info =
        static_cast<const struct_type_info&>(type_info::of<HoldsWeighed>());
    ZSTATIC_EXPECT(info.fields[0].has_extension<Probe>());
    ZEXPECT(info.fields[0].extension<Probe>()->weight == 1);
}

ZEST_CASE(field_presence_overrides_type_one) {
    using predicate_over_value = field_attrs_t<PresenceOverrides, 0>;
    ZSTATIC_EXPECT(spec_of<field_spec, predicate_over_value>.presence == presence_kind::omit_if);
    ZSTATIC_EXPECT((tuple_has_v<predicate_over_value, behavior::omit_if<Negative>>));

    using value_over_predicate = field_attrs_t<PresenceOverrides, 1>;
    ZSTATIC_EXPECT(spec_of<field_spec, value_over_predicate>.presence ==
                   presence_kind::omit_if_default);
    ZSTATIC_EXPECT(!(tuple_has_v<value_over_predicate, behavior::omit_if<Negative>>));
}

ZEST_CASE(field_required_overrides_type_predicate) {
    using required_over_predicate = field_attrs_t<PresenceOverrides, 2>;
    ZSTATIC_EXPECT(!(tuple_has_v<required_over_predicate, behavior::omit_if<Negative>>));
    ZSTATIC_EXPECT(spec_of<field_spec, required_over_predicate>.presence ==
                   presence_kind::required);
}

ZEST_CASE(field_annotation_keeps_its_type_predicate) {
    using renamed = field_attrs_t<PresenceOverrides, 3>;
    ZSTATIC_EXPECT((tuple_has_v<renamed, behavior::omit_if<Negative>>));
    ZSTATIC_EXPECT(spec_of<field_spec, renamed>.presence == presence_kind::omit_if);
    ZSTATIC_EXPECT(spec_of<field_spec, renamed>.rename == "renamed");
}

ZEST_CASE(field_tagging_overrides_type_one) {
    ZSTATIC_EXPECT(spec_of<struct_spec, field_attrs_t<HoldsShapes, 0>>.tagging ==
                   tag_mode::internal);
    using external = field_attrs_t<HoldsShapes, 1>;
    ZSTATIC_EXPECT(spec_of<struct_spec, external>.tagging == tag_mode::external);
    ZSTATIC_EXPECT(spec_of<struct_spec, external>.tag.empty());
}

};  // ZEST_SUITE(codec_annotation)

}  // namespace

}  // namespace kotatsu::codec
