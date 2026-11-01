#pragma once

// Attribute fixtures: rename / skip / alias / flatten / defaulted /
// rename_all / deny_unknown_fields, and the behavior attrs skip_if / with /
// as / enum_string with their combinations. The annotations are written as
// KANNOTATE and KANNOTATE_TYPE (in codec's macro sheet) expand to, with
// meta's own spec API, so meta's tests can use them.

#include <optional>
#include <string>
#include <type_traits>

#include "fixtures/enums.h"
#include "kotatsu/meta/annotation.h"
#include "kotatsu/meta/attrs.h"

namespace kotatsu::test {

struct AnnotatedStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"user_id">) {
        return meta::make_annotation(meta::dsl::rename = "id");
    }

    int user_id;

    static consteval auto __kotatsu__annotation(meta::name_key<"internal">) {
        return meta::make_annotation(meta::dsl::skip = true);
    }

    std::string internal = "kept";
    float value;
};

struct AliasStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"id">) {
        return meta::make_annotation(meta::dsl::alias = {"user_id", "userId"});
    }

    int id;
    std::string name;
};

struct Inner {
    int a;
    int b;

    auto operator==(const Inner&) const -> bool = default;
};

struct Outer {
    int x;

    static consteval auto __kotatsu__annotation(meta::name_key<"inner">) {
        return meta::make_annotation(meta::dsl::flatten = true);
    }

    Inner inner;
    int y;
};

struct DeepInner {
    int p;
    int q;

    auto operator==(const DeepInner&) const -> bool = default;
};

struct Middle {
    int m;

    static consteval auto __kotatsu__annotation(meta::name_key<"deep">) {
        return meta::make_annotation(meta::dsl::flatten = true);
    }

    DeepInner deep;

    auto operator==(const Middle&) const -> bool = default;
};

struct DeepOuter {
    int head;

    static consteval auto __kotatsu__annotation(meta::name_key<"mid">) {
        return meta::make_annotation(meta::dsl::flatten = true);
    }

    Middle mid;
    int tail;
};

struct DefaultStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"with_default">) {
        return meta::make_annotation(meta::dsl::defaulted = true);
    }

    int with_default = 3;
    std::string version;
    int plain;
};

struct RenameTarget {
    int user_name;
    std::string display_name;

    auto operator==(const RenameTarget&) const -> bool = default;
};

/// RenameTarget's fields, renamed to lowerCamel wherever a RenamedRoot stands.
struct RenamedRoot;

constexpr auto __kotatsu__type_annotation(std::type_identity<RenamedRoot>) {
    return meta::make_annotation(meta::dsl::rename_all = naming::Casing::LowerCamel);
}

struct RenamedRoot {
    int user_name;
    std::string display_name;
};

/// RenameTarget's fields, denying any other wherever a StrictRoot stands.
struct StrictRoot;

constexpr auto __kotatsu__type_annotation(std::type_identity<StrictRoot>) {
    return meta::make_annotation(meta::dsl::deny_unknown_fields = true);
}

struct StrictRoot {
    int user_name;
    std::string display_name;
};

struct RenameAllTarget {
    int user_name;
    float total_score;
    std::string item_id;
};

struct MixedRenameStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"user_id">) {
        return meta::make_annotation(meta::dsl::rename = "ID");
    }

    int user_id;
    float total_score;
    std::string item_name;
};

struct AliasRenameAllStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"id">) {
        return meta::make_annotation(meta::dsl::alias = {"user_id"});
    }

    int id;
    float total_score;
};

struct IntToStringAdapter {
    using type = std::string;
};

struct BehaviorStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"maybe">) {
        return meta::make_annotation(meta::dsl::skip_if =
                                         meta::dsl::type<meta::pred::optional_none>);
    }

    std::optional<int> maybe;

    static consteval auto __kotatsu__annotation(meta::name_key<"as_str">) {
        return meta::make_annotation(meta::dsl::as = meta::dsl::type<std::string>);
    }

    int as_str;
    float plain;
};

struct WithReprStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"converted">) {
        return meta::make_annotation(meta::dsl::with = meta::dsl::type<IntToStringAdapter>);
    }

    int converted;
    float plain;
};

struct EnumStringStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"color_field">) {
        return meta::make_annotation(meta::dsl::enum_string =
                                         meta::dsl::type<naming::rename_policy::identity>);
    }

    Color color_field;
    int count;
};

struct MultiAttrStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"opt_with_default">) {
        return meta::make_annotation(meta::dsl::defaulted = true,
                                     meta::dsl::skip_if =
                                         meta::dsl::type<meta::pred::optional_none>);
    }

    std::optional<int> opt_with_default;

    static consteval auto __kotatsu__annotation(meta::name_key<"renamed_as">) {
        return meta::make_annotation(meta::dsl::rename = "score",
                                     meta::dsl::as = meta::dsl::type<std::string>);
    }

    int renamed_as;
};

struct SkipIfAsStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"field">) {
        return meta::make_annotation(meta::dsl::skip_if =
                                         meta::dsl::type<meta::pred::optional_none>,
                                     meta::dsl::as = meta::dsl::type<std::string>);
    }

    std::optional<std::string> field;
};

struct SkipIfWithStruct {
    static consteval auto __kotatsu__annotation(meta::name_key<"field">) {
        return meta::make_annotation(meta::dsl::skip_if =
                                         meta::dsl::type<meta::pred::optional_none>,
                                     meta::dsl::with = meta::dsl::type<IntToStringAdapter>);
    }

    std::optional<int> field;
};

}  // namespace kotatsu::test
