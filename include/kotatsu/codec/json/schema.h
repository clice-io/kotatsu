#pragma once

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "kotatsu/support/expected_try.h"
#include "kotatsu/support/naming.h"
#include "kotatsu/support/type_list.h"
#include "kotatsu/codec/dyn/decode.h"
#include "kotatsu/codec/dyn/document.h"
#include "kotatsu/codec/format.h"
#include "kotatsu/codec/json/json.h"
#include "kotatsu/codec/type_info.h"

namespace kotatsu::codec::json {

/// How a schema is written.
struct schema_options {
    /// Deny the keys no field answers to in every object, as a decode under
    /// decode_options::deny_unknown_fields does.
    bool deny_unknown_fields = false;
    /// Indented, rather than compact, text.
    bool pretty = false;
    /// Whether the documents carry variant tags: a format that is not human
    /// readable encodes a tagged variant as its bare alternative (the
    /// is_human_readable gate in encode_tagged_variant). The typed entry
    /// points take it from the format.
    bool human_readable = true;
};

namespace detail {

/// An internal-tagged struct branch is the one schema site whose struct body
/// is inlined rather than $def'd (the tag property must merge into the
/// struct's own properties). When the typed entry point will run the
/// default-annotation pass, the emitter stamps each such branch with the
/// alternative's normalized type name under this key so the pass can pair
/// the branch with the fresh document of its type; the pass strips every
/// marker it meets, so no returned schema ever carries one.
constexpr inline std::string_view alternative_marker = "x-kotatsu-alternative";

class SchemaEmitter {
    using tk = meta::type_kind;
    using result_t = std::expected<dyn::Value, rich_error>;

public:
    explicit SchemaEmitter(const schema_options& opts, bool mark_alternatives = false) :
        opts(opts), mark_alternatives(mark_alternatives) {}

    result_t emit(const codec::type_info& root) {
        root_ti = unwrap(&root);
        if(root_ti->kind == tk::structure) {
            auto name = kotatsu::naming::normalize_identifier(root_ti->type_name);
            used_names.insert(name);
            def_names.emplace(root_ti, std::move(name));
        }

        dyn::Object schema;
        schema.insert("$schema", "https://json-schema.org/draft/2020-12/schema");

        KOTATSU_EXPECTED_TRY(merge_schema_fields(schema, &root));

        if(!defs.empty()) {
            dyn::Object defs_obj;
            for(auto& [name, value]: defs) {
                defs_obj.insert(std::move(name), std::move(value));
            }
            schema.insert("$defs", std::move(defs_obj));
        }

        return dyn::Value(std::move(schema));
    }

private:
    const static codec::type_info* unwrap(const codec::type_info* ti) {
        while(ti->kind == tk::optional || ti->kind == tk::pointer) {
            ti = &static_cast<const codec::optional_type_info*>(ti)->inner();
        }
        return ti;
    }

    static std::string alternative_name(const codec::variant_type_info* vi, std::size_t i) {
        if(i < vi->alt_names.size()) {
            return std::string(vi->alt_names[i]);
        }
        return kotatsu::naming::normalize_identifier(vi->alternatives[i]().type_name);
    }

    std::expected<std::string_view, rich_error> def_name(const codec::type_info* ti) {
        auto it = def_names.find(ti);
        if(it != def_names.end()) {
            return std::string_view(it->second);
        }
        auto name = kotatsu::naming::normalize_identifier(ti->type_name);
        if(!used_names.insert(name).second) {
            return std::unexpected(rich_error(
                std::format("duplicate $defs name '{}' from type '{}'", name, ti->type_name)));
        }
        auto [pos, _] = def_names.emplace(ti, std::move(name));
        return std::string_view(pos->second);
    }

    result_t make_nullable(const codec::type_info* ti) {
        auto* inner = unwrap(ti);
        KOTATSU_EXPECTED_TRY_V(auto schema, make_schema(inner));
        // anyOf, not oneOf: the inner schema may itself admit null (a
        // monostate alternative, an unconstrained any), and a null document
        // must not fail by matching both branches.
        return dyn::Value{
            {"anyOf", dyn::Array{std::move(schema), dyn::Value{{"type", "null"}}}},
        };
    }

    result_t make_schema(const codec::type_info* ti) {
        switch(ti->kind) {
            case tk::optional:
            case tk::pointer: return make_nullable(ti);
            case tk::null:
                return dyn::Value{
                    {"type", "null"}
                };
            case tk::boolean:
                return dyn::Value{
                    {"type", "boolean"}
                };
            case tk::character:
                // One code point up to U+00FF, the char's value.
                return dyn::Value{
                    {"type",    "string"              },
                    {"pattern", R"(^[\u0000-\u00FF]$)"},
                };
            case tk::string:
                return dyn::Value{
                    {"type", "string"}
                };
            case tk::bytes: {
                // visit_bytes emits an array of octets, not a string.
                KOTATSU_EXPECTED_TRY_V(auto items, make_integer(0, 255));
                return dyn::Value{
                    {"type",  "array"         },
                    {"items", std::move(items)},
                };
            }
            case tk::float32:
            case tk::float64: return make_float();
            case tk::int8:
            case tk::int16:
            case tk::int32:
            case tk::int64:
            case tk::uint8:
            case tk::uint16:
            case tk::uint32:
            case tk::uint64: return make_integer_kind(ti->kind);
            case tk::enumeration: return make_enum(ti);
            case tk::array:
            case tk::set: return make_array(ti);
            case tk::map: return make_map(ti);
            case tk::tuple: return make_tuple(ti);
            case tk::structure: return make_struct_ref(ti);
            case tk::variant: return make_variant(ti);
            case tk::any: return dyn::Value(dyn::Object{});
            default:
                return std::unexpected(
                    rich_error(std::format("unsupported type kind '{}' for JSON Schema generation",
                                           ti->type_name)));
        }
    }

    std::expected<void, rich_error> merge_schema_fields(dyn::Object& target,
                                                        const codec::type_info* ti) {
        ti = unwrap(ti);
        if(ti->kind == tk::structure) {
            return add_struct_body(target, static_cast<const codec::struct_type_info*>(ti));
        }
        KOTATSU_EXPECTED_TRY_V(auto schema, make_schema(ti));
        if(auto* obj = schema.get_object()) {
            for(auto& [k, v]: *obj) {
                target.insert(std::string(k), std::move(v));
            }
        }
        return {};
    }

    /// The property an internally tagged alternative's object carries next to
    /// its struct's own fields.
    struct InternalTag {
        std::string_view field;
        std::string_view alt_name;
    };

    /// A struct's object schema, written into target; with a tag, the object
    /// also carries the tag property, required after the struct's own
    /// required fields.
    std::expected<void, rich_error> add_struct_body(dyn::Object& target,
                                                    const codec::struct_type_info* si,
                                                    const InternalTag* tag = nullptr) {
        target.insert("type", "object");
        if(!si->description.empty()) {
            target.insert("description", si->description);
        }
        KOTATSU_EXPECTED_TRY_V(auto props, make_properties(si));
        dyn::Array required;
        // A required field the document may name by an alias is present under
        // one of its names.
        dyn::Array named_once;
        for(const auto& f: si->fields) {
            if(!is_required(f)) {
                continue;
            }
            if(f.aliases.empty()) {
                required.push_back(dyn::Value(f.name));
                continue;
            }
            dyn::Array names{dyn::Value{{"required", dyn::Array{dyn::Value(f.name)}}}};
            for(auto alias: f.aliases) {
                names.push_back(dyn::Value{
                    {"required", dyn::Array{dyn::Value(alias)}}
                });
            }
            named_once.push_back(dyn::Value{
                {"anyOf", std::move(names)}
            });
        }
        if(tag) {
            props.get_object()->insert(std::string(tag->field),
                                       dyn::Value{
                                           {"const", tag->alt_name}
            });
            required.push_back(dyn::Value(tag->field));
        }
        target.insert("properties", std::move(props));
        if(!required.empty()) {
            target.insert("required", std::move(required));
        }
        if(!named_once.empty()) {
            target.insert("allOf", std::move(named_once));
        }
        if(si->deny_unknown || opts.deny_unknown_fields) {
            target.insert("additionalProperties", false);
        }
        return {};
    }

    static result_t make_integer(std::int64_t min_val, std::int64_t max_val) {
        return dyn::Value{
            {"type",    "integer"},
            {"minimum", min_val  },
            {"maximum", max_val  },
        };
    }

    static result_t make_unsigned(std::uint64_t max_val) {
        return dyn::Value{
            {"type",    "integer"       },
            {"minimum", std::uint64_t{0}},
            {"maximum", max_val         },
        };
    }

    static result_t make_integer_kind(meta::type_kind kind) {
        switch(kind) {
            case tk::int8:
                return make_integer(std::numeric_limits<std::int8_t>::min(),
                                    std::numeric_limits<std::int8_t>::max());
            case tk::int16:
                return make_integer(std::numeric_limits<std::int16_t>::min(),
                                    std::numeric_limits<std::int16_t>::max());
            case tk::int32:
                return make_integer(std::numeric_limits<std::int32_t>::min(),
                                    std::numeric_limits<std::int32_t>::max());
            case tk::int64:
                return make_integer(std::numeric_limits<std::int64_t>::min(),
                                    std::numeric_limits<std::int64_t>::max());
            case tk::uint8: return make_unsigned(std::numeric_limits<std::uint8_t>::max());
            case tk::uint16: return make_unsigned(std::numeric_limits<std::uint16_t>::max());
            case tk::uint32: return make_unsigned(std::numeric_limits<std::uint32_t>::max());
            case tk::uint64: return make_unsigned(std::numeric_limits<std::uint64_t>::max());
            default:
                // char/bool/extended-char underlying: still a number in the
                // document, but without portable bounds here.
                return dyn::Value{
                    {"type", "integer"}
                };
        }
    }

    /// A number: the encoder fails on a float that is not finite, which
    /// JSON has no number for.
    result_t make_float() const {
        return dyn::Value{
            {"type", "number"}
        };
    }

    result_t make_enum(const codec::type_info* ti) const {
        auto* ei = static_cast<const codec::enum_type_info*>(ti);
        if(!ei->named) {
            // An integer: the codec casts through the underlying type
            // without checking membership, and name reflection only covers a
            // limited scan range — values outside it still encode. The honest
            // constraint is the underlying integer's range, not a value list.
            return make_integer_kind(ei->underlying_kind);
        }
        // Exhaustive: a value without a reflected member name has no string
        // spelling, so the encoder rejects it instead of emitting one.
        dyn::Array values;
        for(const auto& name: ei->member_names) {
            values.push_back(dyn::Value(name));
        }
        return dyn::Value{
            {"enum", std::move(values)}
        };
    }

    result_t make_array(const codec::type_info* ti) {
        auto* ai = static_cast<const codec::array_type_info*>(ti);
        KOTATSU_EXPECTED_TRY_V(auto items, make_schema(&ai->element()));
        // No uniqueItems for a set: the decoder takes duplicates and keeps
        // one, and a multiset encodes them.
        return dyn::Value{
            {"type",  "array"         },
            {"items", std::move(items)},
        };
    }

    result_t make_map(const codec::type_info* ti) {
        auto* mi = static_cast<const codec::map_type_info*>(ti);
        KOTATSU_EXPECTED_TRY_V(auto val_schema, make_schema(&mi->value()));
        dyn::Object obj{
            {"type",                 "object"             },
            {"additionalProperties", std::move(val_schema)},
        };
        if(auto names = key_names(&mi->key())) {
            obj.insert("propertyNames", std::move(*names));
        }
        return dyn::Value(std::move(obj));
    }

    /// What a map's keys must spell, beyond a string, as its keys decode:
    /// the decimal digits of an integer, the name of an enumerator. The
    /// range of an integer key is not stated.
    std::optional<dyn::Value> key_names(const codec::type_info* ti) const {
        auto kind = ti->kind;
        if(kind == tk::enumeration) {
            auto* ei = static_cast<const codec::enum_type_info*>(ti);
            if(ei->named) {
                // An enum's schema never fails.
                return *make_enum(ti);
            }
            kind = ei->underlying_kind;
        }
        switch(kind) {
            case tk::int8:
            case tk::int16:
            case tk::int32:
            case tk::int64:
                return dyn::Value{
                    {"pattern", "^-?[0-9]+$"}
                };
            case tk::uint8:
            case tk::uint16:
            case tk::uint32:
            case tk::uint64:
                return dyn::Value{
                    {"pattern", "^[0-9]+$"}
                };
            default: return std::nullopt;
        }
    }

    result_t make_tuple(const codec::type_info* ti) {
        auto* tup = static_cast<const codec::tuple_type_info*>(ti);
        dyn::Array items;
        for(std::size_t i = 0; i < tup->elements.size(); ++i) {
            KOTATSU_EXPECTED_TRY_V(auto elem, make_schema(&tup->elements[i]()));
            items.push_back(std::move(elem));
        }
        auto size = static_cast<std::uint64_t>(tup->elements.size());
        return dyn::Value{
            {"type",        "array"         },
            {"prefixItems", std::move(items)},
            {"items",       false           },
            {"minItems",    size            },
            {"maxItems",    size            },
        };
    }

    result_t make_struct_ref(const codec::type_info* ti) {
        if(ti == root_ti) {
            return dyn::Value{
                {"$ref", "#"}
            };
        }
        KOTATSU_EXPECTED_TRY_V(auto name, def_name(ti));
        KOTATSU_EXPECTED_TRY(ensure_struct_def(ti));
        return dyn::Value{
            {"$ref", std::format("#/$defs/{}", name)}
        };
    }

    std::expected<void, rich_error> ensure_struct_def(const codec::type_info* ti) {
        if(!emitted.insert(ti).second) {
            return {};
        }
        auto* si = static_cast<const codec::struct_type_info*>(ti);
        KOTATSU_EXPECTED_TRY_V(auto name, def_name(ti));
        dyn::Object body;
        KOTATSU_EXPECTED_TRY(add_struct_body(body, si));
        defs.emplace_back(std::string(name), std::move(body));
        return {};
    }

    result_t make_properties(const codec::struct_type_info* si) {
        dyn::Object props;
        // A key the decoder gives the first field answering to it: a second
        // field's property under the same name would claim it too.
        auto claim = [&](std::string_view name,
                         const codec::field_info& f) -> std::expected<void, rich_error> {
            if(props.find(name) != nullptr) {
                return std::unexpected(
                    rich_error(std::format("field '{}' answers to '{}', as another field does",
                                           f.name,
                                           name)));
            }
            return {};
        };
        for(const auto& f: si->fields) {
            // A required field is in every document, so a default holding
            // it cannot leave it out; it has no default of its own to hide.
            if(!f.schema_default && is_required(f)) {
                return std::unexpected(rich_error(
                    std::format("schema_default = false on field '{}', which is required",
                                f.name)));
            }
            KOTATSU_EXPECTED_TRY_V(auto schema, make_schema(&f.type()));
            KOTATSU_EXPECTED_TRY(constrain(schema, f));
            // An alias reads the same value; the field describes it.
            auto aliased = f.aliases.empty() ? dyn::Value() : schema;
            if(!f.description.empty()) {
                schema.get_object()->insert("description", f.description);
            }
            KOTATSU_EXPECTED_TRY(claim(f.name, f));
            props.insert(std::string(f.name), std::move(schema));
            for(auto alias: f.aliases) {
                KOTATSU_EXPECTED_TRY(claim(alias, f));
                props.insert(std::string(alias), aliased);
            }
        }
        return dyn::Value(std::move(props));
    }

    /// The branches of a schema whose `type` is one of types: the schema
    /// itself, or, through the anyOf a nullable or an untagged variant
    /// makes, nested as deep as they nest, its branches.
    static void typed_branches(dyn::Value& schema,
                               std::initializer_list<std::string_view> types,
                               std::vector<dyn::Object*>& branches) {
        auto* object = schema.get_object();
        if(object == nullptr) {
            return;
        }
        const auto* type = object->find("type");
        auto name = type != nullptr ? type->get_string() : std::nullopt;
        if(name && std::ranges::find(types, *name) != types.end()) {
            branches.push_back(object);
        } else if(auto* any = object->find("anyOf")) {
            for(auto& branch: *any->get_array()) {
                typed_branches(branch, types, branches);
            }
        }
    }

    static dyn::Value to_dyn(const codec::schema_number& number) {
        if(const auto* i = std::get_if<std::int64_t>(&number)) {
            return *i;
        }
        if(const auto* u = std::get_if<std::uint64_t>(&number)) {
            return *u;
        }
        return std::get<double>(number);
    }

    static codec::schema_number number_of(const dyn::Value& value) {
        if(auto i = value.get_int()) {
            return *i;
        }
        if(auto u = value.get_uint()) {
            return *u;
        }
        return *value.get_double();
    }

    /// Writes into a number branch a bound the annotation of field f states,
    /// where it is tighter than the one the type gives: tighter is greater
    /// for a minimum, less for a maximum.
    static std::expected<void, rich_error> bound(dyn::Object& branch,
                                                 const codec::field_info& f,
                                                 std::string_view key,
                                                 const codec::schema_number& number,
                                                 std::partial_ordering tighter) {
        if(std::holds_alternative<std::monostate>(number)) {
            return {};
        }
        if(branch.find("type")->get_string() == "integer" &&
           std::holds_alternative<double>(number)) {
            return std::unexpected(
                rich_error(std::format("floating-point {} on integer field '{}'", key, f.name)));
        }
        if(auto* current = branch.find(key)) {
            if(codec::compare_numbers(number, number_of(*current)) == tighter) {
                *current = to_dyn(number);
            }
        } else {
            branch.insert(std::string(key), to_dyn(number));
        }
        return {};
    }

    /// What the field's annotation states beyond its type: the bounds of a
    /// number, the values of a string. The decoder checks neither.
    static std::expected<void, rich_error> constrain(dyn::Value& schema,
                                                     const codec::field_info& f) {
        bool bounded = !std::holds_alternative<std::monostate>(f.minimum) ||
                       !std::holds_alternative<std::monostate>(f.maximum);
        if(bounded) {
            std::vector<dyn::Object*> numbers;
            typed_branches(schema, {"integer", "number"}, numbers);
            if(numbers.empty()) {
                return std::unexpected(rich_error(
                    std::format("minimum or maximum on field '{}', which is not a number",
                                f.name)));
            }
            for(auto* branch: numbers) {
                KOTATSU_EXPECTED_TRY(
                    bound(*branch, f, "minimum", f.minimum, std::partial_ordering::greater));
                KOTATSU_EXPECTED_TRY(
                    bound(*branch, f, "maximum", f.maximum, std::partial_ordering::less));
            }
        }
        if(!f.choices.empty()) {
            std::vector<dyn::Object*> strings;
            typed_branches(schema, {"string"}, strings);
            if(strings.empty()) {
                return std::unexpected(rich_error(
                    std::format("choices on field '{}', which is not a string", f.name)));
            }
            dyn::Array values;
            for(auto choice: f.choices) {
                values.push_back(dyn::Value(choice));
            }
            for(auto* branch: strings) {
                branch->insert("enum", values);
            }
        }
        return {};
    }

    /// A field is required when its presence is: the decoder rejects a document
    /// without it, and the encoder always writes it.
    static bool is_required(const codec::field_info& f) {
        return f.presence == codec::presence_kind::required;
    }

    static dyn::Value make_tag_const(std::string_view tag_field, std::string_view alt_name) {
        return {
            {"properties", {{std::string(tag_field), {{"const", alt_name}}}}},
            {"required",   dyn::Array{dyn::Value(tag_field)}                },
        };
    }

    result_t make_internal_tagged(const codec::type_info* ti,
                                  std::string_view tag_field,
                                  std::string_view alt_name) {
        ti = unwrap(ti);
        if(ti->kind != tk::structure) {
            KOTATSU_EXPECTED_TRY_V(auto schema, make_schema(ti));
            return dyn::Value{
                {"allOf",
                 dyn::Array{
                     std::move(schema),
                     make_tag_const(tag_field, alt_name),
                 }},
            };
        }
        dyn::Object obj;
        InternalTag tag{.field = tag_field, .alt_name = alt_name};
        KOTATSU_EXPECTED_TRY(
            add_struct_body(obj, static_cast<const codec::struct_type_info*>(ti), &tag));
        // The body is inlined here rather than $def'd, so it names its type
        // for the default-annotation sweep, which removes the marker.
        if(mark_alternatives) {
            obj.insert(std::string(alternative_marker),
                       kotatsu::naming::normalize_identifier(ti->type_name));
        }
        return dyn::Value(std::move(obj));
    }

    result_t make_variant(const codec::type_info* ti) {
        auto* vi = static_cast<const codec::variant_type_info*>(ti);
        auto tagging = opts.human_readable ? vi->tagging : codec::tag_mode::none;
        dyn::Array alts;

        for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
            switch(tagging) {
                case codec::tag_mode::none: {
                    KOTATSU_EXPECTED_TRY_V(auto schema, make_schema(&vi->alternatives[i]()));
                    alts.push_back(std::move(schema));
                    break;
                }

                case codec::tag_mode::external: {
                    auto alt_name = alternative_name(vi, i);
                    KOTATSU_EXPECTED_TRY_V(auto schema, make_schema(&vi->alternatives[i]()));
                    alts.push_back({
                        {"type",                 "object"                        },
                        {"properties",           {{alt_name, std::move(schema)}} },
                        {"required",             dyn::Array{dyn::Value(alt_name)}},
                        {"additionalProperties", false                           },
                    });
                    break;
                }

                case codec::tag_mode::internal: {
                    auto alt_name = alternative_name(vi, i);
                    KOTATSU_EXPECTED_TRY_V(
                        auto schema,
                        make_internal_tagged(&vi->alternatives[i](), vi->tag_field, alt_name));
                    alts.push_back(std::move(schema));
                    break;
                }

                case codec::tag_mode::adjacent: {
                    auto alt_name = alternative_name(vi, i);
                    KOTATSU_EXPECTED_TRY_V(auto schema, make_schema(&vi->alternatives[i]()));
                    dyn::Object branch{
                        {"type",       "object"},
                        {"properties",
                         dyn::Object{
                             {std::string(vi->tag_field), {{"const", alt_name}}},
                             {std::string(vi->content_field), std::move(schema)},
                         }                     },
                        {"required",
                         dyn::Array{
                             dyn::Value(vi->tag_field),
                             dyn::Value(vi->content_field),
                         }                     },
                    };
                    // Other keys are passed over unless unknown fields are denied.
                    if(vi->deny_unknown || opts.deny_unknown_fields) {
                        branch.insert("additionalProperties", false);
                    }
                    alts.push_back(std::move(branch));
                    break;
                }
            }
        }

        // Untagged alternatives can overlap — a numeric enum's underlying
        // range next to an integer both match the same document — so only the
        // tagged forms, disjoint by their tag, claim exactly-one semantics.
        return dyn::Value{
            {tagging == codec::tag_mode::none ? "anyOf" : "oneOf", std::move(alts)},
        };
    }

    schema_options opts;
    bool mark_alternatives;
    std::vector<std::pair<std::string, dyn::Value>> defs;
    std::unordered_map<const codec::type_info*, std::string> def_names;
    std::unordered_set<std::string> used_names;
    std::unordered_set<const codec::type_info*> emitted;
    const codec::type_info* root_ti = nullptr;
};

/// The fresh documents the default-annotation pass pairs schema bodies with:
/// for every default-constructible struct the decoder reads directly (see
/// decoder_reads_directly) reachable through the resolved type structure,
/// the document a value-initialized instance encodes to under the resolved
/// config, keyed by the normalized name the emitter gives the type's $def.
/// collect_fresh mirrors the emitter's reach — struct fields (flattened
/// included, skipped excluded), each under the config its slot's struct-level
/// spec merges to, optional and pointer inners, sequence and
/// set elements, map values (keys encode as object keys and carry no
/// schema), tuple elements, variant alternatives. A repr-routed type is
/// skipped, subtree included, field-level behavior attrs (`with`, `as`) are
/// not followed, and a fresh instance the encoder rejects (a NaN member
/// that is not finite, an unnamed enum value through codec::enum_names)
/// records no document: all three leave their bodies unannotated rather
/// than annotated wrongly.
struct FreshDefaults {
    std::unordered_map<std::string, dyn::Value> docs;
    std::unordered_set<std::string> claimed;
    std::unordered_set<const codec::type_info*> seen;
};

/// Annotates the emitted schema with `default` values in two layers, each
/// exact for what it describes:
///
/// - Fresh defaults. Decode value-initializes sequence and set elements, map
///   values, and selected variant alternatives before reading their fields,
///   so every $def body shows the document a freshly constructed instance of
///   its own type encodes to (FreshDefaults) — and so does every
///   internal-tagged variant branch, the one schema site whose struct body
///   is inlined rather than $def'd, via the emitter's alternative marker,
///   which the sweep consumes and strips. A type that cannot be freshly
///   constructed and encoded leaves its body unannotated rather than
///   guessing.
///
/// - Site defaults. Decode assigns struct fields in place on the enclosing
///   instance, so an absent non-required property keeps whatever the
///   enclosing default instance carries — member initializers included. Each
///   schema body is paired with a document of its own type (the root body
///   with the encoded root instance) and every non-required property copies
///   its encoded value onto its schema as `default`; a non-required struct
///   $ref or nullable property thus carries its whole encoded object, which
///   takes precedence at that site wherever an enclosing initializer
///   overrides the fresh values behind the shared $def. Required properties
///   must always appear, so a default would be a lie; they are skipped, as
///   are properties the document does not carry (encode-side skip
///   conditions, the null a nullable root encodes to), and an override on a
///   required site has no schema position of its own — the shared body keeps
///   speaking for fresh instances only.
class DefaultAnnotator {
public:
    explicit DefaultAnnotator(FreshDefaults fresh) : fresh(std::move(fresh)) {}

    void run(dyn::Object& root, const dyn::Value& root_doc) {
        annotate_properties(root, root_doc);
        if(auto* defs = root.find("$defs")) {
            for(auto& [name, body]: *defs->get_object()) {
                if(auto it = fresh.docs.find(name); it != fresh.docs.end()) {
                    annotate_properties(*body.get_object(), it->second);
                }
            }
        }
        sweep(root);
    }

private:
    static bool lists(const dyn::Value* required, std::string_view name) {
        return required != nullptr &&
               std::ranges::any_of(*required->get_array(),
                                   [&](const dyn::Value& v) { return v.get_string() == name; });
    }

    /// Whether body requires the property: in its required list, or, for a
    /// field the document may name by an alias, under one of its names.
    static bool is_required(const dyn::Object& body, std::string_view name) {
        if(lists(body.find("required"), name)) {
            return true;
        }
        const auto* named_once = body.find("allOf");
        return named_once != nullptr &&
               std::ranges::any_of(*named_once->get_array(), [&](const dyn::Value& names) {
                   const auto* any = names.get_object()->find("anyOf");
                   return any != nullptr &&
                          std::ranges::any_of(*any->get_array(), [&](const dyn::Value& one) {
                              return lists(one.get_object()->find("required"), name);
                          });
               });
    }

    static void annotate_properties(dyn::Object& body, const dyn::Value& doc_value) {
        // A non-struct root pairs with a non-object document (an array, a
        // scalar, the null a value-initialized nullable root encodes to);
        // it has no properties to annotate from.
        const auto* doc = doc_value.get_object();
        if(doc == nullptr) {
            return;
        }
        auto* props = body.find("properties");
        if(props == nullptr) {
            return;
        }
        for(auto& [name, prop]: *props->get_object()) {
            const auto* v = doc->find(name);
            if(v != nullptr && !is_required(body, name)) {
                prop.get_object()->insert("default", *v);
            }
        }
    }

    /// Walks every subschema position looking for marked internal-tagged
    /// branches; a marked branch takes its site defaults from the fresh
    /// document of its alternative type, and drops the marker either way.
    /// Only keys whose values are schemas are entered — never `default`,
    /// `enum`, or `const`, whose contents are documents, not schemas.
    void sweep(dyn::Object& schema) {
        if(const auto* marker = schema.find(alternative_marker)) {
            auto name = std::string(*marker->get_string());
            if(auto it = fresh.docs.find(name); it != fresh.docs.end()) {
                annotate_properties(schema, it->second);
            }
            schema.remove(alternative_marker);
        }
        for(std::string_view key: {"items", "additionalProperties"}) {
            if(auto* sub = schema.find(key)) {
                sweep_value(*sub);
            }
        }
        for(std::string_view key: {"prefixItems", "oneOf", "anyOf", "allOf"}) {
            if(auto* subs = schema.find(key)) {
                for(auto& sub: *subs->get_array()) {
                    sweep_value(sub);
                }
            }
        }
        for(std::string_view key: {"properties", "$defs"}) {
            if(auto* subs = schema.find(key)) {
                for(auto& [_, sub]: *subs->get_object()) {
                    sweep_value(sub);
                }
            }
        }
    }

    /// A tuple schema closes its prefix with "items": false — not a schema
    /// object, nothing to enter.
    void sweep_value(dyn::Value& sub) {
        if(auto* obj = sub.get_object()) {
            sweep(*obj);
        }
    }

    FreshDefaults fresh;
};

/// The format the documents the defaults come from are encoded under: F,
/// leaving out the fields whose default the schema does not state
/// (schema_default = false), wherever they sit, so that neither their own
/// property nor the whole-object default of one holding them states it.
template <typename F>
struct default_document_format : codec::extend<F> {
    constexpr static bool omit_unstated_defaults = true;
};

/// The options as the format F has them.
template <typename F>
schema_options options_of(schema_options options) {
    options.human_readable = F::human_readable;
    return options;
}

/// True when decode reads a T value directly: T resolves to itself, or is a
/// class derived from the std::variant it resolves to. Anything routed
/// through a codec::repr makes decode read the repr's value instead, whose
/// relationship to a fresh T is the repr's business; the defaults pass covers
/// only the direct kind and leaves the rest honestly unannotated.
template <typename T, typename F>
constexpr bool decoder_reads_directly() {
    using resolved = codec::resolved_repr_t<T, F>;
    if constexpr(meta::derived_variant<T>) {
        return std::same_as<resolved, meta::variant_base_t<T>>;
    } else {
        return std::same_as<resolved, T>;
    }
}

template <typename T, typename Config>
void collect_fresh(FreshDefaults& out);

template <typename Config, typename... Slots>
void collect_fresh_slots(FreshDefaults& out, kotatsu::type_list<Slots...>) {
    (collect_fresh<typename Slots::raw_type,
                   codec::node_config_t<codec::members_config_t<Config>,
                                        typename Slots::raw_type,
                                        typename Slots::attrs>>(out),
     ...);
}

template <typename Config, typename... Ts>
void collect_fresh_alternatives(FreshDefaults& out, std::type_identity<std::variant<Ts...>>) {
    (collect_fresh<Ts, Config>(out), ...);
}

template <typename T, typename Config>
void collect_fresh(FreshDefaults& out) {
    // A repr-routed type is skipped, subtree included: decode reads the
    // repr's value, not a T, so no honest fresh document exists here.
    if constexpr(decoder_reads_directly<T, codec::members_config_t<Config>>()) {
        using tk = meta::type_kind;
        using resolved = codec::resolved_repr_t<T, codec::members_config_t<Config>>;
        constexpr tk kind = meta::kind_of<resolved>();
        if constexpr(kind == tk::structure) {
            const codec::type_info& ti = codec::type_info_of<T, Config>();
            if(!out.seen.insert(&ti).second) {
                return;
            }
            // The resolved config carries T's own structural annotations (an
            // annotated root or container element — slots merge theirs
            // before recursing), so the document keys and field names below
            // match the schema the emitter derives from the same resolution.
            using cfg = codec::resolved_config_t<T, Config>;
            auto name = kotatsu::naming::normalize_identifier(ti.type_name);
            if(!out.claimed.insert(name).second) {
                // Two distinct types under one normalized name: the emitter
                // rejects the collision for $def'd types, but an inlined
                // internal-tagged alternative shares the namespace silently
                // — annotate neither rather than pair one with the other's
                // document.
                out.docs.erase(name);
            } else if constexpr(std::is_default_constructible_v<T>) {
                if(auto text = to_string<default_document_format<cfg>>(T())) {
                    if(auto doc = from_string<dyn::Value>(*text)) {
                        out.docs.emplace(name, std::move(*doc));
                    }
                }
            }
            collect_fresh_slots<cfg>(out, typename codec::virtual_schema<resolved, cfg>::slots{});
        } else if constexpr(kind == tk::optional) {
            collect_fresh<typename resolved::value_type, Config>(out);
        } else if constexpr(kind == tk::pointer) {
            collect_fresh<typename resolved::element_type, Config>(out);
        } else if constexpr(kind == tk::array || kind == tk::set) {
            collect_fresh<std::ranges::range_value_t<resolved>, Config>(out);
        } else if constexpr(kind == tk::map) {
            collect_fresh<typename std::ranges::range_value_t<resolved>::second_type, Config>(out);
        } else if constexpr(kind == tk::tuple) {
            [&out]<std::size_t... Is>(std::index_sequence<Is...>) {
                (collect_fresh<std::tuple_element_t<Is, resolved>, Config>(out), ...);
            }(std::make_index_sequence<std::tuple_size_v<resolved>>{});
        } else if constexpr(kind == tk::variant) {
            collect_fresh_alternatives<Config>(out, std::type_identity<resolved>{});
        }
        // Scalars, enums, bytes, any: leaves without annotatable structure.
    }
}

}  // namespace detail

inline std::expected<dyn::Value, rich_error> schema(const codec::type_info& root,
                                                    const schema_options& options = {}) {
    return detail::SchemaEmitter{options}.emit(root);
}

namespace detail {

inline std::expected<std::string, rich_error> stringify(dyn::Value value, bool pretty) {
    KOTATSU_EXPECTED_TRY_V(auto compact, to_string(std::move(value)));
    if(!pretty) {
        return compact;
    }
    return prettify(compact);
}

}  // namespace detail

/// When T is default-constructible and the decoder reads it directly — T
/// resolves to itself, or is a class derived from the std::variant it
/// resolves to — the schema also carries `default` annotations: fresh
/// instances are encoded through the real JSON encoder under F and
/// parsed back into documents, so the values match what to_string emits byte
/// for byte (enum renames, structural annotations on the root
/// itself included). Root properties take the values of T(); each $def and
/// inlined variant branch takes the values of a freshly constructed instance
/// of its own type, with non-required sites layering their whole-object
/// defaults on top (see DefaultAnnotator). Two consequences of riding the
/// real encoder: T() must encode under F — an instance the encoder
/// rejects (a member that is NaN or an infinity, an enum value without a
/// reflected name through codec::enum_names) fails schema generation with that
/// error — and a T whose fields the codec cannot serialize fails to compile,
/// exactly like to_string itself. A repr-routed root gets an honestly
/// unannotated schema; an opaque root — one whose JSON-resolved
/// representation still reflects as kind unknown — keeps reporting the
/// emission error at runtime.
///
/// A reader taking a property's default takes the one at the property's own
/// site over the one inside the $def it refers to: the site's default is the
/// whole value an enclosing initializer gives it. A field annotated
/// `schema_default = false` has no default stated anywhere. The bounds
/// (`minimum`, `maximum`) and the values (`choices`) a field annotation
/// states join its schema for the schema's readers; the decoder checks none
/// of them.
template <typename T, typename F = json::format>
std::expected<dyn::Value, rich_error> schema(const schema_options& options = {}) {
    static_assert(std::derived_from<F, json::format>, "a JSON format extends json::format");
    using resolved = codec::resolved_repr_t<T, F>;
    constexpr bool annotate_defaults = std::is_default_constructible_v<T> &&
                                       meta::kind_of<resolved>() != meta::type_kind::unknown &&
                                       detail::decoder_reads_directly<T, F>();
    KOTATSU_EXPECTED_TRY_V(
        auto result,
        (detail::SchemaEmitter{detail::options_of<F>(options), annotate_defaults}.emit(
            codec::type_info_of<T, F>())));
    if constexpr(annotate_defaults) {
        KOTATSU_EXPECTED_TRY_V(auto text, to_string<detail::default_document_format<F>>(T()));
        KOTATSU_EXPECTED_TRY_V(auto doc, from_string<dyn::Value>(text));
        detail::FreshDefaults fresh;
        detail::collect_fresh<T, F>(fresh);
        detail::DefaultAnnotator{std::move(fresh)}.run(*result.get_object(), doc);
    }
    return result;
}

inline std::expected<std::string, rich_error> schema_string(const codec::type_info& root,
                                                            const schema_options& options = {}) {
    KOTATSU_EXPECTED_TRY_V(auto value, schema(root, options));
    return detail::stringify(std::move(value), options.pretty);
}

template <typename T, typename F = json::format>
std::expected<std::string, rich_error> schema_string(const schema_options& options = {}) {
    KOTATSU_EXPECTED_TRY_V(auto value, (schema<T, F>(options)));
    return detail::stringify(std::move(value), options.pretty);
}

}  // namespace kotatsu::codec::json
