#pragma once

#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "kota/support/naming.h"
#include "kota/meta/type_info.h"
#include "kota/codec/content/document.h"
#include "kota/codec/content/serializer.h"
#include "kota/codec/json/serializer.h"

namespace kota::codec::json {

namespace detail {

class SchemaEmitter {
    using tk = meta::type_kind;

public:
    content::Value emit(const meta::type_info& root) {
        root_ti = unwrap(&root);
        if(root_ti->kind == tk::structure) {
            used_names.insert(kota::naming::normalize_identifier(root_ti->type_name));
        }

        content::Object schema;
        schema.insert("$schema", "https://json-schema.org/draft/2020-12/schema");

        merge_schema_fields(schema, &root);

        if(!defs.empty()) {
            content::Object defs_obj;
            for(auto& [name, value]: defs) {
                defs_obj.insert(std::move(name), std::move(value));
            }
            schema.insert("$defs", std::move(defs_obj));
        }

        return schema;
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

    const std::string& canonical_name(const meta::type_info* ti) {
        auto it = def_names.find(ti);
        if(it != def_names.end()) {
            return it->second;
        }
        auto base = kota::naming::normalize_identifier(ti->type_name);
        auto name = base;
        for(std::size_t counter = 2; used_names.contains(name); ++counter) {
            name = std::format("{}_{}", base, counter);
        }
        used_names.insert(name);
        return def_names.emplace(ti, std::move(name)).first->second;
    }

    content::Value make_schema(const meta::type_info* ti) {
        ti = unwrap(ti);

        switch(ti->kind) {
            case tk::null:
                return {
                    {"type", "null"}
                };
            case tk::boolean:
                return {
                    {"type", "boolean"}
                };
            case tk::character:
            case tk::string:
            case tk::bytes:
                return {
                    {"type", "string"}
                };
            case tk::float32:
            case tk::float64:
                return {
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
            default: return content::Object{};
        }
    }

    void merge_schema_fields(content::Object& target, const meta::type_info* ti) {
        ti = unwrap(ti);
        if(ti->kind == tk::structure) {
            add_struct_body(target, static_cast<const meta::struct_type_info*>(ti));
            return;
        }
        auto schema = make_schema(ti);
        if(auto* obj = schema.get_object()) {
            for(auto& entry: *obj) {
                target.insert(std::string(entry.key), std::move(entry.value));
            }
        }
    }

    void add_struct_body(content::Object& target, const meta::struct_type_info* si) {
        target.insert("type", "object");
        target.insert("properties", make_properties(si));
        add_required(target, si);
        if(si->deny_unknown) {
            target.insert("additionalProperties", false);
        }
    }

    static content::Value make_integer(std::int64_t min_val, std::int64_t max_val) {
        return {
            {"type",    "integer"},
            {"minimum", min_val  },
            {"maximum", max_val  },
        };
    }

    static content::Value make_unsigned(std::uint64_t max_val) {
        return {
            {"type",    "integer"       },
            {"minimum", std::uint64_t{0}},
            {"maximum", max_val         },
        };
    }

    static content::Value make_enum(const meta::type_info* ti) {
        auto* ei = static_cast<const meta::enum_type_info*>(ti);
        content::Array values;
        for(const auto& name: ei->member_names) {
            values.push_back(content::Value(name));
        }
        return {
            {"enum", std::move(values)}
        };
    }

    content::Value make_array(const meta::type_info* ti) {
        auto* ai = static_cast<const meta::array_type_info*>(ti);
        content::Object obj{
            {"type",  "array"                    },
            {"items", make_schema(&ai->element())},
        };
        if(ti->kind == tk::set) {
            obj.insert("uniqueItems", true);
        }
        return obj;
    }

    content::Value make_map(const meta::type_info* ti) {
        auto* mi = static_cast<const meta::map_type_info*>(ti);
        return {
            {"type",                 "object"                 },
            {"additionalProperties", make_schema(&mi->value())},
        };
    }

    content::Value make_tuple(const meta::type_info* ti) {
        auto* tup = static_cast<const meta::tuple_type_info*>(ti);
        content::Array items;
        for(std::size_t i = 0; i < tup->elements.size(); ++i) {
            items.push_back(make_schema(&tup->elements[i]()));
        }
        return {
            {"type",        "array"         },
            {"prefixItems", std::move(items)},
        };
    }

    content::Value make_struct_ref(const meta::type_info* ti) {
        if(ti == root_ti) {
            return {
                {"$ref", "#"}
            };
        }
        const auto& name = canonical_name(ti);
        ensure_struct_def(ti);
        return {
            {"$ref", std::format("#/$defs/{}", name)}
        };
    }

    void ensure_struct_def(const meta::type_info* ti) {
        if(!emitted.insert(ti).second) {
            return;
        }
        auto* si = static_cast<const meta::struct_type_info*>(ti);
        content::Object body;
        add_struct_body(body, si);
        defs.emplace_back(canonical_name(ti), std::move(body));
    }

    content::Value make_properties(const meta::struct_type_info* si) {
        content::Object props;
        for(const auto& f: si->fields) {
            props.insert(std::string(f.name), make_schema(&f.type()));
        }
        return props;
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

    content::Value make_variant(const meta::type_info* ti) {
        auto* vi = static_cast<const meta::variant_type_info*>(ti);
        content::Array one_of;

        for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
            switch(vi->tagging) {
                case meta::tag_mode::none:
                    one_of.push_back(make_schema(&vi->alternatives[i]()));
                    break;

                case meta::tag_mode::external: {
                    auto alt_name = alternative_name(vi, i);
                    one_of.push_back({
                        {"type",                 "object"                                         },
                        {"properties",           {{alt_name, make_schema(&vi->alternatives[i]())}}},
                        {"required",             content::Array{content::Value(alt_name)}         },
                        {"additionalProperties", false                                            },
                    });
                    break;
                }

                case meta::tag_mode::internal: {
                    auto alt_name = alternative_name(vi, i);
                    one_of.push_back({
                        {"allOf",
                         content::Array{
                             make_schema(&vi->alternatives[i]()),
                             make_tag_const(vi->tag_field, alt_name),
                         }},
                    });
                    break;
                }

                case meta::tag_mode::adjacent: {
                    auto alt_name = alternative_name(vi, i);
                    one_of.push_back({
                        {"type",                 "object"},
                        {"properties",
                         content::Object{
                             {std::string(vi->tag_field), {{"const", alt_name}}},
                             {std::string(vi->content_field), make_schema(&vi->alternatives[i]())},
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

        return {
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

inline content::Value schema(const meta::type_info& root) {
    return detail::SchemaEmitter{}.emit(root);
}

template <typename T>
content::Value schema() {
    return schema(meta::type_info_of<T>());
}

inline std::string schema_string(const meta::type_info& root, bool pretty = false) {
    auto compact = to_json(schema(root)).value();
    if(!pretty) {
        return compact;
    }
    simdjson::dom::parser parser;
    return simdjson::prettify(parser.parse(compact));
}

template <typename T>
std::string schema_string(bool pretty = false) {
    return schema_string(meta::type_info_of<T>(), pretty);
}

}  // namespace kota::codec::json
