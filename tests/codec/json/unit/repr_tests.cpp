#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"
#include "kota/meta/repr.h"
#include "kota/meta/type_info.h"
#include "kota/codec/json/json.h"
#include "kota/codec/json/schema.h"
#include "kota/codec/macro.h"

namespace kota_repr_test {

// The clice RelationKind pattern: an enum that must travel as a fixed-width
// unsigned integer, overriding the built-in enum dispatch.
enum class relation : std::uint8_t {
    declares,
    defines,
    references,
};

// A value class with a textual encoded form ("major.minor").
struct version {
    int major = 0;
    int minor = 0;

    auto operator<=>(const version&) const = default;
};

// Encode-only type: decoding it must never be instantiated.
struct audit_stamp {
    std::uint64_t at = 0;
};

// Imperative form: the body drives the visitor, repr stays declared.
struct hex_id {
    std::uint32_t v = 0;

    auto operator==(const hex_id&) const -> bool = default;
};

// Dynamic form: int or string, decided at runtime.
struct poly_value {
    std::variant<std::int64_t, std::string> v;

    auto operator==(const poly_value&) const -> bool = default;
};

// Chained repr: ticket travels as step_id, whose own repr is uint32.
struct step_id {
    std::uint32_t v = 0;

    auto operator==(const step_id&) const -> bool = default;
};

struct ticket {
    step_id id;

    auto operator==(const ticket&) const -> bool = default;
};

// A non-nullable value with a nullable repr (zero travels as null).
struct lamport_stamp {
    std::uint32_t tick = 0;

    auto operator==(const lamport_stamp&) const -> bool = default;
};

// Repr whose declared encoded type is itself annotated: the annotation's
// behavior attr decides the final repr.
struct basis_points {
    int v = 0;

    auto operator==(const basis_points&) const -> bool = default;
};

// Repr whose declared encoded type is an annotated struct: the annotation's
// structural attrs (rename_all / deny_unknown_fields) shape the encoded
// document and must surface identically in the exported schema.
struct line_range {
    int first = 0;
    int last = 0;

    auto operator==(const line_range&) const -> bool = default;
};

struct line_range_repr {
    int start_line = 0;
    int line_count = 0;
};

KOTATSU_ANNOTATION(strict_camel_annotation,
                   rename_all = casing::lower_camel,
                   deny_unknown_fields = true);
using annotated_line_range_repr =
    kota::meta::annotate<strict_camel_annotation>::type<line_range_repr>;

// Two resolve chains merging the same policies — one node carrying both,
// two chained nodes carrying one each — must produce the same config and
// share one type_info instance: schema export keys $defs by instance
// identity, so a split is a hard duplicate-name error.
struct merge_point {
    int x_val = 0;
};

KOTATSU_ANNOTATION(camel_only_annotation, rename_all = casing::lower_camel);
KOTATSU_ANNOTATION(deny_only_annotation, deny_unknown_fields = true);

struct deny_step {
    int v = 0;
};

struct chained_policies {
    int v = 0;
};

struct flat_policies {
    int v = 0;
};

// Repr whose declared encoded type is an annotated tagged variant: the tagging
// must surface in type_info exactly as the codec writes it.
struct load_ok {
    int byte_count = 0;

    auto operator==(const load_ok&) const -> bool = default;
};

struct load_err {
    std::string message;

    auto operator==(const load_err&) const -> bool = default;
};

KOTATSU_ANNOTATION(load_result_annotation,
                   tag = "status",
                   content = "value",
                   tag_names = {"ok", "err"});
using load_result_repr =
    kota::meta::annotate<load_result_annotation>::type<std::variant<load_ok, load_err>>;

struct load_result {
    bool ok = true;
    int bytes = 0;
    std::string message;

    auto operator==(const load_result&) const -> bool = default;
};

}  // namespace kota_repr_test

namespace kota::meta {

template <>
struct repr<kota_repr_test::relation> {
    using type = std::uint32_t;

    static type to(kota_repr_test::relation r) {
        return static_cast<type>(r);
    }

    static kota_repr_test::relation from(type v) {
        return static_cast<kota_repr_test::relation>(v);
    }
};

template <>
struct repr<kota_repr_test::version> {
    using type = std::string;

    static type to(const kota_repr_test::version& v) {
        return std::format("{}.{}", v.major, v.minor);
    }

    static kota_repr_test::version from(const std::string& encoded) {
        kota_repr_test::version v;
        auto dot = encoded.find('.');
        if(dot == std::string::npos) {
            std::from_chars(encoded.data(), encoded.data() + encoded.size(), v.major);
            return v;
        }
        std::from_chars(encoded.data(), encoded.data() + dot, v.major);
        std::from_chars(encoded.data() + dot + 1, encoded.data() + encoded.size(), v.minor);
        return v;
    }
};

template <>
struct repr<kota_repr_test::audit_stamp> {
    using type = std::uint64_t;

    static type to(const kota_repr_test::audit_stamp& s) {
        return s.at;
    }
};

template <>
struct repr<kota_repr_test::hex_id> {
    using type = std::string;

    template <typename Config>
    static bool serialize(auto& vis, const kota_repr_test::hex_id& h) {
        return vis.visit_str(std::format("{:08x}", h.v));
    }

    template <typename Config>
    static bool deserialize(auto& vis, kota_repr_test::hex_id& h) {
        std::string s;
        if(!vis.visit_str(s))
            return false;
        h.v = static_cast<std::uint32_t>(std::stoul(s, nullptr, 16));
        return true;
    }
};

template <>
struct repr<kota_repr_test::poly_value> {
    using type = dynamic;

    template <typename Config>
    static bool serialize(auto& vis, const kota_repr_test::poly_value& p) {
        return std::visit([&](const auto& alt) { return codec::encode_value<Config>(vis, alt); },
                          p.v);
    }

    template <typename Config>
    static bool deserialize(auto& vis, kota_repr_test::poly_value& p) {
        if(vis.peek_kind() == type_kind::string) {
            std::string s;
            if(!vis.visit_str(s))
                return false;
            p.v = std::move(s);
            return true;
        }
        std::int64_t n = 0;
        if(!vis.visit_int(n))
            return false;
        p.v = n;
        return true;
    }
};

template <>
struct repr<kota_repr_test::step_id> {
    using type = std::uint32_t;

    static type to(kota_repr_test::step_id s) {
        return s.v;
    }

    static kota_repr_test::step_id from(type v) {
        return {.v = v};
    }
};

template <>
struct repr<kota_repr_test::ticket> {
    using type = kota_repr_test::step_id;

    static type to(const kota_repr_test::ticket& t) {
        return t.id;
    }

    static kota_repr_test::ticket from(type id) {
        return {.id = id};
    }
};

template <>
struct repr<kota_repr_test::lamport_stamp> {
    using type = std::optional<std::uint32_t>;

    static type to(const kota_repr_test::lamport_stamp& s) {
        return s.tick == 0 ? type{} : type{s.tick};
    }

    static kota_repr_test::lamport_stamp from(type v) {
        return {.tick = v.value_or(0)};
    }
};

template <>
struct repr<kota_repr_test::basis_points> {
    using type = annotation<int, behavior::as<double>>;

    static type to(const kota_repr_test::basis_points& b) {
        return b.v;
    }

    static kota_repr_test::basis_points from(const type& v) {
        return {.v = annotated_value(v)};
    }
};

template <>
struct repr<kota_repr_test::line_range> {
    using type = kota_repr_test::annotated_line_range_repr;

    static type to(const kota_repr_test::line_range& r) {
        return {
            {.start_line = r.first, .line_count = r.last - r.first}
        };
    }

    static kota_repr_test::line_range from(const type& w) {
        const auto& encoded = annotated_value(w);
        return {.first = encoded.start_line, .last = encoded.start_line + encoded.line_count};
    }
};

template <>
struct repr<kota_repr_test::deny_step> {
    using type = kota::meta::annotate<kota_repr_test::deny_only_annotation>::type<
        kota_repr_test::merge_point>;

    static type to(const kota_repr_test::deny_step& s) {
        return {{.x_val = s.v}};
    }

    static kota_repr_test::deny_step from(const type& w) {
        return {.v = annotated_value(w).x_val};
    }
};

template <>
struct repr<kota_repr_test::chained_policies> {
    using type = kota::meta::annotate<kota_repr_test::camel_only_annotation>::type<
        kota_repr_test::deny_step>;

    static type to(const kota_repr_test::chained_policies& c) {
        return {{.v = c.v}};
    }

    static kota_repr_test::chained_policies from(const type& w) {
        return {.v = annotated_value(w).v};
    }
};

template <>
struct repr<kota_repr_test::flat_policies> {
    using type = kota::meta::annotate<kota_repr_test::strict_camel_annotation>::type<
        kota_repr_test::merge_point>;

    static type to(const kota_repr_test::flat_policies& f) {
        return {{.x_val = f.v}};
    }

    static kota_repr_test::flat_policies from(const type& w) {
        return {.v = annotated_value(w).x_val};
    }
};

template <>
struct repr<kota_repr_test::load_result> {
    using type = kota_repr_test::load_result_repr;

    static type to(const kota_repr_test::load_result& r) {
        if(r.ok) {
            return type{kota_repr_test::load_ok{.byte_count = r.bytes}};
        }
        return type{kota_repr_test::load_err{.message = r.message}};
    }

    static kota_repr_test::load_result from(const type& w) {
        const auto& v = annotated_value(w);
        if(const auto* ok = std::get_if<kota_repr_test::load_ok>(&v)) {
            return {.ok = true, .bytes = ok->byte_count, .message = {}};
        }
        return {.ok = false, .bytes = 0, .message = std::get<kota_repr_test::load_err>(v).message};
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

using json::from_string;
using json::to_string;
using kota_repr_test::audit_stamp;
using kota_repr_test::basis_points;
using kota_repr_test::hex_id;
using kota_repr_test::lamport_stamp;
using kota_repr_test::line_range;
using kota_repr_test::load_result;
using kota_repr_test::poly_value;
using kota_repr_test::relation;
using kota_repr_test::ticket;
using kota_repr_test::version;

struct symbol {
    relation rel = relation::declares;
    version ver;

    auto operator==(const symbol&) const -> bool = default;
};

// Field annotation must beat the field type's repr.
struct version_as_int_adapter {
    using type = std::uint32_t;

    static std::uint32_t to(const version& v) {
        return static_cast<std::uint32_t>(v.major * 1000 + v.minor);
    }

    static version from(std::uint32_t encoded) {
        return {.major = static_cast<int>(encoded / 1000),
                .minor = static_cast<int>(encoded % 1000)};
    }
};

struct packed_symbol {
    meta::annotation<version, meta::behavior::with<version_as_int_adapter>> ver;
};

struct audit_log {
    audit_stamp stamp;
};

struct dynamic_holder {
    poly_value v;
};

struct maybe_version {
    std::optional<version> v;
};

struct chained_holder {
    ticket t;

    auto operator==(const chained_holder&) const -> bool = default;
};

struct stamped {
    lamport_stamp s;

    auto operator==(const stamped&) const -> bool = default;
};

struct fee_schedule {
    basis_points fee;

    auto operator==(const fee_schedule&) const -> bool = default;
};

struct range_doc {
    line_range r;

    auto operator==(const range_doc&) const -> bool = default;
};

// Outer structural policy on a repr-backed field whose repr resolves to a
// tagged encoded variant: rename/deny merge into the config and reach the
// fields of the selected alternative.
struct strict_report {
    meta::annotate<kota_repr_test::strict_camel_annotation>::type<load_result> result;
};

/// Imperative adapter: uppercases when encoding, lowercases back.
struct shout_adapter {
    using type = std::string;

    template <typename Config>
    static bool serialize(auto& vis, const std::string& s) {
        std::string encoded = s;
        for(char& c: encoded)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return vis.visit_str(encoded);
    }

    template <typename Config>
    static bool deserialize(auto& vis, std::string& s) {
        std::string encoded;
        if(!vis.visit_str(encoded))
            return false;
        for(char& c: encoded)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        s = std::move(encoded);
        return true;
    }
};

struct shouted {
    meta::annotation<std::string, meta::behavior::with<shout_adapter>> name;
};

// One annotation carrying both a variant tagging spec and a with-adapter: the
// adapter decides the repr (as in meta::resolved_repr_t), the tagging spec
// is inert.
struct choice_text_adapter {
    using type = std::string;

    static auto to(const std::variant<int, std::string>& v) -> std::string {
        if(const auto* n = std::get_if<int>(&v)) {
            return std::format("i:{}", *n);
        }
        return std::format("s:{}", std::get<std::string>(v));
    }

    static auto from(const std::string& encoded) -> std::variant<int, std::string> {
        if(encoded.starts_with("i:")) {
            int n = 0;
            std::from_chars(encoded.data() + 2, encoded.data() + encoded.size(), n);
            return n;
        }
        return encoded.starts_with("s:") ? encoded.substr(2) : encoded;
    }
};

KOTATSU_ANNOTATION(tagged_choice_annotation, tag = "t", content = "c", tag_names = {"num", "text"});
using adapted_tagged_choice =
    meta::annotate<tagged_choice_annotation>::type<std::variant<int, std::string>,
                                                   meta::behavior::with<choice_text_adapter>>;

struct string_enum_config {
    [[maybe_unused]] constexpr static auto enum_repr = codec::enum_repr::String;
};

ZEST_SUITE(codec_json_repr) {

ZEST_CASE(declarative_repr_roundtrip) {
    const symbol input{
        .rel = relation::references,
        .ver = {.major = 1, .minor = 22}
    };

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"rel":2,"ver":"1.22"})");

    symbol output{};
    auto status = from_string(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(repr_reaches_container_elements_and_map_keys) {
    std::vector<relation> rels{relation::defines, relation::declares};
    auto encoded = to_string(rels);
    ASSERT(encoded);
    EXPECT(*encoded == R"([1,0])");

    std::vector<relation> parsed_rels;
    ASSERT(from_string(*encoded, parsed_rels).has_value());
    EXPECT(parsed_rels == rels);

    // A repr declared as string makes the type usable as a JSON map key.
    std::map<version, int> by_version{
        {{.major = 1, .minor = 0}, 10},
        {{.major = 2, .minor = 5}, 25},
    };
    auto encoded_map = to_string(by_version);
    ASSERT(encoded_map);
    EXPECT(*encoded_map == R"({"1.0":10,"2.5":25})");

    std::map<version, int> parsed_map;
    ASSERT(from_string(*encoded_map, parsed_map).has_value());
    EXPECT(parsed_map == by_version);
}

ZEST_CASE(field_annotation_beats_type_repr) {
    const packed_symbol input{.ver = {{.major = 3, .minor = 14}}};

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"ver":3014})");

    packed_symbol output{};
    auto status = from_string(*encoded, output);
    ASSERT(status);
    EXPECT(meta::annotated_value(output.ver) == (version{.major = 3, .minor = 14}));
}

ZEST_CASE(one_directional_repr_encodes) {
    const audit_log input{.stamp = {.at = 1234567}};

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"stamp":1234567})");
}

ZEST_CASE(imperative_repr_roundtrip) {
    const hex_id input{.v = 0xDEADBEEF};

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"("deadbeef")");

    hex_id output{};
    auto status = from_string(*encoded, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(dynamic_repr_roundtrip) {
    dynamic_holder as_int{.v = {.v = std::int64_t{42}}};
    auto encoded = to_string(as_int);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"v":42})");

    dynamic_holder output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output.v == as_int.v);

    dynamic_holder as_str{.v = {.v = std::string("free-form")}};
    encoded = to_string(as_str);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"v":"free-form"})");

    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output.v == as_str.v);
}

ZEST_CASE(repr_alternative_in_untagged_variant) {
    // The version alternative arrives as its string repr; alternative
    // pruning must judge compatibility against that, not the raw kind.
    std::variant<version, int> input = version{.major = 1, .minor = 22};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"("1.22")");

    std::variant<version, int> output;
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output == input);

    input = 7;
    encoded = to_string(input);
    ASSERT(encoded);
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output == input);
}

ZEST_CASE(repr_inside_optional) {
    maybe_version input{
        .v = version{.major = 1, .minor = 5}
    };
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"v":"1.5"})");

    maybe_version output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output.v == input.v);

    input.v.reset();
    encoded = to_string(input);
    ASSERT(encoded);
    output.v = version{.major = 9, .minor = 9};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(!output.v);
}

ZEST_CASE(repr_beats_enum_string_config) {
    // The repr'd enum still travels as its declared integer repr.
    auto encoded = to_string<string_enum_config>(relation::references);
    ASSERT(encoded);
    EXPECT(*encoded == "2");
}

ZEST_CASE(imperative_with_adapter_roundtrip) {
    shouted input{.name = "loud"};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"name":"LOUD"})");

    shouted output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(meta::annotated_value(output.name) == std::string("loud"));
}

ZEST_CASE(chained_repr_resolves_to_final_type) {
    const chained_holder input{.t = {.id = {.v = 7}}};

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"t":7})");

    chained_holder output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output == input);

    // The schema follows the chain to the final integer shape.
    auto schema = json::schema_string<chained_holder>();
    ASSERT(schema);
    EXPECT(zest::contains(*schema, R"("t":{"type":"integer")"));
}

ZEST_CASE(equivalent_merge_chains_share_type_info) {
    // "camel then deny" across two chained repr nodes and "camel + deny" on
    // one node merge to the same effective config, so both routes must share
    // one type_info instance and describe the same document.
    const auto& chained = meta::type_info_of<kota_repr_test::chained_policies>();
    const auto& flat = meta::type_info_of<kota_repr_test::flat_policies>();
    EXPECT(&chained == &flat);

    const auto& info = static_cast<const meta::struct_type_info&>(chained);
    EXPECT(info.deny_unknown);
    EXPECT(info.fields[0].name == "xVal");
}

ZEST_CASE(annotation_nested_in_repr_resolved_type) {
    // The repr's declared encoded type carries a behavior attr; the resolver
    // must follow it to the annotation's repr, so schema consumers
    // classify the double the dispatch actually writes.
    static_assert(std::is_same_v<meta::resolved_repr_t<basis_points>, double>);

    const fee_schedule input{.fee = {.v = 250}};
    auto encoded = to_string(input);
    ASSERT(encoded);

    fee_schedule output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output == input);

    auto schema = json::schema_string<fee_schedule>();
    ASSERT(schema);
    EXPECT(zest::contains(*schema, R"("fee":{"anyOf":[{"type":"number"},{"type":"null"}]})"));
}

ZEST_CASE(annotated_repr_alternative_in_untagged_variant) {
    // The adapter, not version's own string repr, decides the repr, so
    // alternative pruning must keep the numeric alternative on number input.
    using packed_ver = meta::annotation<version, meta::behavior::with<version_as_int_adapter>>;
    std::variant<packed_ver, std::string> input = packed_ver{
        {.major = 3, .minor = 14}
    };

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == "3014");

    std::variant<packed_ver, std::string> output;
    ASSERT(from_string(*encoded, output).has_value());
    ASSERT(output.index() == 0);
    EXPECT(meta::annotated_value(std::get<0>(output)) == (version{.major = 3, .minor = 14}));
}

ZEST_CASE(structural_attrs_nested_in_repr_resolved_type) {
    // repr<line_range>'s encoded struct carries rename_all + deny_unknown_fields;
    // the codec applies them to the intermediate encoded value, and type_info
    // must describe that same document.
    const range_doc input{
        .r = {.first = 3, .last = 7}
    };

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"r":{"startLine":3,"lineCount":4}})");

    range_doc output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output == input);

    // deny_unknown_fields on the declared annotation rejects stray keys.
    EXPECT(!from_string(R"({"r":{"startLine":3,"lineCount":4,"x":1}})", output).has_value());

    // The schema exposes the renamed properties and the unknown-field policy.
    auto schema = json::schema_string<range_doc>();
    ASSERT(schema);
    EXPECT(zest::contains(*schema, R"("startLine")"));
    EXPECT(!zest::contains(*schema, R"("start_line")"));
    EXPECT(zest::contains(*schema, R"("additionalProperties":false)"));
}

ZEST_CASE(tagging_nested_in_repr_resolved_type) {
    // repr<load_result>'s encoded variant carries an adjacent tagging spec; the
    // codec writes the tagged object and type_info must carry the tagging.
    const load_result input{.ok = false, .message = "missing"};

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"status":"err","value":{"message":"missing"}})");

    load_result output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output == input);

    const auto& info = meta::type_info_of<load_result>();
    ASSERT(info.kind == meta::type_kind::variant);
    const auto& vi = static_cast<const meta::variant_type_info&>(info);
    EXPECT(vi.tagging == meta::tag_mode::adjacent);
    EXPECT(vi.tag_field == std::string_view("status"));

    auto schema = json::schema_string<load_result>();
    ASSERT(schema);
    EXPECT(zest::contains(*schema, R"("status":{"const":"err"})"));
}

ZEST_CASE(outer_policy_reaches_tagged_repr_alternatives) {
    // rename_all + deny_unknown_fields on an annotated repr-backed value
    // merge into the config before the repr's tagged encoded variant
    // re-dispatches, so they apply inside the selected alternative; type_info
    // and the schema must describe that same document.
    const strict_report input{.result = {{.ok = true, .bytes = 3, .message = {}}}};

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"result":{"status":"ok","value":{"byteCount":3}}})");

    strict_report output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(meta::annotated_value(output.result) == meta::annotated_value(input.result));

    // deny_unknown_fields applies to the fields inside the alternative.
    EXPECT(!from_string(R"({"result":{"status":"ok","value":{"byteCount":3,"x":1}}})", output)
                .has_value());

    // The alternative's type_info carries the merged policy.
    const auto& info = meta::type_info_of<decltype(strict_report::result)>();
    ASSERT(info.kind == meta::type_kind::variant);
    const auto& vi = static_cast<const meta::variant_type_info&>(info);
    EXPECT(vi.tagging == meta::tag_mode::adjacent);
    const auto& ok_alt = static_cast<const meta::struct_type_info&>(vi.alternatives[0]());
    EXPECT(ok_alt.deny_unknown);
    ASSERT(ok_alt.fields.size() == 1);
    EXPECT(ok_alt.fields[0].name == std::string_view("byteCount"));

    auto schema = json::schema_string<strict_report>();
    ASSERT(schema);
    EXPECT(zest::contains(*schema, R"("byteCount")"));
    EXPECT(!zest::contains(*schema, R"("byte_count")"));
}

ZEST_CASE(adapter_beats_variant_tagging_outside_fields) {
    // The encoded-type resolver gives the adapter precedence over the tagging
    // spec; the top-level value dispatch must agree with it (and with the
    // field-level dispatch), not emit a tagged object.
    static_assert(std::is_same_v<meta::resolved_repr_t<adapted_tagged_choice>, std::string>);

    adapted_tagged_choice input{7};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"("i:7")");

    adapted_tagged_choice output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(std::get<int>(meta::annotated_value(output)) == 7);
}

ZEST_CASE(nullable_repr_keeps_field_required) {
    // The encoded value may be null, but the property itself must be present:
    // requiredness follows the declared field type, which decode enforces.
    auto schema = json::schema_string<stamped>();
    ASSERT(schema);
    EXPECT(zest::contains(*schema, R"("required":["s"])"));

    const stamped input{};  // tick == 0 travels as null
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"s":null})");

    stamped output{.s = {.tick = 9}};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output == input);

    // An absent property is rejected, matching the schema.
    EXPECT(!from_string("{}", output).has_value());
}

ZEST_CASE(schema_follows_repr) {
    auto schema = json::schema_string<symbol>();
    ASSERT(schema);

    // relation surfaces as its uint32 repr, version as a string.
    EXPECT(zest::contains(*schema, R"("rel":{"type":"integer")"));
    EXPECT(zest::contains(*schema, R"("ver":{"type":"string"})"));
    EXPECT(!zest::contains(*schema, "enum"));

    // A dynamic repr degrades to the "any" schema.
    auto dynamic_schema = json::schema_string<dynamic_holder>();
    ASSERT(dynamic_schema);
    EXPECT(zest::contains(*dynamic_schema, R"("v":{})"));
}

};  // ZEST_SUITE(codec_json_repr)

}  // namespace

}  // namespace kota::codec

// ============================================================================
// Format-scoped repr: the JSON backend picks repr<T, json::format>.
// ============================================================================

namespace kota_repr_format_test {

// Travels as a packed integer in JSON only; the format-agnostic repr keeps
// the textual form every other backend and bare meta queries see.
struct journal {
    int page = 0;

    auto operator<=>(const journal&) const = default;
};

}  // namespace kota_repr_format_test

namespace kota::meta {

template <>
struct repr<kota_repr_format_test::journal> {
    using type = std::string;

    static type to(const kota_repr_format_test::journal& j) {
        return "p" + std::to_string(j.page);
    }

    static kota_repr_format_test::journal from(const std::string& encoded) {
        kota_repr_format_test::journal j;
        std::from_chars(encoded.data() + 1, encoded.data() + encoded.size(), j.page);
        return j;
    }
};

template <>
struct repr<kota_repr_format_test::journal, codec::json::format> {
    using type = std::int64_t;

    static type to(const kota_repr_format_test::journal& j) {
        return j.page;
    }

    static kota_repr_format_test::journal from(type v) {
        return {.page = static_cast<int>(v)};
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

using kota_repr_format_test::journal;

struct journal_holder {
    journal j;

    auto operator==(const journal_holder&) const -> bool = default;
};

// The bare (format-agnostic) view keeps the string form; only the JSON
// backend resolves the scoped int64 form.
static_assert(std::is_same_v<meta::resolved_repr_t<journal>, std::string>);
static_assert(std::is_same_v<meta::resolved_repr_t<journal, json::format>, std::int64_t>);

ZEST_SUITE(codec_json_repr_format_scoped) {

ZEST_CASE(json_backend_picks_json_scoped_repr) {
    const journal_holder input{.j = {.page = 41}};

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"j":41})");

    journal_holder output{};
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output == input);
}

ZEST_CASE(schema_follows_json_scoped_repr) {
    auto schema = json::schema_string<journal_holder>();
    ASSERT(schema);
    EXPECT(zest::contains(*schema, R"("j":{"type":"integer")"));
}

ZEST_CASE(untagged_variant_probes_with_json_scoped_repr) {
    // Kind compatibility is judged under the visitor's format: 42 matches
    // journal's json-scoped int64 repr, so the first alternative wins; under
    // the format-agnostic string repr it would fall through to the plain
    // integer alternative.
    std::variant<journal, std::int64_t> v;
    ASSERT(from_string("42", v).has_value());
    EXPECT(v.index() == 0U);
    EXPECT(std::get<journal>(v) == (journal{.page = 42}));
}

ZEST_CASE(map_keys_follow_json_scoped_repr) {
    const std::map<journal, int> input{
        {journal{.page = 7},  1},
        {journal{.page = 19}, 2},
    };

    auto encoded = to_string(input);
    ASSERT(encoded);
    // Keys travel through the json-scoped integer repr, not the generic
    // textual one.
    EXPECT(*encoded == R"({"7":1,"19":2})");

    std::map<journal, int> output;
    ASSERT(from_string(*encoded, output).has_value());
    EXPECT(output == input);
}

};  // ZEST_SUITE(codec_json_repr_format_scoped)

}  // namespace

}  // namespace kota::codec
