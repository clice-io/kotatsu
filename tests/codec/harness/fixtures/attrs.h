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

/// presence_all on a field reaches the members of the struct below it, not
/// the one holding it, nor the structs further down.
struct TunablesHolder {
    KANNOTATE(&tunables, presence_all = optional)
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

/// Holds for an empty text: a write leaves it out.
struct IsEmptyText {
    bool operator()(const std::string& text) const {
        return text.empty();
    }
};

struct OmitsEmptyText {
    KANNOTATE(&text, presence = type<IsEmptyText>)
    std::string text;
};

struct TextPlain {
    std::string text;
};

/// Travels as its enumerator's name, in lowerCamel, in a field.
struct AccessName {
    KANNOTATE(&value, with = type<meta::enum_names<naming::rename_policy::lower_camel>>)
    Access value;
};

struct AccessGrant {
    KANNOTATE(&level, with = type<meta::enum_names<naming::rename_policy::lower_camel>>)
    Access level;
    int count;
};

/// Mixed's enumerators in lower camel case.
struct MixedName {
    KANNOTATE(&value, with = type<meta::enum_names<naming::rename_policy::lower_camel>>)
    Mixed value;
};

struct AccessGrantPlain {
    std::string level;
    int count;
};

/// Holds for a negative value: a write leaves it out.
struct IsNegative {
    constexpr bool operator()(const int& value) const {
        return value < 0;
    }
};

/// Every way a write leaves a field out: at its default, and where a
/// predicate holds. The defaults are not the empty values, so a write that
/// compares with those, or a decoder that resets a field it did not read,
/// shows.
struct Omittable {
    int id;
    KANNOTATE(&note, presence = omit_if_default)
    std::optional<std::string> note = "kept";
    KANNOTATE(&tags, presence = omit_if_default)
    std::vector<int> tags = std::vector<int>{9};
    KANNOTATE(&generation, presence = omit_if_default)
    int generation = 7;
    KANNOTATE(&score, presence = type<IsNegative>)
    int score = 3;
};

struct OmittablePlain {
    int id;
    std::optional<std::string> note;
    std::vector<int> tags;
    int generation;
    int score;
};

/// Omittable with every omittable field left out.
struct IdOnly {
    int id;
};

/// Left out at its default, beside a list only value-initialization makes:
/// the default is the field of a value-initialized struct, which `{}` cannot
/// make.
struct CountBesideExplicit {
    ExplicitList list;
    KANNOTATE(&count, presence = omit_if_default)
    int count = 2;
};

struct ListPlain {
    std::vector<int> list;
};

}  // namespace kotatsu::test
