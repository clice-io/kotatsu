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
};

namespace detail {

/// The documents the defaults a schema states come from: the one the root
/// value encodes to, for the root's properties, and, by the info of each
/// struct, the one a value-initialized instance of it encodes to, for its
/// $def or its inlined body. Null where there is none.
struct DefaultDocuments {
    dyn::Value root;
    std::unordered_map<const codec::type_info*, dyn::Value> fresh;
};

class SchemaEmitter {
    using tk = meta::type_kind;
    using result_t = std::expected<dyn::Value, rich_error>;

public:
    /// human_readable says whether the documents carry variant tags: a format
    /// that is not human readable writes a tagged variant as its bare
    /// alternative. Without defaults, the schema states none.
    SchemaEmitter(const schema_options& opts,
                  bool human_readable,
                  const DefaultDocuments* defaults = nullptr) :
        opts(opts), human_readable(human_readable), defaults(defaults) {}

    result_t emit(const codec::type_info& root) {
        root_ti = unwrap(&root);

        dyn::Object schema;
        schema.insert("$schema", "https://json-schema.org/draft/2020-12/schema");

        if(root_ti->kind == tk::structure) {
            // The root's references are "#": no $def may take its name.
            used_names.insert(kotatsu::naming::normalize_identifier(root_ti->type_name));
            KOTATSU_EXPECTED_TRY(
                add_struct_body(schema,
                                static_cast<const codec::struct_type_info*>(root_ti),
                                defaults ? defaults->root.get_object() : nullptr));
        } else {
            KOTATSU_EXPECTED_TRY_V(auto body, make_schema(root_ti));
            for(auto& [k, v]: *body.get_object()) {
                schema.insert(std::string(k), std::move(v));
            }
        }

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

    /// The document the defaults of struct si's body come from: none without
    /// defaults, and none for a struct reached through a repr or whose fresh
    /// instance does not encode.
    const dyn::Object* document_of(const codec::struct_type_info* si) const {
        if(defaults == nullptr) {
            return nullptr;
        }
        auto it = defaults->fresh.find(si);
        return it == defaults->fresh.end() ? nullptr : it->second.get_object();
    }

    result_t make_schema(const codec::type_info* ti) {
        switch(ti->kind) {
            case tk::optional:
            case tk::pointer: {
                KOTATSU_EXPECTED_TRY_V(auto schema, make_schema(unwrap(ti)));
                // anyOf, not oneOf: the inner schema may itself admit null (a
                // monostate alternative, an unconstrained any), and a null
                // document must not fail by matching both branches.
                return dyn::Value{
                    {"anyOf", dyn::Array{std::move(schema), dyn::Value{{"type", "null"}}}},
                };
            }
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
            case tk::float64:
                // A number: the encoder fails on a float that is not finite,
                // which JSON has no number for.
                return dyn::Value{
                    {"type", "number"}
                };
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

    /// The property an internally tagged alternative's object carries next to
    /// its struct's own fields.
    struct InternalTag {
        std::string_view field;
        std::string_view alt_name;
        /// Whether the variant denies keys its alternative has no field for.
        bool deny_unknown = false;
    };

    /// A struct's object schema, written into target, its properties stating
    /// the defaults doc gives them; with a tag, the object also carries the tag
    /// property, required after the struct's own required fields.
    std::expected<void, rich_error> add_struct_body(dyn::Object& target,
                                                    const codec::struct_type_info* si,
                                                    const dyn::Object* doc,
                                                    const InternalTag* tag = nullptr) {
        target.insert("type", "object");
        if(!si->description.empty()) {
            target.insert("description", si->description);
        }
        KOTATSU_EXPECTED_TRY_V(auto props, make_properties(si, doc));
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
        if(si->deny_unknown || opts.deny_unknown_fields || (tag && tag->deny_unknown)) {
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

    /// A reference to the schema of struct ti: the root's, or its $def, which
    /// the first reference writes. The references inside a $def to its own
    /// struct find its name taken, so a recursive struct ends there.
    result_t make_struct_ref(const codec::type_info* ti) {
        if(ti == root_ti) {
            return dyn::Value{
                {"$ref", "#"}
            };
        }
        // Held by address: emitting a body adds names, which may rehash.
        const std::string* name = nullptr;
        if(auto found = def_names.find(ti); found != def_names.end()) {
            name = &found->second;
        } else {
            auto spelled = kotatsu::naming::normalize_identifier(ti->type_name);
            if(!used_names.insert(spelled).second) {
                return std::unexpected(
                    rich_error(std::format("duplicate $defs name '{}' from type '{}'",
                                           spelled,
                                           ti->type_name)));
            }
            name = &def_names.emplace(ti, std::move(spelled)).first->second;
            auto* si = static_cast<const codec::struct_type_info*>(ti);
            dyn::Object body;
            KOTATSU_EXPECTED_TRY(add_struct_body(body, si, document_of(si)));
            defs.emplace_back(*name, std::move(body));
        }
        return dyn::Value{
            {"$ref", std::format("#/$defs/{}", *name)}
        };
    }

    /// The schemas of si's fields, by name and alias; a field the document
    /// may leave out states the value doc gives it as its default.
    result_t make_properties(const codec::struct_type_info* si, const dyn::Object* doc) {
        dyn::Object props;
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
            // A required field must appear, so a default would be a lie; and
            // doc leaves out what its encode leaves out.
            if(doc != nullptr && !is_required(f)) {
                if(const auto* value = doc->find(f.name)) {
                    schema.get_object()->insert("default", *value);
                }
            }
            // No two fields answer to one name, which type_info checks.
            props.insert(std::string(f.name), std::move(schema));
            for(auto alias: f.aliases) {
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

    /// An internally tagged alternative: its struct's object, the tag among
    /// its properties. A std::monostate is a unit variant, the tag alone, as
    /// a struct without fields writes it.
    result_t make_internal_tagged(const codec::type_info* ti, const InternalTag& tag) {
        const static codec::struct_type_info unit = {
            {tk::structure, "std::monostate"},
            false,
            {},
        };
        ti = unwrap(ti);
        if(ti->kind == tk::null) {
            ti = &unit;
        }
        if(ti->kind != tk::structure) {
            // Only an info built by hand holds such an alternative: the codec
            // tags structs and std::monostate alone.
            KOTATSU_EXPECTED_TRY_V(auto schema, make_schema(ti));
            return dyn::Value{
                {"allOf",
                 dyn::Array{
                     std::move(schema),
                     dyn::Value{
                         {"properties", {{std::string(tag.field), {{"const", tag.alt_name}}}}},
                         {"required", dyn::Array{dyn::Value(tag.field)}},
                     },
                 }},
            };
        }
        auto* si = static_cast<const codec::struct_type_info*>(ti);
        dyn::Object obj;
        KOTATSU_EXPECTED_TRY(add_struct_body(obj, si, document_of(si), &tag));
        return dyn::Value(std::move(obj));
    }

    result_t make_variant(const codec::type_info* ti) {
        auto* vi = static_cast<const codec::variant_type_info*>(ti);
        auto tagging = human_readable ? vi->tagging : codec::tag_mode::none;
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
                    const InternalTag tag{
                        .field = vi->tag_field,
                        .alt_name = alt_name,
                        .deny_unknown = vi->deny_unknown,
                    };
                    KOTATSU_EXPECTED_TRY_V(auto schema,
                                           make_internal_tagged(&vi->alternatives[i](), tag));
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
    bool human_readable;
    const DefaultDocuments* defaults;
    std::vector<std::pair<std::string, dyn::Value>> defs;
    std::unordered_map<const codec::type_info*, std::string> def_names;
    std::unordered_set<std::string> used_names;
    const codec::type_info* root_ti = nullptr;
};

/// F, leaving out the fields annotated `schema_default = false` wherever they
/// sit, so that neither their own property nor the default of an object
/// holding them states their value: the format the documents the defaults
/// come from are encoded under.
template <typename F>
struct default_document_format : codec::extend<F> {
    constexpr static bool omit_unstated_defaults = true;
};

/// True when decode reads a T value directly: T resolves to itself, or is a
/// class derived from the std::variant it resolves to. Through a codec::repr,
/// decode reads the repr's value instead, whose relation to a fresh T is the
/// repr's business, so no default is stated there.
template <typename T, typename F>
constexpr bool decoder_reads_directly() {
    using resolved = codec::resolved_repr_t<T, F>;
    if constexpr(meta::derived_variant<T>) {
        return std::same_as<resolved, meta::variant_base_t<T>>;
    } else {
        return std::same_as<resolved, T>;
    }
}

/// The document a value-initialized T, a struct, encodes to in Scope, the
/// scope its node's attributes made, which T's own annotation is merged into
/// already; null when the encoder rejects it (a member that is NaN, an enum
/// value without a name), which leaves its body without defaults rather than
/// failing the schema.
template <typename T, typename Scope>
dyn::Value fresh_document() {
    rich_error error;
    codec::scoped_context<rich_error> guard(error);
    StringBuilder builder(default_capacity);
    ValueWriter<Scope> writer{builder};
    if(!codec::detail::encode_plain<Scope>(writer, T())) {
        return {};
    }
    return from_string<dyn::Value>(std::move(builder).take()).value_or(dyn::Value());
}

/// Records in documents.fresh, by its info, the document of each struct the
/// emitter reaches from T as the decoder reads it, under Format, the format
/// around T, and Attrs, its node's attributes (a field's, or else its
/// type's): a struct's fields, each in its struct's scope, an optional's or a
/// pointer's value, the elements of a sequence, a set or a tuple, a map's
/// values, a variant's alternatives. What a codec::repr routes elsewhere is
/// not followed, nor what a field's `as` or `with` does.
template <typename T, typename Format, typename Attrs = meta::attrs_of_t<T>>
void collect_fresh(DefaultDocuments& documents) {
    if constexpr(decoder_reads_directly<T, Format>()) {
        using tk = meta::type_kind;
        using resolved = codec::resolved_repr_t<T, Format>;
        constexpr tk kind = meta::kind_of<resolved>();
        if constexpr(kind == tk::structure) {
            // The scope the emitter's info for T was built in: the node's
            // attributes, the field's over T's own, merged onto Format.
            using scope = codec::node_format_t<Format, T, Attrs>;
            auto [at, added] = documents.fresh.try_emplace(&codec::detail::struct_info<T, scope>());
            if(!added) {
                return;
            }
            if constexpr(std::is_default_constructible_v<T>) {
                at->second = fresh_document<
                    T,
                    codec::node_format_t<default_document_format<Format>, T, Attrs>>();
            }
            [&]<typename... Slots>(kotatsu::type_list<Slots...>) {
                (collect_fresh<typename Slots::raw_type,
                               codec::members_format_t<scope>,
                               typename Slots::attrs>(documents),
                 ...);
            }(typename codec::virtual_schema<T, scope>::slots{});
        } else if constexpr(kind == tk::optional) {
            collect_fresh<typename resolved::value_type, Format>(documents);
        } else if constexpr(kind == tk::pointer) {
            collect_fresh<typename resolved::element_type, Format>(documents);
        } else if constexpr(kind == tk::array || kind == tk::set) {
            collect_fresh<std::ranges::range_value_t<resolved>, Format>(documents);
        } else if constexpr(kind == tk::map) {
            collect_fresh<typename std::ranges::range_value_t<resolved>::second_type, Format>(
                documents);
        } else if constexpr(kind == tk::tuple) {
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (collect_fresh<std::tuple_element_t<Is, resolved>, Format>(documents), ...);
            }(std::make_index_sequence<std::tuple_size_v<resolved>>{});
        } else if constexpr(kind == tk::variant) {
            [&]<typename... Ts>(std::type_identity<std::variant<Ts...>>) {
                (collect_fresh<Ts, Format>(documents), ...);
            }(std::type_identity<resolved>{});
        }
        // Scalars, enums, bytes, any: leaves without properties.
    }
}

inline std::expected<std::string, rich_error> stringify(dyn::Value value, bool pretty) {
    KOTATSU_EXPECTED_TRY_V(auto compact, to_string(std::move(value)));
    if(!pretty) {
        return compact;
    }
    return prettify(compact);
}

}  // namespace detail

/// The JSON Schema of the documents `root` describes, as a format that is
/// human readable writes them; it states no defaults, having no value to
/// take them from.
inline std::expected<dyn::Value, rich_error> schema(const codec::type_info& root,
                                                    const schema_options& options = {}) {
    return detail::SchemaEmitter{options, true}.emit(root);
}

/// The JSON Schema of the documents json::to_string<F> writes for a T and
/// json::from_string<F> reads into one.
///
/// When T is default-constructible and decode reads it directly (it resolves
/// to itself, or is a class derived from the std::variant it resolves to), the
/// schema also states defaults, the value a property the document leaves out
/// keeps:
///
/// - The root's properties take the values T() encodes to.
/// - The properties of a $def, and of an internally tagged alternative, whose
///   struct body is inlined, take what a value-initialized instance of their
///   struct encodes to: decode value-initializes elements, map values and
///   alternatives before reading them. A struct the encoder rejects fresh,
///   or one reached through a codec::repr, states none.
/// - A property the document may leave out holding a struct thus states the
///   whole object its holder gives it, which a reader takes over the defaults
///   inside the $def. A required property states none, and neither does a
///   field annotated `schema_default = false`, anywhere.
///
/// The defaults are what the JSON encoder writes under F, byte for byte, so
/// T() must encode: one that does not (a member that is NaN, an enum value
/// without a name through codec::enum_names) fails the schema with the
/// encoder's error, and a T whose fields the codec cannot write fails to
/// compile, as to_string does. An opaque T, which has no schema, fails at run
/// time.
///
/// The bounds (`minimum`, `maximum`) and the values (`choices`) a field's
/// annotation states join its schema for the schema's readers; the decoder
/// checks none of them.
template <typename T, typename F = json::format>
std::expected<dyn::Value, rich_error> schema(const schema_options& options = {}) {
    static_assert(std::derived_from<F, json::format>, "a JSON format extends json::format");
    const auto& root = codec::type_info::of<T, F>();
    if constexpr(std::is_default_constructible_v<T> &&
                 meta::kind_of<codec::resolved_repr_t<T, F>>() != meta::type_kind::unknown &&
                 detail::decoder_reads_directly<T, F>()) {
        detail::DefaultDocuments defaults;
        KOTATSU_EXPECTED_TRY_V(auto text, to_string<detail::default_document_format<F>>(T()));
        KOTATSU_EXPECTED_TRY_V(auto doc, from_string<dyn::Value>(text));
        defaults.root = std::move(doc);
        detail::collect_fresh<T, F>(defaults);
        return detail::SchemaEmitter{options, F::human_readable, &defaults}.emit(root);
    } else {
        return detail::SchemaEmitter{options, F::human_readable}.emit(root);
    }
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
