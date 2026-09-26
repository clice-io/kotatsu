#pragma once

// Attribute fixtures: rename / skip / alias / flatten / defaulted /
// rename_all / deny_unknown_fields / description, and the behavior attrs
// skip_if / with / as / enum_string with their combinations. The annotations
// are written with meta's own spec API rather than the codec's annotation
// macros, so meta's tests can use them.
//
// A subject the codec kit encodes is followed by its plain structs: no
// attributes, fields named as the document names them (hence names such as
// `userName`), describing the documents the subject encodes to or is read
// from.

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
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
    meta::annotate<Skip>::type<std::string> internal = "kept";
    float value;
};

struct AnnotatedPlain {
    int id;
    float value;
};

struct AnnotatedWithInternal {
    int id;
    std::string internal;
    float value;
};

struct AliasUserId {
    constexpr static auto spec = meta::make_spec(meta::dsl::alias = {"user_id", "userId"});
};

struct AliasStruct {
    meta::annotate<AliasUserId>::type<int> id;
    std::string name;
};

/// AliasStruct under its second alias.
struct AliasPlain {
    int userId;
    std::string name;
};

struct Inner {
    int a;
    int b;

    auto operator==(const Inner&) const -> bool = default;
};

struct Outer {
    int x;
    meta::annotate<Flatten>::type<Inner> inner;
    int y;
};

struct OuterPlain {
    int x;
    int a;
    int b;
    int y;
};

struct DeepInner {
    int p;
    int q;

    auto operator==(const DeepInner&) const -> bool = default;
};

struct Middle {
    int m;
    meta::annotate<Flatten>::type<DeepInner> deep;

    auto operator==(const Middle&) const -> bool = default;
};

struct DeepOuter {
    int head;
    meta::annotate<Flatten>::type<Middle> mid;
    int tail;
};

struct DeepOuterPlain {
    int head;
    int m;
    int p;
    int q;
    int tail;
};

struct FlattenInnerWithSkip {
    int keep_a;
    meta::annotate<Skip>::type<int> drop_b;
    int keep_c;

    auto operator==(const FlattenInnerWithSkip&) const -> bool = default;
};

struct FlattenOuterWithChildSkip {
    int head;
    meta::annotate<Flatten>::type<FlattenInnerWithSkip> inner;
};

struct FlattenOuterWithChildSkipPlain {
    int head;
    int keep_a;
    int keep_c;
};

struct RenameToRenamedA {
    constexpr static auto spec = meta::make_spec(meta::dsl::rename = "renamed_a");
};

struct FlattenInnerWithRename {
    meta::annotate<RenameToRenamedA>::type<int> a;
    int b;

    auto operator==(const FlattenInnerWithRename&) const -> bool = default;
};

struct FlattenOuterWithChildRename {
    meta::annotate<Flatten>::type<FlattenInnerWithRename> inner;
};

struct FlattenOuterWithChildRenamePlain {
    int renamed_a;
    int b;
};

struct Defaulted {
    constexpr static auto spec = meta::make_spec(meta::dsl::defaulted = true);
};

struct DefaultStruct {
    meta::annotate<Defaulted>::type<int> with_default = 3;
    std::string version;
    int plain;
};

struct DefaultStructPlain {
    int with_default;
    std::string version;
    int plain;
};

struct DefaultStructAbsent {
    std::string version;
    int plain;
};

struct RenameTarget {
    int user_name;
    std::string display_name;

    auto operator==(const RenameTarget&) const -> bool = default;
};

struct RenameAllCamelTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::rename_all = naming::casing::lower_camel);
};

using RenamedRoot = meta::annotate<RenameAllCamelTag>::type<RenameTarget>;

struct RenameTargetCamel {
    int userName;
    std::string displayName;
};

struct DenyUnknownTag {
    constexpr static auto spec = meta::make_struct_spec(meta::dsl::deny_unknown_fields = true);
};

using StrictRoot = meta::annotate<DenyUnknownTag>::type<RenameTarget>;

struct RenameTargetWithExtra {
    int user_name;
    std::string display_name;
    int extra;
};

/// On a field, a struct-level annotation's policies apply inside it.
struct UpperSnakeStrictTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::rename_all = naming::casing::upper_snake,
                               meta::dsl::deny_unknown_fields = true);
};

struct RenameTargetUpperSnake {
    int USER_NAME;
    std::string DISPLAY_NAME;
};

struct RenameTargetUpperSnakeWithExtra {
    int USER_NAME;
    std::string DISPLAY_NAME;
    int EXTRA;
};

/// rename_all merges into the config only at a reflected struct; an untagged
/// variant is not one, so its alternatives keep their names.
using CamelChoice = meta::annotate<RenameAllCamelTag>::type<std::variant<RenameTarget, int>>;

struct RenameAllTarget {
    int user_name;
    float total_score;
    std::string item_id;
};

struct RenameAllTargetCamel {
    int userName;
    float totalScore;
    std::string itemId;
};

struct NestedRenameTarget {
    int request_id;
    RenameAllTarget nested_info;
};

struct NestedRenameTargetCamel {
    int requestId;
    RenameAllTargetCamel nestedInfo;
};

struct RenameToUpperId {
    constexpr static auto spec = meta::make_spec(meta::dsl::rename = "ID");
};

struct MixedRenameStruct {
    meta::annotate<RenameToUpperId>::type<int> user_id;
    float total_score;
    std::string item_name;
};

struct MixedRenameStructCamel {
    int ID;
    float totalScore;
    std::string itemName;
};

struct AliasUserIdOnly {
    constexpr static auto spec = meta::make_spec(meta::dsl::alias = {"user_id"});
};

struct AliasRenameAllStruct {
    meta::annotate<AliasUserIdOnly>::type<int> id;
    float total_score;
};

/// Two fields answering to one name.
struct AliasDup {
    constexpr static auto spec = meta::make_spec(meta::dsl::alias = {"dup"});
};

struct SharedAlias {
    meta::annotate<AliasDup>::type<int> left;
    meta::annotate<AliasDup>::type<int> right;
};

/// Two fields that lower_camel renames to one name.
struct CamelCollision {
    int user_id;
    int userId;
};

struct DescribeId {
    constexpr static auto spec = meta::make_spec(meta::dsl::description = "Numeric identifier.");
};

struct Documented {
    meta::annotate<DescribeId>::type<int> id;
    std::string name;
};

/// A skipped field needs no codec support for its type.
struct SkipsRawPointer {
    int id;
    meta::annotate<Skip>::type<int*> raw;
};

struct IntToStringAdapter {
    using type = std::string;
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

/// Travels as its decimal text (behavior::with).
struct DecimalText {
    using type = std::string;

    static std::string to(int value) {
        return std::to_string(value);
    }

    static int from(const std::string& text) {
        int value = 0;
        std::from_chars(text.data(), text.data() + text.size(), value);
        return value;
    }
};

/// Classes that travel as another type (behavior::as): converting both ways.
struct UserId {
    std::string raw;

    UserId() = default;

    UserId(std::string text) : raw(std::move(text)) {}

    operator std::string() const {
        return raw;
    }

    auto operator==(const UserId&) const -> bool = default;
};

struct Samples {
    std::vector<int> values;

    Samples() = default;

    Samples(std::vector<int> list) : values(std::move(list)) {}

    operator std::vector<int>() const {
        return values;
    }

    auto operator==(const Samples&) const -> bool = default;
};

struct GridIndex {
    int row = 0;
    int column = 0;

    GridIndex() = default;

    GridIndex(Point point) : row(point.y), column(point.x) {}

    operator Point() const {
        return {.x = column, .y = row};
    }

    auto operator==(const GridIndex&) const -> bool = default;
};

struct AsTargets {
    meta::annotation<UserId, meta::behavior::as<std::string>> owner;
    meta::annotation<Samples, meta::behavior::as<std::vector<int>>> samples;
    meta::annotation<GridIndex, meta::behavior::as<Point>> cell;
};

struct AsTargetsPlain {
    std::string owner;
    std::vector<int> samples;
    Point cell;
};

struct EnumStringStruct {
    meta::annotation<Color, meta::behavior::enum_string<naming::rename_policy::identity>>
        color_field;
    int count;
};

using AccessName =
    meta::annotation<Access, meta::behavior::enum_string<naming::rename_policy::lower_camel>>;

struct AccessGrant {
    AccessName level;
    int count;
};

struct AccessGrantPlain {
    std::string level;
    int count;
};

struct IsNegative {
    constexpr bool operator()(const int& v) const {
        return v < 0;
    }
};

struct SkipWhenNone {
    constexpr static auto spec = meta::make_spec(meta::dsl::skip_if = meta::skip_when::none);
};

struct SkipWhenEmpty {
    constexpr static auto spec = meta::make_spec(meta::dsl::skip_if = meta::skip_when::empty);
};

struct SkipWhenDefault {
    constexpr static auto spec =
        meta::make_spec(meta::dsl::skip_if = meta::skip_when::default_value);
};

struct SkipIfNegative {
    constexpr static auto spec = meta::make_spec(meta::dsl::skip_if = meta::dsl::type<IsNegative>);
};

/// Every skip condition: the built-in ones and a predicate.
struct Skippable {
    int id;
    meta::annotate<SkipWhenNone>::type<std::optional<std::string>> note;
    meta::annotate<SkipWhenEmpty>::type<std::vector<int>> tags;
    meta::annotate<SkipWhenDefault>::type<int> generation;
    meta::annotate<SkipIfNegative>::type<int> score;
};

struct SkippablePlain {
    int id;
    std::string note;
    std::vector<int> tags;
    int generation;
    int score;
};

/// Skippable with every skippable field left out.
struct IdOnly {
    int id;
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
