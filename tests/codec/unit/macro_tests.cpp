#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "codec/harness/fixtures/attrs.h"
#include "fixtures/structs.h"
#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/macro.h"

// Outside namespace kota, where <cstdio> puts ::rename in scope: the macros
// name their entries through using-declarations, so `rename` is not ambiguous.
namespace {

struct DownstreamConfig {
    KOTATSU_ANNOTATE(rename = "compileCommands")
    <int> compile_commands = 0;
};

template <typename T>
struct DownstreamBox {
    KOTATSU_ANNOTATE(alias = {"v"})
    <T> value;
};

}  // namespace

namespace kota::codec {

namespace {

enum class Level : std::uint8_t { low, high };

struct Profile {
    std::string first;
    int age = 0;
};

struct Entries {
    KOTATSU_ANNOTATE(rename = "id", alias = {"uid"}, description = "Identifier.")
    <int> user_id;
    KOTATSU_ANNOTATE(skip = true)
    <int> internal;
    KOTATSU_ANNOTATE(flatten = true)
    <Profile> profile;
    KOTATSU_ANNOTATE(defaulted = true, skip_if = skip_when::none)
    <std::optional<int>> note;
};

struct Behaviors {
    KOTATSU_ANNOTATE(as = type<std::string>)
    <int> as_text;
    KOTATSU_ANNOTATE(with = type<test::DecimalText>)
    <int> with_adapter;
    KOTATSU_ANNOTATE(skip_if = type<test::IsNegative>)
    <int> skip_negative;
    KOTATSU_ANNOTATE(enum_string = type<naming::rename_policy::lower_camel>)
    <Level> level;
};

KOTATSU_ANNOTATION(ShapeTag, tag = "kind", tag_names = {"circle", "rect"});
KOTATSU_ANNOTATION(StrictCamel, rename_all = Casing::LowerCamel, deny_unknown_fields = true);
KOTATSU_ANNOTATION(AllDefaulted, defaulted_fields = true);

struct StructEntriesOnFields {
    KOTATSU_ANNOTATE(tag = "kind", tag_names = {"circle", "rect"})
    <std::variant<test::Circle, test::Rect>> shape;
    KOTATSU_ANNOTATE(rename_all = Casing::UpperSnake, deny_unknown_fields = true)
    <Profile> inner;
};

ZEST_SUITE(codec_macro) {

ZEST_CASE(annotate_works_outside_kota) {
    constexpr const auto& spec = meta::field_spec_of<decltype(DownstreamConfig::compile_commands)>;
    ZSTATIC_EXPECT(spec.rename == std::string_view("compileCommands"));
}

ZEST_CASE(annotate_works_in_a_class_template) {
    constexpr const auto& spec = meta::field_spec_of<decltype(DownstreamBox<int>::value)>;
    ZSTATIC_EXPECT(spec.alias.count == 1U);
    ZSTATIC_EXPECT(spec.alias.names()[0] == std::string_view("v"));
}

ZEST_CASE(annotate_field_entries_make_a_field_spec) {
    constexpr const auto& id = meta::field_spec_of<decltype(Entries::user_id)>;
    ZSTATIC_EXPECT(id.rename == std::string_view("id"));
    ZSTATIC_EXPECT(id.alias.names()[0] == std::string_view("uid"));
    ZSTATIC_EXPECT(id.description == std::string_view("Identifier."));
    ZSTATIC_EXPECT(meta::field_spec_of<decltype(Entries::internal)>.skip);
    ZSTATIC_EXPECT(meta::field_spec_of<decltype(Entries::profile)>.flatten);
    constexpr const auto& note = meta::field_spec_of<decltype(Entries::note)>;
    ZSTATIC_EXPECT(note.defaulted);
    ZSTATIC_EXPECT(note.skip_if == meta::skip_when::none);
}

ZEST_CASE(annotate_type_entries_become_behavior_attrs) {
    using as_attrs = decltype(Behaviors::as_text)::attrs;
    ZSTATIC_EXPECT(tuple_has_v<as_attrs, meta::behavior::as<std::string>>);
    using with_attrs = decltype(Behaviors::with_adapter)::attrs;
    ZSTATIC_EXPECT(tuple_has_v<with_attrs, meta::behavior::with<test::DecimalText>>);
    using skip_attrs = decltype(Behaviors::skip_negative)::attrs;
    ZSTATIC_EXPECT(tuple_has_v<skip_attrs, meta::behavior::skip_if<test::IsNegative>>);
    using level_attrs = decltype(Behaviors::level)::attrs;
    ZSTATIC_EXPECT(
        tuple_has_v<level_attrs, meta::behavior::enum_string<naming::rename_policy::lower_camel>>);
}

ZEST_CASE(annotation_struct_entries_make_a_struct_spec) {
    constexpr const auto& shape = ShapeTag::spec;
    ZSTATIC_EXPECT(shape.tagging == meta::tag_mode::internal);
    ZSTATIC_EXPECT(shape.tag == std::string_view("kind"));
    ZSTATIC_EXPECT(shape.tag_names.names()[1] == std::string_view("rect"));

    constexpr const auto& strict = StrictCamel::spec;
    ZSTATIC_EXPECT(strict.rename_all == naming::Casing::LowerCamel);
    ZSTATIC_EXPECT(strict.deny_unknown_fields);
    ZSTATIC_EXPECT(AllDefaulted::spec.defaulted_fields);
    ZEXPECT(zest::type_eq<meta::annotate<StrictCamel>::type<Profile>,
                          meta::annotation<Profile, meta::attrs::struct_spec<StrictCamel>>>());
}

ZEST_CASE(annotate_takes_struct_entries_on_a_field) {
    constexpr const auto& shape =
        meta::struct_spec_of<decltype(StructEntriesOnFields::shape)::attrs>;
    ZSTATIC_EXPECT(shape.tagging == meta::tag_mode::internal);
    ZSTATIC_EXPECT(shape.tag == std::string_view("kind"));
    constexpr const auto& inner =
        meta::struct_spec_of<decltype(StructEntriesOnFields::inner)::attrs>;
    ZSTATIC_EXPECT(inner.rename_all == naming::Casing::UpperSnake);
    ZSTATIC_EXPECT(inner.deny_unknown_fields);
}

ZEST_CASE(field_annotation_shares_type_info) {
    // A field spec is local to its field: it does not fork the type_info of
    // the type it annotates.
    using annotated = decltype(Entries::profile);
    ZEXPECT(&meta::type_info_of<annotated>() == &meta::type_info_of<Profile>());
}

};  // ZEST_SUITE(codec_macro)

}  // namespace

}  // namespace kota::codec
