#pragma once

// Attribute fixtures: rename / skip / alias / flatten / defaulted /
// rename_all / deny_unknown_fields, and the behavior attrs skip_if / with /
// as / enum_string with their combinations. The annotations are written with
// meta's own spec API rather than the codec's annotation macros, so meta's
// tests can use them.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "fixtures/enums.h"
#include "fixtures/structs.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::test {

struct RenameToId {
    constexpr static auto spec = meta::make_spec(meta::dsl::rename = "id");
};

struct Skip {
    constexpr static auto spec = meta::make_spec(meta::dsl::skip = true);
};

struct Flatten {
    constexpr static auto spec = meta::make_spec(meta::dsl::flatten = true);
};

struct AnnotatedStruct {
    meta::annotate<RenameToId>::type<int> user_id;
    meta::annotate<Skip>::type<std::string> internal;
    float value;
};

struct AliasUserId {
    constexpr static auto spec = meta::make_spec(meta::dsl::alias = {"user_id", "userId"});
};

struct AliasStruct {
    meta::annotate<AliasUserId>::type<int> id;
    std::string name;
};

struct Inner {
    int a;
    int b;
};

struct Outer {
    int x;
    meta::annotate<Flatten>::type<Inner> inner;
    int y;
};

struct FlattenTailStruct {
    int head;
    int neck;
    meta::annotate<Flatten>::type<Inner> body;
};

struct DeepInner {
    int p;
    int q;
};

struct Middle {
    int m;
    meta::annotate<Flatten>::type<DeepInner> deep;
};

struct DeepOuter {
    int head;
    meta::annotate<Flatten>::type<Middle> mid;
    int tail;
};

struct FlattenInnerWithSkip {
    int keep_a;
    meta::annotate<Skip>::type<int> drop_b;
    int keep_c;
};

struct FlattenOuterWithChildSkip {
    int head;
    meta::annotate<Flatten>::type<FlattenInnerWithSkip> inner;
};

struct RenameToRenamedA {
    constexpr static auto spec = meta::make_spec(meta::dsl::rename = "renamed_a");
};

struct FlattenInnerWithRename {
    meta::annotate<RenameToRenamedA>::type<int> a;
    int b;
};

struct FlattenOuterWithChildRename {
    meta::annotate<Flatten>::type<FlattenInnerWithRename> inner;
};

struct Defaulted {
    constexpr static auto spec = meta::make_spec(meta::dsl::defaulted = true);
};

struct DefaultStruct {
    meta::annotate<Defaulted>::type<int> with_default;
    std::string version;
    int plain;
};

struct RenameTarget {
    int user_name;
    std::string display_name;
};

struct RenameAllCamelTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::rename_all = naming::casing::lower_camel);
};

using RenamedRoot = meta::annotate<RenameAllCamelTag>::type<RenameTarget>;

struct DenyUnknownTag {
    constexpr static auto spec = meta::make_struct_spec(meta::dsl::deny_unknown_fields = true);
};

using StrictRoot = meta::annotate<DenyUnknownTag>::type<RenameTarget>;

struct RenameAllTarget {
    int user_name;
    float total_score;
    std::string item_id;
};

struct RenameToUpperId {
    constexpr static auto spec = meta::make_spec(meta::dsl::rename = "ID");
};

struct MixedRenameStruct {
    meta::annotate<RenameToUpperId>::type<int> user_id;
    float total_score;
    std::string item_name;
};

struct AliasUserIdOnly {
    constexpr static auto spec = meta::make_spec(meta::dsl::alias = {"user_id"});
};

struct AliasRenameAllStruct {
    meta::annotate<AliasUserIdOnly>::type<int> id;
    float total_score;
};

struct IntToStringAdapter {
    using type = std::string;
};

struct BytesAdapter {
    using type = std::vector<std::byte>;
};

struct BehaviorStruct {
    meta::annotation<std::optional<int>, meta::behavior::skip_if<meta::pred::optional_none>> maybe;
    meta::annotation<int, meta::behavior::as<std::string>> as_str;
    float plain;
};

struct WithReprStruct {
    meta::annotation<int, meta::behavior::with<IntToStringAdapter>> converted;
    float plain;
};

struct WithCompoundReprStruct {
    meta::annotation<int, meta::behavior::with<BytesAdapter>> chunk;
};

struct AsVectorStruct {
    meta::annotation<int, meta::behavior::as<std::vector<int>>> value;
};

struct AsStructStruct {
    meta::annotation<int, meta::behavior::as<SimpleStruct>> value;
};

struct AsOptionalStruct {
    meta::annotation<int, meta::behavior::as<std::optional<int>>> value;
};

struct EnumStringStruct {
    meta::annotation<Color, meta::behavior::enum_string<naming::rename_policy::identity>>
        color_field;
    int count;
};

struct EnumStringCamelStruct {
    meta::annotation<Color, meta::behavior::enum_string<naming::rename_policy::lower_camel>>
        color_field;
};

struct EnumStringUpperSnakeStruct {
    meta::annotation<Color, meta::behavior::enum_string<naming::rename_policy::upper_snake>>
        color_field;
};

struct SkipIfEmptyStringStruct {
    meta::annotation<std::string, meta::behavior::skip_if<meta::pred::empty>> s;
};

struct SkipIfEmptyVectorStruct {
    meta::annotation<std::vector<int>, meta::behavior::skip_if<meta::pred::empty>> xs;
};

struct SkipIfDefaultIntStruct {
    meta::annotation<int, meta::behavior::skip_if<meta::pred::default_value>> x;
};

struct IsNegative {
    constexpr bool operator()(const int& v) const {
        return v < 0;
    }
};

struct SkipIfCustomStruct {
    meta::annotation<int, meta::behavior::skip_if<IsNegative>> maybe_negative;
};

struct DefaultedUnlessNone {
    constexpr static auto spec =
        meta::make_spec(meta::dsl::defaulted = true,
                        meta::dsl::skip_if = meta::dsl::type<meta::pred::optional_none>);
};

struct RenameToScoreAsString {
    constexpr static auto spec =
        meta::make_spec(meta::dsl::rename = "score", meta::dsl::as = meta::dsl::type<std::string>);
};

struct MultiAttrStruct {
    meta::annotate<DefaultedUnlessNone>::type<std::optional<int>> opt_with_default;
    meta::annotate<RenameToScoreAsString>::type<int> renamed_as;
};

struct SkipIfAsStruct {
    meta::annotation<std::optional<std::string>,
                     meta::behavior::skip_if<meta::pred::optional_none>,
                     meta::behavior::as<std::string>>
        field;
};

struct SkipIfWithStruct {
    meta::annotation<std::optional<int>,
                     meta::behavior::skip_if<meta::pred::optional_none>,
                     meta::behavior::with<IntToStringAdapter>>
        field;
};

}  // namespace kota::test
