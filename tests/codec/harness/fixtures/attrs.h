#pragma once

// Attribute fixtures only the codec's tests use, and the plain structs that
// describe the documents the subjects in tests/fixtures/attrs.h and here
// encode to or are read from: no attributes, fields named as the document
// names them (hence names such as `userName`).

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/containers.h"
#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/structs.h"
#include "fixtures/attrs.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/attrs.h"
#include "kotatsu/codec/macro.h"

namespace kotatsu::test {

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
    KANNOTATE(&drop_b, skip = true)
    int drop_b;
    int keep_c;

    auto operator==(const FlattenInnerWithSkip&) const -> bool = default;
};

struct FlattenOuterWithChildSkip {
    int head;
    KANNOTATE(&inner, flatten = true)
    FlattenInnerWithSkip inner;
};

struct FlattenOuterWithChildSkipPlain {
    int head;
    int keep_a;
    int keep_c;
};

struct FlattenInnerWithRename {
    KANNOTATE(&a, rename = "renamed_a")
    int a;
    int b;

    auto operator==(const FlattenInnerWithRename&) const -> bool = default;
};

struct FlattenOuterWithChildRename {
    KANNOTATE(&inner, flatten = true)
    FlattenInnerWithRename inner;
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
struct SnakeStrict {
    KANNOTATE(&value, rename_all = Casing::UpperSnake, deny_unknown_fields = true)
    RenameTarget value;
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
KANNOTATE_TYPE(struct CamelChoice, rename_all = Casing::LowerCamel);

struct CamelChoice : std::variant<RenameTarget, int> {
    using variant::variant;
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

struct MixedRenameStructCamel {
    int ID;
    float totalScore;
    std::string itemName;
};

/// Point with a key Point has no field for.
struct PointWithExtra {
    std::int32_t x;
    std::int32_t y;
    bool extra;
};

/// Unknown keys at each depth of a keyed document: a struct, a struct inside
/// it, a sequence element and a map value.
struct Layout {
    int id;
    Point origin;
    std::vector<Point> points;
    std::map<std::string, Point> named;
};

struct LayoutWithExtrasPlain {
    int id;
    PointWithExtra origin;
    std::vector<PointWithExtra> points;
    std::map<std::string, PointWithExtra> named;
    bool stray;
};

/// An unknown key, then a field the document gives a text where Point has a
/// number.
struct ExtraBeforeTextPlain {
    bool extra;
    std::string x;
    std::int32_t y;
};

/// A field the document may name by an alias.
struct AliasedOrigin {
    KANNOTATE(&origin, alias = {"anchor"})
    Point origin;
};

template <typename T>
struct AnchorPlain {
    T anchor;
};

/// An alternative probed before Point that the same keys and a third make.
struct Measured {
    std::int32_t x;
    std::int32_t y;
    std::int32_t length;

    auto operator==(const Measured&) const -> bool = default;
};

/// Fields with initializers, one of them a struct with its own.
struct Limits {
    int low = 1;
    int high = 9;

    auto operator==(const Limits&) const -> bool = default;
};

struct Tunables {
    int threads = 4;
    std::string name = "worker";
    Limits limits;

    auto operator==(const Tunables&) const -> bool = default;
};

/// defaulted_fields on a field reaches the struct below it, not the one
/// holding it.
struct TunablesHolder {
    KANNOTATE(&tunables, defaulted_fields = true)
    Tunables tunables;
    int count;
};

/// A field whose default a schema leaves unstated.
struct WithUnstated {
    int id;
    KANNOTATE(&seed, schema_default = false)
    int seed;
};

/// Two fields answering to one name.
KANNOTATION(AliasDup, alias = {"dup"});

struct SharedAlias {
    KANNOTATE(&left, AliasDup)
    int left;
    KANNOTATE(&right, AliasDup)
    int right;
};

/// Two fields that lower_camel renames to one name.
struct CamelCollision {
    int user_id;
    int userId;
};

struct Documented {
    KANNOTATE(&id, description = "Numeric identifier.")
    int id;
    std::string name;
};

/// A skipped field needs no codec support for its type.
struct SkipsRawPointer {
    int id;
    KANNOTATE(&raw, skip = true)
    int* raw;
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

/// Travels as decimal text, in a field.
struct Decimal {
    KANNOTATE(&value, with = type<DecimalText>)
    int value;
};

/// Renamed, and travelling as decimal text, by one annotation.
struct RenamedDecimal {
    KANNOTATE(&value, rename = "amount", with = type<DecimalText>)
    int value;
};

struct RenamedDecimalPlain {
    std::string amount;
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

/// Travels as HoldsExplicit (behavior::as), a target only value-initialization
/// makes.
struct Tally {
    std::vector<int> marks;

    Tally() = default;

    Tally(std::vector<int> list) : marks(std::move(list)) {}

    Tally(const HoldsExplicit& held) : marks(held.list.begin(), held.list.end()) {}

    operator HoldsExplicit() const {
        return {.list = ExplicitList(marks.begin(), marks.end()),
                .count = static_cast<int>(marks.size())};
    }

    auto operator==(const Tally&) const -> bool = default;
};

struct TallyAsHeld {
    KANNOTATE(&value, as = type<HoldsExplicit>)
    Tally value;
};

struct AsTargets {
    KANNOTATE(&owner, as = type<std::string>)
    UserId owner;
    KANNOTATE(&samples, as = type<std::vector<int>>)
    Samples samples;
    KANNOTATE(&cell, as = type<Point>)
    GridIndex cell;
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
    KANNOTATE(&cell, as = type<Point>, skip_if = type<SkipCellOnDecode>)
    GridIndex cell;
    int after = 0;
};

struct CellSkippedOnDecodePlain {
    Point cell;
    int after;
};

/// Skips its field on decode only, whatever the value.
struct SkipHeldOnDecode {
    bool operator()(const HoldsExplicit& /*held*/, bool is_serialize) const {
        return !is_serialize;
    }
};

/// A field skipped on decode whose type only value-initialization makes: a
/// positional decode reads past it into a value of its own.
struct HeldSkippedOnDecode {
    KANNOTATE(&held, skip_if = type<SkipHeldOnDecode>)
    HoldsExplicit held;
    int after = 0;
};

struct HeldSkippedOnDecodePlain {
    HoldsExplicit held;
    int after;
};

/// Skips an empty text. It takes only the value, so it speaks for encoding.
struct IsEmptyText {
    bool operator()(const std::string& text) const {
        return text.empty();
    }
};

struct SkipsEmptyText {
    KANNOTATE(&text, skip_if = type<IsEmptyText>)
    std::string text;
};

struct TextPlain {
    std::string text;
};

/// Travels as its enumerator's name, in lowerCamel, in a field.
struct AccessName {
    KANNOTATE(&value, enum_string = type<naming::rename_policy::lower_camel>)
    Access value;
};

struct AccessGrant {
    KANNOTATE(&level, enum_string = type<naming::rename_policy::lower_camel>)
    Access level;
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

/// Every skip condition: the built-in ones and a predicate. The initializers
/// are not what a skip condition matches, so a decoder that resets a field it
/// did not read shows.
struct Skippable {
    int id;
    KANNOTATE(&note, skip_if = skip_when::none)
    std::optional<std::string> note = "kept";
    KANNOTATE(&tags, skip_if = skip_when::empty)
    std::vector<int> tags = std::vector<int>{9};
    KANNOTATE(&generation, skip_if = skip_when::default_value)
    int generation = 7;
    KANNOTATE(&score, skip_if = type<IsNegative>)
    int score = 3;
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

}  // namespace kotatsu::test
