#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "kota/support/expected_try.h"
#include "kota/support/naming.h"
#include "kota/meta/type_info.h"
#include "kota/codec/content/document.h"
#include "kota/codec/content/serializer.h"
#include "kota/codec/json/serializer.h"

namespace kota::codec::json {

namespace detail {

class SchemaEmitter {
    using tk = meta::type_kind;
    using result_t = std::expected<content::Value, error>;

public:
    result_t emit(const meta::type_info& root) {
        root_ti = unwrap(&root);
        if(root_ti->kind == tk::structure) {
            auto name = kota::naming::normalize_identifier(root_ti->type_name);
            used_names.insert(name);
            def_names.emplace(root_ti, std::move(name));
        }

        content::Object schema;
        schema.insert("$schema", "https://json-schema.org/draft/2020-12/schema");

        KOTA_EXPECTED_TRY(merge_schema_fields(schema, &root));

        if(!defs.empty()) {
            content::Object defs_obj;
            for(auto& [name, value]: defs) {
                defs_obj.insert(std::move(name), std::move(value));
            }
            schema.insert("$defs", std::move(defs_obj));
        }

        return content::Value(std::move(schema));
    }

private:
    const static meta::type_info* unwrap(const meta::type_info* ti) {
        while(ti->kind == tk::optional || ti->kind == tk::pointer) {
            ti = &static_cast<const meta::optional_type_info*>(ti)->inner();
        }
        return ti;
    }

    static std::string alternative_name(const meta::variant_type_info* vi, std::size_t i) {
        if(i < vi->alt_names.size()) {
            return std::string(vi->alt_names[i]);
        }
        return kota::naming::normalize_identifier(vi->alternatives[i]().type_name);
    }

    std::expected<std::string_view, error> def_name(const meta::type_info* ti) {
        auto it = def_names.find(ti);
        if(it != def_names.end()) {
            return std::string_view(it->second);
        }
        auto name = kota::naming::normalize_identifier(ti->type_name);
        if(!used_names.insert(name).second) {
            return std::unexpected(error::custom(
                error_kind::invalid_state,
                std::format("duplicate $defs name '{}' from type '{}'", name, ti->type_name)));
        }
        auto [pos, _] = def_names.emplace(ti, std::move(name));
        return std::string_view(pos->second);
    }

    result_t make_nullable(const meta::type_info* ti) {
        auto* inner = unwrap(ti);
        KOTA_EXPECTED_TRY_V(auto schema, make_schema(inner));
        return content::Value{
            {"oneOf", content::Array{std::move(schema), content::Value{{"type", "null"}}}},
        };
    }

    result_t make_schema(const meta::type_info* ti) {
        if(ti->kind == tk::optional || ti->kind == tk::pointer) {
            return make_nullable(ti);
        }

        switch(ti->kind) {
            case tk::null:
                return content::Value{
                    {"type", "null"}
                };
            case tk::boolean:
                return content::Value{
                    {"type", "boolean"}
                };
            case tk::character:
            case tk::string:
            case tk::bytes:
                return content::Value{
                    {"type", "string"}
                };
            case tk::float32:
            case tk::float64:
                return content::Value{
                    {"type", "number"}
                };
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
            case tk::enumeration: return make_enum(ti);
            case tk::array:
            case tk::set: return make_array(ti);
            case tk::map: return make_map(ti);
            case tk::tuple: return make_tuple(ti);
            case tk::structure: return make_struct_ref(ti);
            case tk::variant: return make_variant(ti);
            case tk::any: return content::Value(content::Object{});
            default:
                return std::unexpected(error::custom(
                    error_kind::invalid_state,
                    std::format("unsupported type kind '{}' for JSON Schema generation",
                                ti->type_name)));
        }
    }

    std::expected<void, error> merge_schema_fields(content::Object& target,
                                                   const meta::type_info* ti) {
        ti = unwrap(ti);
        if(ti->kind == tk::structure) {
            return add_struct_body(target, static_cast<const meta::struct_type_info*>(ti));
        }
        KOTA_EXPECTED_TRY_V(auto schema, make_schema(ti));
        if(auto* obj = schema.get_object()) {
            for(auto& entry: *obj) {
                target.insert(std::string(entry.key), std::move(entry.value));
            }
        }
        return {};
    }

    std::expected<void, error> add_struct_body(content::Object& target,
                                               const meta::struct_type_info* si) {
        target.insert("type", "object");
        KOTA_EXPECTED_TRY_V(auto props, make_properties(si));
        target.insert("properties", std::move(props));
        add_required(target, si);
        if(si->deny_unknown) {
            target.insert("additionalProperties", false);
        }
        return {};
    }

    static result_t make_integer(std::int64_t min_val, std::int64_t max_val) {
        return content::Value{
            {"type",    "integer"},
            {"minimum", min_val  },
            {"maximum", max_val  },
        };
    }

    static result_t make_unsigned(std::uint64_t max_val) {
        return content::Value{
            {"type",    "integer"       },
            {"minimum", std::uint64_t{0}},
            {"maximum", max_val         },
        };
    }

    static result_t make_enum(const meta::type_info* ti) {
        auto* ei = static_cast<const meta::enum_type_info*>(ti);
        content::Array values;
        for(const auto& name: ei->member_names) {
            values.push_back(content::Value(name));
        }
        return content::Value{
            {"enum", std::move(values)}
        };
    }

    result_t make_array(const meta::type_info* ti) {
        auto* ai = static_cast<const meta::array_type_info*>(ti);
        KOTA_EXPECTED_TRY_V(auto items, make_schema(&ai->element()));
        content::Object obj{
            {"type",  "array"         },
            {"items", std::move(items)},
        };
        if(ti->kind == tk::set) {
            obj.insert("uniqueItems", true);
        }
        return content::Value(std::move(obj));
    }

    result_t make_map(const meta::type_info* ti) {
        auto* mi = static_cast<const meta::map_type_info*>(ti);
        KOTA_EXPECTED_TRY_V(auto val_schema, make_schema(&mi->value()));
        return content::Value{
            {"type",                 "object"             },
            {"additionalProperties", std::move(val_schema)},
        };
    }

    result_t make_tuple(const meta::type_info* ti) {
        auto* tup = static_cast<const meta::tuple_type_info*>(ti);
        content::Array items;
        for(std::size_t i = 0; i < tup->elements.size(); ++i) {
            KOTA_EXPECTED_TRY_V(auto elem, make_schema(&tup->elements[i]()));
            items.push_back(std::move(elem));
        }
        auto size = static_cast<std::uint64_t>(tup->elements.size());
        return content::Value{
            {"type",        "array"         },
            {"prefixItems", std::move(items)},
            {"items",       false           },
            {"minItems",    size            },
            {"maxItems",    size            },
        };
    }

    result_t make_struct_ref(const meta::type_info* ti) {
        if(ti == root_ti) {
            return content::Value{
                {"$ref", "#"}
            };
        }
        KOTA_EXPECTED_TRY_V(auto name, def_name(ti));
        KOTA_EXPECTED_TRY(ensure_struct_def(ti));
        return content::Value{
            {"$ref", std::format("#/$defs/{}", name)}
        };
    }

    std::expected<void, error> ensure_struct_def(const meta::type_info* ti) {
        if(!emitted.insert(ti).second) {
            return {};
        }
        auto* si = static_cast<const meta::struct_type_info*>(ti);
        KOTA_EXPECTED_TRY_V(auto name, def_name(ti));
        content::Object body;
        KOTA_EXPECTED_TRY(add_struct_body(body, si));
        defs.emplace_back(std::string(name), std::move(body));
        return {};
    }

    result_t make_properties(const meta::struct_type_info* si) {
        content::Object props;
        for(const auto& f: si->fields) {
            KOTA_EXPECTED_TRY_V(auto schema, make_schema(&f.type()));
            props.insert(std::string(f.name), std::move(schema));
        }
        return content::Value(std::move(props));
    }

    static void add_required(content::Object& target, const meta::struct_type_info* si) {
        content::Array required;
        for(const auto& f: si->fields) {
            const meta::type_info& ft = f.type();
            bool is_optional = f.has_default || ft.kind == tk::optional || ft.kind == tk::pointer;
            if(!is_optional) {
                required.push_back(content::Value(f.name));
            }
        }
        if(!required.empty()) {
            target.insert("required", std::move(required));
        }
    }

    static content::Value make_tag_const(std::string_view tag_field, std::string_view alt_name) {
        return {
            {"properties", {{std::string(tag_field), {{"const", alt_name}}}}},
            {"required",   content::Array{content::Value(tag_field)}        },
        };
    }

    result_t make_internal_tagged(const meta::type_info* ti,
                                  std::string_view tag_field,
                                  std::string_view alt_name) {
        ti = unwrap(ti);
        if(ti->kind == tk::structure) {
            auto* si = static_cast<const meta::struct_type_info*>(ti);
            KOTA_EXPECTED_TRY_V(auto props, make_properties(si));
            auto* props_obj = props.get_object();
            props_obj->insert(std::string(tag_field),
                              content::Value{
                                  {"const", alt_name}
            });
            content::Object obj;
            obj.insert("type", "object");
            obj.insert("properties", std::move(props));
            content::Array required;
            for(const auto& f: si->fields) {
                const meta::type_info& ft = f.type();
                bool is_opt = f.has_default || ft.kind == tk::optional || ft.kind == tk::pointer;
                if(!is_opt) {
                    required.push_back(content::Value(f.name));
                }
            }
            required.push_back(content::Value(tag_field));
            obj.insert("required", std::move(required));
            if(si->deny_unknown) {
                obj.insert("additionalProperties", false);
            }
            return content::Value(std::move(obj));
        }
        KOTA_EXPECTED_TRY_V(auto schema, make_schema(ti));
        return content::Value{
            {"allOf",
             content::Array{
                 std::move(schema),
                 make_tag_const(tag_field, alt_name),
             }},
        };
    }

    result_t make_variant(const meta::type_info* ti) {
        auto* vi = static_cast<const meta::variant_type_info*>(ti);
        content::Array one_of;

        for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
            switch(vi->tagging) {
                case meta::tag_mode::none: {
                    KOTA_EXPECTED_TRY_V(auto schema, make_schema(&vi->alternatives[i]()));
                    one_of.push_back(std::move(schema));
                    break;
                }

                case meta::tag_mode::external: {
                    auto alt_name = alternative_name(vi, i);
                    KOTA_EXPECTED_TRY_V(auto schema, make_schema(&vi->alternatives[i]()));
                    one_of.push_back({
                        {"type",                 "object"                                },
                        {"properties",           {{alt_name, std::move(schema)}}         },
                        {"required",             content::Array{content::Value(alt_name)}},
                        {"additionalProperties", false                                   },
                    });
                    break;
                }

                case meta::tag_mode::internal: {
                    auto alt_name = alternative_name(vi, i);
                    KOTA_EXPECTED_TRY_V(
                        auto schema,
                        make_internal_tagged(&vi->alternatives[i](), vi->tag_field, alt_name));
                    one_of.push_back(std::move(schema));
                    break;
                }

                case meta::tag_mode::adjacent: {
                    auto alt_name = alternative_name(vi, i);
                    KOTA_EXPECTED_TRY_V(auto schema, make_schema(&vi->alternatives[i]()));
                    one_of.push_back({
                        {"type",                 "object"},
                        {"properties",
                         content::Object{
                             {std::string(vi->tag_field), {{"const", alt_name}}},
                             {std::string(vi->content_field), std::move(schema)},
                         }                               },
                        {"required",
                         content::Array{
                             content::Value(vi->tag_field),
                             content::Value(vi->content_field),
                         }                               },
                        {"additionalProperties", false   },
                    });
                    break;
                }
            }
        }

        return content::Value{
            {"oneOf", std::move(one_of)}
        };
    }

    std::vector<std::pair<std::string, content::Value>> defs;
    std::unordered_map<const meta::type_info*, std::string> def_names;
    std::unordered_set<std::string> used_names;
    std::unordered_set<const meta::type_info*> emitted;
    const meta::type_info* root_ti = nullptr;
};

}  // namespace detail

inline std::expected<content::Value, error> schema(const meta::type_info& root) {
    return detail::SchemaEmitter{}.emit(root);
}

template <typename T>
std::expected<content::Value, error> schema() {
    return schema(meta::type_info_of<T>());
}

inline std::expected<std::string, error> schema_string(const meta::type_info& root,
                                                       bool pretty = false) {
    KOTA_EXPECTED_TRY_V(auto value, schema(root));
    KOTA_EXPECTED_TRY_V(auto compact, to_json(std::move(value)));
    if(!pretty) {
        return compact;
    }
    simdjson::dom::parser parser;
    simdjson::dom::element doc;
    if(auto err = parser.parse(compact).get(doc)) {
        return std::unexpected(error(make_error(err)));
    }
    return simdjson::prettify(doc);
}

template <typename T>
std::expected<std::string, error> schema_string(bool pretty = false) {
    return schema_string(meta::type_info_of<T>(), pretty);
}

}  // namespace kota::codec::json
