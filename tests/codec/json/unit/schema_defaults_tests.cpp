#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/attrs.h"
#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/containers.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/meta/schema.h"
#include "kota/codec/json/schema.h"
#include "kota/codec/macro.h"

namespace kota::meta {

namespace {

/// A non-reflectable class (raw kind unknown) whose meta::repr resolves to a
/// struct with a defaulted member: the schema describes the representation's
/// shape, but the defaults pass covers only types the decoder reads
/// directly, so a repr-routed root stays unannotated.
class json_schema_reprd_root {
public:
    int total() const {
        return total_;
    }

private:
    int total_ = 5;
};

struct json_schema_reprd_repr {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> total = 5;
};

/// A declarative repr whose to() disagrees with a fresh representation
/// (a fresh root encodes n = 9, decode value-initializes n = 4): no single
/// honest default exists behind the repr, so the schema carries none.
class json_schema_reprd_shifted {
public:
    json_schema_reprd_shifted() = default;

    explicit json_schema_reprd_shifted(int n) : n_(n) {}

    int n() const {
        return n_;
    }

private:
    int n_ = 9;
};

struct json_schema_reprd_shifted_repr {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> n = 4;
};

/// An imperative repr with a struct representation: repr_decode's imperative
/// branch hands the caller's value straight to deserialize — the declared
/// representation is never constructed — so what an absent property leaves
/// behind is the repr's business, and the schema carries no default.
class json_schema_imperative_root {
public:
    int n() const {
        return n_;
    }

    void set_n(int n) {
        n_ = n;
    }

private:
    int n_ = 9;
};

struct json_schema_imperative_repr {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> n = 4;
};

}  // namespace

template <>
struct repr<json_schema_reprd_root> {
    using type = json_schema_reprd_repr;

    static type to(const json_schema_reprd_root& v) {
        return {.total = v.total()};
    }
};

template <>
struct repr<json_schema_reprd_shifted> {
    using type = json_schema_reprd_shifted_repr;

    static type to(const json_schema_reprd_shifted& v) {
        return {.n = v.n()};
    }

    static json_schema_reprd_shifted from(const type& d) {
        return json_schema_reprd_shifted(d.n);
    }
};

template <>
struct repr<json_schema_imperative_root> {
    using type = json_schema_imperative_repr;

    template <typename Config>
    static bool serialize(auto& vis, const json_schema_imperative_root& v) {
        return codec::encode_value<Config>(vis, type{.n = v.n()});
    }

    template <typename Config>
    static bool deserialize(auto& vis, json_schema_imperative_root& v) {
        // Seed the representation from the in-place value: an absent
        // property keeps it.
        type d{.n = v.n()};
        if(!codec::decode_value<Config>(vis, d)) {
            return false;
        }
        v.set_n(d.n);
        return true;
    }
};

namespace {

struct defaults_leaf {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> threads = 4;

    KOTATSU_ANNOTATE(defaulted = true)
    <std::string> name = "worker";
};

/// A member only value-initialization makes, beside one with a default.
struct defaults_explicit_member {
    test::ExplicitList list;
    std::optional<std::int32_t> count = 3;
};

struct defaults_explicit_holder {
    defaults_explicit_member inner;
};

struct defaults_root {
    KOTATSU_ANNOTATE(defaulted = true)
    <bool> enabled = true;

    defaults_leaf pool;
    defaults_leaf mirror;

    std::optional<std::int32_t> limit;

    KOTATSU_ANNOTATE(defaulted = true)
    <std::vector<std::int32_t>> ids;
};

struct defaults_skipped {
    KOTATSU_ANNOTATE(skip_if = skip_when::empty)
    <std::vector<std::int32_t>> tags;
};

enum class defaults_level : std::uint8_t { Low = 0, High = 1 };

struct defaults_with_enum {
    KOTATSU_ANNOTATE(defaulted = true)
    <defaults_level> log_level = defaults_level::High;
};

struct defaults_shared_override {
    defaults_leaf pool;
    defaults_leaf mirror = {.threads = 9};
};

struct defaults_engaged {
    std::optional<std::int32_t> limit = 5;
    std::optional<defaults_leaf> anchor = defaults_leaf{};
};

struct defaults_node {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> depth = 1;

    std::unique_ptr<defaults_node> next;
};

struct defaults_ref_sites {
    KOTATSU_ANNOTATE(defaulted = true)
    <defaults_leaf> pool;

    KOTATSU_ANNOTATE(defaulted = true)
    <defaults_leaf> mirror = {defaults_leaf{.threads = 9}};
};

struct defaults_alt_a {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> depth = 3;

    std::string name;
};

struct defaults_alt_b {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> other = 9;
};

KOTATSU_ANNOTATION(defaults_internal_annotation, tag = "kind", tag_names = {"a", "b"});
using defaults_internal_variant =
    annotate<defaults_internal_annotation>::type<std::variant<defaults_alt_a, defaults_alt_b>>;

struct defaults_variant_holder {
    defaults_internal_variant shape;
};

KOTATSU_ANNOTATION(defaults_adjacent_annotation,
                   tag = "type",
                   content = "value",
                   tag_names = {"a", "b"});
using defaults_adjacent_variant =
    annotate<defaults_adjacent_annotation>::type<std::variant<defaults_alt_a, defaults_alt_b>>;

KOTATSU_ANNOTATION(defaults_external_annotation, tagged = true, tag_names = {"a", "b"});
using defaults_external_variant =
    annotate<defaults_external_annotation>::type<std::variant<defaults_alt_a, defaults_alt_b>>;

struct defaults_elem_a {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> alpha = 7;
};

struct defaults_elem_b {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::string> beta = "cell";
};

struct defaults_elem_c {
    KOTATSU_ANNOTATE(defaulted = true)
    <bool> gamma = true;
};

struct defaults_containers {
    std::vector<defaults_elem_a> pool = {{}};
    std::tuple<defaults_elem_b, std::int32_t> entry;
    std::map<std::string, defaults_elem_c> index = {
        {"main", {}}
    };
};

struct defaults_seq_override {
    std::vector<defaults_leaf> workers = {{.threads = 9}};
};

struct defaults_variant_override {
    defaults_internal_variant shape{
        defaults_alt_a{.depth = 8, .name = {}}
    };
};

struct defaults_cyclic {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> depth = 1;

    std::vector<defaults_cyclic> kids = {
        {.depth = 2, .kids = {}}
    };
};

struct defaults_engaged_override {
    std::optional<defaults_leaf> anchor = defaults_leaf{.threads = 9};
};

struct defaults_mid {
    defaults_leaf leaf;
};

struct defaults_cascade_root {
    defaults_mid mid = {.leaf = {.threads = 9}};
};

struct defaults_tuple_override {
    std::tuple<defaults_leaf, std::int32_t> entry = {defaults_leaf{.threads = 9}, 0};
};

struct renamed_defaults_child {
    KOTATSU_ANNOTATE(defaulted = true)
    <std::int32_t> first_value = 4;
};

struct renamed_defaults_holder {
    KOTATSU_ANNOTATE(rename_all = Casing::LowerCamel)
    <renamed_defaults_child> child;
};

KOTATSU_ANNOTATION(renamed_defaults_root_annotation, rename_all = Casing::LowerCamel);
using renamed_defaults_root =
    annotate<renamed_defaults_root_annotation>::type<renamed_defaults_child>;

namespace json = kota::codec::json;

ZEST_SUITE(codec_json_schema_defaults) {

ZEST_CASE(defaults_annotated) {
    const auto result = json::schema_string<defaults_root>().value();
    // Non-required root fields carry the value a default-constructed
    // instance encodes.
    ZEXPECT(zest::contains(result, R"("enabled":{"type":"boolean","default":true})"));
    ZEXPECT(zest::contains(result, R"({"type":"null"}],"default":null})"));
    ZEXPECT(zest::contains(result, R"("default":[]})"));
    // The shared defaults_leaf $def is annotated once, inside $defs; the ref
    // sites stay bare.
    ZEXPECT(zest::contains(result, R"("pool":{"$ref":"#/$defs/defaults_leaf"})"));
    ZEXPECT(zest::contains(result, R"("mirror":{"$ref":"#/$defs/defaults_leaf"})"));
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    ZEXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
}

ZEST_CASE(defaults_value_initialize) {
    // The fresh instance is value-initialized: `{}` would copy-list-initialize
    // the explicit list. So is a $def's.
    const auto result = json::schema_string<defaults_explicit_member>().value();
    ZEXPECT(zest::contains(result, R"({"type":"null"}],"default":3})"));
    const auto nested = json::schema_string<defaults_explicit_holder>().value();
    ZEXPECT(zest::contains(nested, R"({"type":"null"}],"default":3})"));
}

ZEST_CASE(defaulted_fields_requires_nothing) {
    // Every property may be absent and carries its default, nested ones too.
    const auto result = json::schema_string<test::Tunables, test::DefaultedConfig>().value();
    ZEXPECT(!zest::contains(result, R"("required")"));
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":9})"));
}

ZEST_CASE(defaulted_fields_on_a_field_leaves_its_holder_required) {
    const auto result = json::schema_string<test::TunablesHolder>().value();
    // The field itself is the holder's to require.
    ZEXPECT(zest::contains(result, R"("required":["tunables","count"])"));
    ZEXPECT(!zest::contains(result, R"("required":["threads")"));
}

ZEST_CASE(defaults_skip_condition) {
    // The empty vector triggers skip_if at encode time, so the default
    // document has no such property to annotate from.
    const auto result = json::schema_string<defaults_skipped>().value();
    ZEXPECT(zest::contains(result, R"("tags")"));
    ZEXPECT(!zest::contains(result, R"("default")"));
}

ZEST_CASE(defaults_shared_def_shows_fresh_values) {
    // The shared $def body always describes a fresh defaults_leaf — threads
    // 4, name "worker" — even though mirror's member initializer overrides
    // threads to 9 at its site: both sites are required, so the override has
    // no schema position and is not represented.
    const auto result = json::schema_string<defaults_shared_override>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    ZEXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
    ZEXPECT(!zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_non_required_ref_sites) {
    // Non-required refs to a shared $def each carry their whole encoded
    // object as the property default, so the per-site member initializer
    // survives and takes precedence at its site; the $def body keeps
    // describing a fresh defaults_leaf.
    const auto result = json::schema_string<defaults_ref_sites>().value();
    ZEXPECT(zest::contains(
        result,
        R"("pool":{"$ref":"#/$defs/defaults_leaf","default":{"threads":4,"name":"worker"}})"));
    ZEXPECT(zest::contains(
        result,
        R"("mirror":{"$ref":"#/$defs/defaults_leaf","default":{"threads":9,"name":"worker"}})"));
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    ZEXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
}

ZEST_CASE(defaults_engaged_override_carries_site_default) {
    // The nullable site carries the enclosing instance's whole encoded
    // object — the engaged override rides the site default, which takes
    // precedence there — while the $def body keeps describing a fresh
    // defaults_leaf.
    const auto result = json::schema_string<defaults_engaged_override>().value();
    ZEXPECT(zest::contains(result, R"("default":{"threads":9,"name":"worker"})"));
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    ZEXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
}

ZEST_CASE(defaults_in_place_override_not_represented) {
    // The override sits two required in-place levels down (root member
    // initializer → mid.leaf.threads): required sites carry no site default,
    // so the override has no schema position — the leaf $def stays exact for
    // a fresh defaults_leaf.
    const auto result = json::schema_string<defaults_cascade_root>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    ZEXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
    ZEXPECT(!zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_tuple_element_def_shows_fresh_values) {
    // Tuple elements have no per-element default position, so the required
    // entry's override is not represented; the element type's $def describes
    // a fresh defaults_leaf.
    const auto result = json::schema_string<defaults_tuple_override>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    ZEXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
    ZEXPECT(!zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_nullable_root) {
    // A nullable root unwraps to the struct body, but its default instance
    // encodes to null — a document with no properties to annotate from, not
    // a crash.
    const auto opt = json::schema_string<std::optional<defaults_leaf>>().value();
    ZEXPECT(zest::contains(opt, R"("threads")"));
    ZEXPECT(!zest::contains(opt, R"("default")"));

    const auto ptr = json::schema_string<std::unique_ptr<defaults_leaf>>().value();
    ZEXPECT(!zest::contains(ptr, R"("default")"));
}

ZEST_CASE(defaults_engaged_optional) {
    // An engaged optional lands its whole encoded value as the default on
    // the anyOf wrapper, and the struct's $def — reachable only through the
    // nullable field — still carries the fresh instance's own defaults.
    const auto result = json::schema_string<defaults_engaged>().value();
    ZEXPECT(zest::contains(result, R"("default":5)"));
    ZEXPECT(zest::contains(result, R"("default":{"threads":4,"name":"worker"})"));
    ZEXPECT(zest::contains(result, R"("name":{"type":"string","default":"worker"})"));
}

ZEST_CASE(defaults_recursive_root) {
    // A self-referential root terminates: the pointer self-reference sits
    // inside an anyOf wrapper, which is a leaf for the walk, and carries the
    // disengaged pointer's null.
    const auto result = json::schema_string<defaults_node>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":1})"));
    ZEXPECT(zest::contains(result,
                           R"("next":{"anyOf":[{"$ref":"#"},{"type":"null"}],"default":null})"));
}

ZEST_CASE(defaults_internal_tagged_variant_member) {
    // Every internal-tagged branch takes its defaults from a freshly
    // constructed alternative — what decode emplaces before reading fields —
    // so selected and unselected alternatives alike keep their own
    // initializers.
    const auto result = json::schema_string<defaults_variant_holder>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    ZEXPECT(zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_tagged_variant_root) {
    // The same applies to a tagged variant at the root, whose oneOf is
    // merged into the top-level schema object.
    const auto result = json::schema_string<defaults_internal_variant>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    ZEXPECT(zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_adjacent_tagged_variant) {
    // Adjacent tagging routes each alternative behind the content property,
    // a struct $ref: both alternatives' $defs carry their fresh defaults.
    const auto result = json::schema_string<defaults_adjacent_variant>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    ZEXPECT(zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_external_tagged_variant) {
    // External tagging nests each alternative's schema behind its name; the
    // struct $defs under both branches carry their fresh defaults.
    const auto result = json::schema_string<defaults_external_variant>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    ZEXPECT(zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_container_elements) {
    // Required containers carry no site default, but struct $defs reachable
    // only through containers — array elements, tuple slots, map values —
    // still carry the fresh defaults of their own types.
    const auto result = json::schema_string<defaults_containers>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":7})"));
    ZEXPECT(zest::contains(result, R"("beta":{"type":"string","default":"cell"})"));
    ZEXPECT(zest::contains(result, R"("gamma":{"type":"boolean","default":true})"));
    // The required container properties themselves stay bare.
    ZEXPECT(
        zest::contains(result,
                       R"("pool":{"type":"array","items":{"$ref":"#/$defs/defaults_elem_a"}})"));
}

ZEST_CASE(defaults_container_root) {
    // A container root merges its schema shape (std::array reflects as a
    // tuple: prefixItems) into the top-level object; the element $def still
    // carries its fresh defaults.
    const auto result = json::schema_string<std::array<defaults_elem_a, 2>>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":7})"));
}

ZEST_CASE(defaults_sequence_element_override_stays_local) {
    // decode value-initializes every sequence element before reading its
    // fields, so the shared $def carries defaults_leaf's own initializers —
    // the root's per-element override must not leak into it.
    const auto result = json::schema_string<defaults_seq_override>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    ZEXPECT(!zest::contains(result, R"("default":9)"));
}

ZEST_CASE(defaults_variant_holder_override_stays_local) {
    // decode emplaces a fresh alternative before reading its fields, so the
    // branch carries defaults_alt_a's own initializer — the holder's member
    // initializer must not leak into the branch default.
    const auto result = json::schema_string<defaults_variant_override>().value();
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":3})"));
    ZEXPECT(!zest::contains(result, R"("default":8)"));
}

ZEST_CASE(defaults_recursive_root_with_elements) {
    // A default instance may carry elements of the root type itself (the
    // element initializer bottoms out by overriding kids to empty); the
    // pass never follows the emitted root self-reference, so it terminates
    // and the per-element override leaks into no default.
    const auto result = json::schema_string<defaults_cyclic>().value();
    ZEXPECT(zest::contains(result, R"("$ref":"#")"));
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":1})"));
    ZEXPECT(!zest::contains(result, R"("default":2)"));
}

ZEST_CASE(defaults_repr_backed_root_unannotated) {
    // The decoder reads the representation, not a json_schema_reprd_root:
    // the defaults pass covers only types decode reads directly, so the
    // repr-routed root keeps the representation's schema shape without any
    // default.
    const auto result = json::schema_string<json_schema_reprd_root>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("total":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}})");
}

ZEST_CASE(defaults_declarative_repr_unannotated) {
    // Encode and decode disagree behind the repr (a fresh root encodes
    // n = 9, decode value-initializes n = 4): rather than guess, the
    // repr-routed root carries no default at all.
    const auto result = json::schema_string<json_schema_reprd_shifted>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("n":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}})");
}

ZEST_CASE(defaults_follow_slot_rename_all) {
    // A rename_all spec on the field slot renames the child $def's
    // properties; the fresh document is encoded under the same merged
    // config, so the renamed property still pairs with its default.
    const auto result = json::schema_string<renamed_defaults_holder>().value();
    ZEXPECT(zest::contains(result, R"("firstValue")"));
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
}

ZEST_CASE(defaults_annotated_root_rename_all) {
    // A structural annotation on the root itself shapes the fresh document
    // the same way it shapes the schema: the fresh root encodes under the
    // resolution's merged config, so the renamed property still pairs with
    // its default.
    const auto result = json::schema_string<renamed_defaults_root>().value();
    ZEXPECT(zest::contains(result, R"("firstValue")"));
    ZEXPECT(zest::contains(result, R"("maximum":2147483647,"default":4})"));
    ZEXPECT(!zest::contains(result, R"("first_value")"));
}

ZEST_CASE(defaults_imperative_repr_unannotated) {
    // repr_decode's imperative branch never constructs the declared
    // representation — deserialize reads the caller's value in place — so
    // what an absent property leaves behind is the repr's business and the
    // repr-routed root carries no default.
    const auto result = json::schema_string<json_schema_imperative_root>().value();
    ZEXPECT(result == R"({"$schema":"https://json-schema.org/draft/2020-12/schema",)"
                      R"("type":"object",)"
                      R"("properties":{)"
                      R"("n":{"type":"integer",)"
                      R"("minimum":-2147483648,)"
                      R"("maximum":2147483647}}})");
}

struct defaults_enum_config {
    [[maybe_unused]] constexpr static auto enum_repr = codec::enum_repr::String;
    using field_rename = naming::rename_policy::lower_camel;
};

ZEST_CASE(defaults_enum_and_rename_with_config) {
    // The default rides through the real encoder: the enum's String repr and
    // the field rename both shape the annotated value and its property name.
    const auto result = json::schema_string<defaults_with_enum, defaults_enum_config>().value();
    ZEXPECT(zest::contains(result, R"("logLevel":{"enum":["Low","High"],"default":"High"})"));
}

ZEST_CASE(defaults_type_erased_absent) {
    // The type-erased entry has no T to default-construct, so no defaults.
    const auto result = json::schema_string(type_info_of<defaults_root>()).value();
    ZEXPECT(!zest::contains(result, R"("default")"));
}

};  // ZEST_SUITE(codec_json_schema_defaults)

}  // namespace

}  // namespace kota::meta
