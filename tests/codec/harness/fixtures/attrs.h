#pragma once

// Attribute fixtures only the codec's tests use, and the plain structs that
// describe the documents the subjects in tests/fixtures/attrs.h and here
// encode to or are read from: no attributes, fields named as the document
// names them (hence names such as `userName`).

#include <charconv>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/structs.h"
#include "fixtures/attrs.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::test {

struct AnnotatedPlain {
    int id;
    float value;
};

struct AnnotatedWithInternal {
    int id;
    std::string internal;
    float value;
};

/// AliasStruct under its second alias.
struct AliasPlain {
    int userId;
    std::string name;
};

struct OuterPlain {
    int x;
    int a;
    int b;
    int y;
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

struct DefaultStructPlain {
    int with_default;
    std::string version;
    int plain;
};

struct DefaultStructAbsent {
    std::string version;
    int plain;
};

struct DefaultedFieldsTag {
    constexpr static auto spec = meta::make_struct_spec(meta::dsl::defaulted_fields = true);
};

/// No field annotated: under DefaultedFieldsTag every field may be absent,
/// the nested struct's included.
struct Settings {
    int retries = 3;
    std::string name = "default";
    RenameTarget owner;

    auto operator==(const Settings&) const -> bool = default;
};

using DefaultedSettings = meta::annotate<DefaultedFieldsTag>::type<Settings>;

/// The owner of SettingsPartialPlain: its name alone.
struct OwnerNamePlain {
    std::string display_name;
};

/// A Settings document without `name`, and its owner without `user_name`.
struct SettingsPartialPlain {
    int retries;
    OwnerNamePlain owner;
};

/// Point with a key Point does not have.
struct PointWithExtra {
    int x;
    int y;
    int extra;
};

/// Structs wherever a decode reaches one: a field, a sequence element, a map
/// value.
struct Placed {
    Point at;
    std::vector<Point> trail;
    std::map<std::string, Point> named;
};

/// Placed's document with a key nothing reads at every depth.
struct PlacedWithExtras {
    PointWithExtra at;
    std::vector<PointWithExtra> trail;
    std::map<std::string, PointWithExtra> named;
    int extra;
};

/// Point's x and a label: an untagged variant probes Point first, which
/// passes over `label` before it misses `y`.
struct Labeled {
    int x;
    std::string label;
};

struct LabeledWithExtra {
    int x;
    std::string label;
    int extra;
};

struct RenameTargetCamel {
    int userName;
    std::string displayName;
};

struct RenameTargetWithExtra {
    int user_name;
    std::string display_name;
    int extra;
};

/// On a field, a struct-level annotation's policies apply inside it.
struct UpperSnakeStrictTag {
    constexpr static auto spec =
        meta::make_struct_spec(meta::dsl::rename_all = naming::Casing::UpperSnake,
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

struct MixedRenameStructCamel {
    int ID;
    float totalScore;
    std::string itemName;
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

/// Skips its field on decode only, whatever the value.
struct SkipCellOnDecode {
    bool operator()(const GridIndex& /*cell*/, bool is_serialize) const {
        return !is_serialize;
    }
};

/// A field that travels as another type and is skipped on decode: the
/// document still holds it, written as that other type.
struct CellSkippedOnDecode {
    meta::
        annotation<GridIndex, meta::behavior::as<Point>, meta::behavior::skip_if<SkipCellOnDecode>>
            cell;
    int after = 0;
};

struct CellSkippedOnDecodePlain {
    Point cell;
    int after;
};

/// Skips an empty text. It takes only the value, so it speaks for encoding.
struct IsEmptyText {
    bool operator()(const std::string& text) const {
        return text.empty();
    }
};

struct SkipsEmptyText {
    meta::annotation<std::string, meta::behavior::skip_if<IsEmptyText>> text;
};

struct TextPlain {
    std::string text;
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

/// Skips a negative value when encoding; decoding never skips.
struct IsNegative {
    constexpr bool operator()(const int& value, bool is_serialize) const {
        return is_serialize && value < 0;
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

/// Every skip condition: the built-in ones and a predicate. The initializers
/// are not what a skip condition matches, so a decoder that resets a field it
/// did not read shows.
struct Skippable {
    int id;
    meta::annotate<SkipWhenNone>::type<std::optional<std::string>> note = "kept";
    meta::annotate<SkipWhenEmpty>::type<std::vector<int>> tags = std::vector<int>{9};
    meta::annotate<SkipWhenDefault>::type<int> generation = 7;
    meta::annotate<SkipIfNegative>::type<int> score = 3;
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

}  // namespace kota::test
