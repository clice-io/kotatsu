#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>

#include "codec/harness/fixtures/attrs.h"
#include "fixtures/structs.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/codec/attribute.h"
#include "kotatsu/codec/macro.h"
#include "kotatsu/codec/type_info.h"

// Outside namespace kotatsu, where <cstdio> puts ::rename in scope: the macros
// name their attributes through using-declarations, so `rename` is not ambiguous.
namespace {

struct DownstreamConfig {
    KANNOTATE(&compile_commands, rename = "compileCommands")
    int compile_commands = 0;
};

template <typename T>
struct DownstreamBox {
    KANNOTATE(&value, alias = {"v"})
    T value;
};

/// Named like the struct the namespace below annotates.
struct Options {};

namespace third_party {

/// A struct whose definition the annotation below does not touch.
struct Point {
    int x = 0;
    int y = 0;
};

KANNOTATE_FIELD(&third_party::Point::y, rename = "Y", alias = {"why"});

template <typename A, typename B>
struct Pair {
    A first;
    B second;
};

// The comma in the template arguments takes parentheses.
KANNOTATE_FIELD((&third_party::Pair<int, int>::second), rename = "Second");

}  // namespace third_party

namespace downstream {

// Declares the struct in this namespace, though ::Options is in scope.
KANNOTATE_TYPE(struct Options, description = "Downstream options");

struct Options {
    int jobs = 0;
};

enum class Level : std::uint8_t;
KANNOTATE_TYPE(Level, description = "How loud");

enum class Level : std::uint8_t { Quiet, Loud };

/// The spec of an extension of this namespace's own.
struct Probe {
    int weight = 0;
};

[[maybe_unused]] constexpr inline kotatsu::meta::dsl::member_proxy<&Probe::weight> weight{};

KANNOTATE_TYPE(struct Weighed, weight = 2);

struct Weighed {
    int value = 0;
};

struct HoldsWeighed {
    Weighed plain;
};

}  // namespace downstream

}  // namespace

namespace kotatsu::codec {

namespace {

struct Profile {
    std::string first;
    int age = 0;
};

struct Entries {
    KANNOTATE(&user_id, rename = "id", alias = {"uid"}, description = "Identifier.")
    int user_id;
    KANNOTATE(&internal, skip = true)
    int internal;
    KANNOTATE(&profile, flatten = true)
    Profile profile;
    KANNOTATE(&note, presence = omit_if_default)
    std::optional<int> note;
};

struct Behaviors {
    KANNOTATE(&as_text, as = type<std::string>)
    int as_text;
    KANNOTATE(&with_adapter, with = type<test::DecimalText>)
    int with_adapter;
    KANNOTATE(&omit_negative, presence = type<test::IsNegative>)
    int omit_negative;
};

KANNOTATION(ShapeTag, tag = "kind", tag_names = {"circle", "rect"});
KANNOTATION(StrictCamel, rename_all = Casing::LowerCamel, deny_unknown_fields = true);
KANNOTATION(AllOptional, presence_all = optional);

KANNOTATE_TYPE(struct Shape, ShapeTag);

struct Shape : std::variant<test::Circle, test::Rect> {
    using variant::variant;
};

/// Two bundles in one annotation.
KANNOTATE_TYPE(struct StrictProfile, StrictCamel, AllOptional);

struct StrictProfile {
    std::string first;
    int age = 0;
};

/// A bundle, then an attribute that overrides one of its own.
struct Bundled {
    KANNOTATE(&shape, ShapeTag, tag = "type")
    std::variant<test::Circle, test::Rect> shape;
};

struct DescribedProfile {
    KANNOTATE(&profile, description = "Whose profile.")
    Profile profile;
};

struct StructEntriesOnFields {
    KANNOTATE(&shape, tag = "kind", tag_names = {"circle", "rect"})
    std::variant<test::Circle, test::Rect> shape;
    KANNOTATE(&inner, rename_all = Casing::UpperSnake, deny_unknown_fields = true)
    Profile inner;
};

ZEST_SUITE(codec_macro) {

ZEST_CASE(annotate_works_outside_kotatsu) {
    constexpr const auto& spec = codec::field_spec_of<DownstreamConfig, 0>;
    ZSTATIC_EXPECT(spec.rename == std::string_view("compileCommands"));
}

ZEST_CASE(annotate_field_annotates_a_class_from_outside) {
    constexpr const auto& y = codec::field_spec_of<third_party::Point, 1>;
    ZSTATIC_EXPECT(y.rename == std::string_view("Y"));
    ZSTATIC_EXPECT(y.alias[0] == std::string_view("why"));
    ZSTATIC_EXPECT(codec::field_spec_of<third_party::Point, 0>.rename.empty());
}

ZEST_CASE(annotate_field_takes_a_parenthesized_member) {
    ZSTATIC_EXPECT((codec::field_spec_of<third_party::Pair<int, int>, 1>.rename == "Second"));
    ZSTATIC_EXPECT((codec::field_spec_of<third_party::Pair<int, int>, 0>.rename.empty()));
}

ZEST_CASE(annotate_type_declares_the_struct_where_it_is_written) {
    ZSTATIC_EXPECT(
        codec::spec_of<codec::field_spec, meta::attrs_of_t<downstream::Options>>.description ==
        std::string_view("Downstream options"));
    ZSTATIC_EXPECT(
        codec::spec_of<codec::field_spec, meta::attrs_of_t<::Options>>.description.empty());
}

ZEST_CASE(annotate_type_follows_an_opaque_enum_declaration) {
    ZSTATIC_EXPECT(
        codec::spec_of<codec::field_spec, meta::attrs_of_t<downstream::Level>>.description ==
        std::string_view("How loud"));
}

ZEST_CASE(annotate_type_carries_extension_attributes_to_plain_fields) {
    constexpr const auto& info = static_cast<const codec::struct_type_info&>(
        codec::type_info::of<downstream::HoldsWeighed>());
    ZSTATIC_EXPECT(info.fields[0].has_extension<downstream::Probe>());
    ZEXPECT(info.fields[0].extension<downstream::Probe>()->weight == 2);
}

ZEST_CASE(annotate_works_in_a_class_template) {
    constexpr const auto& spec = codec::field_spec_of<DownstreamBox<int>, 0>;
    ZSTATIC_EXPECT(spec.alias.size() == 1U);
    ZSTATIC_EXPECT(spec.alias[0] == std::string_view("v"));
}

ZEST_CASE(annotate_field_attributes_make_a_field_spec) {
    constexpr const auto& id = codec::field_spec_of<Entries, 0>;
    ZSTATIC_EXPECT(id.rename == std::string_view("id"));
    ZSTATIC_EXPECT(id.alias[0] == std::string_view("uid"));
    ZSTATIC_EXPECT(id.description == std::string_view("Identifier."));
    ZSTATIC_EXPECT(codec::field_spec_of<Entries, 1>.skip);
    ZSTATIC_EXPECT(codec::field_spec_of<Entries, 2>.flatten);
    constexpr const auto& note = codec::field_spec_of<Entries, 3>;
    ZSTATIC_EXPECT(note.presence == codec::presence_kind::omit_if_default);
}

ZEST_CASE(annotate_type_attributes_become_behavior_attrs) {
    using as_attrs = meta::field_attrs_t<Behaviors, 0>;
    ZSTATIC_EXPECT(tuple_has_v<as_attrs, codec::behavior::as<std::string>>);
    using with_attrs = meta::field_attrs_t<Behaviors, 1>;
    ZSTATIC_EXPECT(tuple_has_v<with_attrs, codec::behavior::with<test::DecimalText>>);
    using omit_attrs = meta::field_attrs_t<Behaviors, 2>;
    ZSTATIC_EXPECT(tuple_has_v<omit_attrs, codec::behavior::omit_if<test::IsNegative>>);
}

ZEST_CASE(annotation_struct_attributes_make_a_struct_spec) {
    constexpr const auto& shape = codec::spec_of<codec::struct_spec, meta::attrs_of_t<Shape>>;
    ZSTATIC_EXPECT(shape.tagging == codec::tag_mode::internal);
    ZSTATIC_EXPECT(shape.tag == std::string_view("kind"));
    ZSTATIC_EXPECT(shape.tag_names[1] == std::string_view("rect"));

    using strict_attrs = meta::attrs_of_t<StrictProfile>;
    constexpr const auto& strict = codec::spec_of<codec::struct_spec, strict_attrs>;
    ZEXPECT(zest::type_eq<naming::rename_policy_of_t<*strict.rename_all>,
                          naming::rename_policy::lower_camel>());
    ZSTATIC_EXPECT(strict.deny_unknown_fields);
    ZSTATIC_EXPECT(strict.presence_all == codec::presence_kind::optional);
    // Struct attributes alone make the struct spec and nothing else.
    ZSTATIC_EXPECT(std::tuple_size_v<strict_attrs> == 1U);
}

ZEST_CASE(attributes_after_a_bundle_override_its_own) {
    constexpr const auto& shape =
        codec::spec_of<codec::struct_spec, meta::field_attrs_t<Bundled, 0>>;
    ZSTATIC_EXPECT(shape.tag == std::string_view("type"));
    ZSTATIC_EXPECT(shape.tag_names[0] == std::string_view("circle"));
}

ZEST_CASE(annotate_takes_struct_attributes_on_a_field) {
    constexpr const auto& shape =
        codec::spec_of<codec::struct_spec, meta::field_attrs_t<StructEntriesOnFields, 0>>;
    ZSTATIC_EXPECT(shape.tagging == codec::tag_mode::internal);
    ZSTATIC_EXPECT(shape.tag == std::string_view("kind"));
    constexpr const auto& inner =
        codec::spec_of<codec::struct_spec, meta::field_attrs_t<StructEntriesOnFields, 1>>;
    ZEXPECT(zest::type_eq<naming::rename_policy_of_t<*inner.rename_all>,
                          naming::rename_policy::upper_snake>());
    ZSTATIC_EXPECT(inner.deny_unknown_fields);
}

ZEST_CASE(field_annotation_shares_type_info) {
    // A field spec is local to its field: it does not fork the type_info of
    // the type it annotates.
    const auto& info =
        static_cast<const codec::struct_type_info&>(codec::type_info::of<DescribedProfile>());
    ZEXPECT(&info.fields[0].type() == &codec::type_info::of<Profile>());
}

};  // ZEST_SUITE(codec_macro)

}  // namespace

}  // namespace kotatsu::codec
