#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "fixtures/attrs.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::codec {

namespace {

namespace fx = ::kotatsu::test;

/// The spec of an extension, a module beside the codec with attributes of its own.
struct probe_extension {
    int weight = 0;
};

[[maybe_unused]] constexpr inline meta::dsl::member_proxy<&probe_extension::weight> weight{};

// The structs below are annotated as KANNOTATE (in codec's macro sheet)
// expands to.

struct Leaf {
    static consteval auto __kotatsu__annotation(meta::name_key<"leaf">) {
        return meta::make_annotation(dsl::description = "Declared in Leaf.", weight = 2);
    }

    int leaf = 0;
};

struct Holder {
    int head = 0;

    static consteval auto __kotatsu__annotation(meta::name_key<"inner">) {
        return meta::make_annotation(dsl::flatten = true);
    }

    Leaf inner;
};

struct Defaults {
    static consteval auto __kotatsu__annotation(meta::name_key<"count">) {
        return meta::make_annotation(dsl::presence = presence_kind::omit_if_default);
    }

    int count = 4;
};

constexpr std::array<std::string_view, 1> dup_alias{"dup"};
constexpr std::array<std::string_view, 1> right_alias{"right"};

constexpr field_info named(std::string_view name, std::span<const std::string_view> aliases = {}) {
    return {.name = name,
            .aliases = aliases,
            .offset = 0,
            .type = nullptr,
            .presence = presence_kind::required};
}

struct Shouting : extend<format> {
    using rename_all = naming::rename_policy::upper_snake;
};

ZEST_SUITE(codec_schema_field) {

ZEST_CASE(states_name_aliases_and_presence) {
    using id = field<fx::AliasStruct, 0>;
    ZSTATIC_EXPECT(id::index == 0U);
    ZSTATIC_EXPECT(id::name == "id");
    ZSTATIC_EXPECT(id::aliases.size() == 2U);
    ZSTATIC_EXPECT(id::aliases[1] == "userId");
    ZSTATIC_EXPECT(field<fx::DefaultStruct, 0>::presence == presence_kind::optional);
    ZSTATIC_EXPECT(field<fx::DefaultStruct, 2>::presence == presence_kind::required);
}

ZEST_CASE(counts_the_fields_a_document_holds) {
    // The skipped field is left out: the second is the float after it.
    using value = field<fx::AnnotatedStruct, 1>;
    ZSTATIC_EXPECT(value::name == "value");
    ZEXPECT(zest::type_eq<value::type, float>());
    ZSTATIC_EXPECT(field<fx::AnnotatedStruct, 0>::spec.rename == "id");
}

ZEST_CASE(names_follow_the_format) {
    ZSTATIC_EXPECT(field<fx::AnnotatedStruct, 1, Shouting>::name == "VALUE");
    // A rename stands as written.
    ZSTATIC_EXPECT(field<fx::AnnotatedStruct, 0, Shouting>::name == "id");
}

ZEST_CASE(flattened_field_keeps_its_declaring_spec) {
    using leaf = field<Holder, 1>;
    ZSTATIC_EXPECT(leaf::name == "leaf");
    ZSTATIC_EXPECT(leaf::spec.description == "Declared in Leaf.");
    ZSTATIC_EXPECT(leaf::info.description == "Declared in Leaf.");

    Holder holder;
    leaf::of(holder) = 7;
    ZEXPECT(holder.inner.leaf == 7);
}

ZEST_CASE(reads_extension_specs) {
    using leaf = field<Holder, 1>;
    ZSTATIC_EXPECT(leaf::has<probe_extension>());
    ZSTATIC_EXPECT(leaf::extension<probe_extension>().weight == 2);
    ZSTATIC_EXPECT(!field<Holder, 0>::has<probe_extension>());
}

ZEST_CASE(omitted_follows_presence) {
    using maybe = field<fx::BehaviorStruct, 0>;
    fx::BehaviorStruct value{};
    ZEXPECT(maybe::omitted(value));
    value.maybe = 1;
    ZEXPECT(!maybe::omitted(value));

    using count = field<Defaults, 0>;
    Defaults defaults;
    ZEXPECT(count::omitted(defaults));
    defaults.count = 5;
    ZEXPECT(!count::omitted(defaults));

    // A field without a predicate or omit_if_default is always written.
    ZEXPECT(!field<fx::DefaultStruct, 0>::omitted(fx::DefaultStruct{}));
}

ZEST_CASE(no_two_fields_answer_to_one_name) {
    // A document's key would go to the first field answering to it, so a
    // struct whose fields share a name, or an alias, does not build.
    constexpr std::array distinct{named("left", dup_alias), named("right")};
    ZSTATIC_EXPECT(fields_distinct(distinct));
    constexpr std::array shared{named("left", dup_alias), named("right", dup_alias)};
    ZSTATIC_EXPECT(!fields_distinct(shared));
    constexpr std::array taken{named("left", right_alias), named("right")};
    ZSTATIC_EXPECT(!fields_distinct(taken));
    constexpr std::array renamed{named("same"), named("same")};
    ZSTATIC_EXPECT(!fields_distinct(renamed));
}

};  // ZEST_SUITE(codec_schema_field)

}  // namespace

}  // namespace kotatsu::codec
