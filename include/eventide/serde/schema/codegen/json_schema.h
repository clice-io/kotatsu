#pragma once

#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>

#include "eventide/serde/content/dom.h"
#include "eventide/serde/schema/field_info.h"

namespace eventide::serde::schema::codegen::json_schema {

using val_t = yyjson_mut_val*;

inline std::string normalize_name(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for(char c: text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if(std::isalnum(u)) {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if(out.empty()) {
        out = "Unnamed";
    }
    if(std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

class emitter {
    using tk = type_kind;

public:
    std::string emit(const type_info* root) {
        defs = doc.unchecked_make_object();
        auto* root_obj = doc.unchecked_make_object();
        add_str(root_obj, "$schema", "https://json-schema.org/draft/2020-12/schema");

        auto* type_schema = build_schema(root);
        // Merge type_schema properties into root_obj
        merge_into(root_obj, type_schema);

        // Add $defs if any were collected
        if(has_defs) {
            add_val(root_obj, "$defs", defs);
        }

        doc.unchecked_set_root(root_obj);
        auto result = doc.to_json_string();
        return result ? std::move(*result) : std::string{};
    }

private:
    const static type_info* unwrap(const type_info* ti) {
        while(ti->kind == tk::optional || ti->kind == tk::pointer) {
            ti = static_cast<const optional_type_info*>(ti)->inner;
        }
        return ti;
    }

    val_t build_schema(const type_info* ti) {
        // Unwrap optional/pointer — the "required" logic handles nullability at field level
        ti = unwrap(ti);

        switch(ti->kind) {
            case tk::null: return make_type("null");
            case tk::boolean: return make_type("boolean");
            case tk::character:
            case tk::string: return make_type("string");
            case tk::bytes: return make_type("string");
            case tk::float32:
            case tk::float64: return make_type("number");
            case tk::int8: return make_integer(-128, 127);
            case tk::int16: return make_integer(-32768, 32767);
            case tk::int32:
                return make_integer(std::numeric_limits<std::int32_t>::min(),
                                    std::numeric_limits<std::int32_t>::max());
            case tk::int64:
                return make_integer(std::numeric_limits<std::int64_t>::min(),
                                    std::numeric_limits<std::int64_t>::max());
            case tk::uint8: return make_unsigned(255);
            case tk::uint16: return make_unsigned(65535);
            case tk::uint32: return make_unsigned(std::numeric_limits<std::uint32_t>::max());
            case tk::uint64: return make_unsigned(std::numeric_limits<std::uint64_t>::max());
            case tk::enumeration: return make_enum(ti);
            case tk::array:
            case tk::set: return make_array(ti);
            case tk::map: return make_map(ti);
            case tk::tuple: return make_tuple(ti);
            case tk::structure: return make_struct_ref(ti);
            case tk::variant: return make_variant(ti);
            default: return doc.unchecked_make_object();
        }
    }

    // --- Leaf types ---

    val_t make_type(std::string_view type_name) {
        auto* obj = doc.unchecked_make_object();
        add_str(obj, "type", type_name);
        return obj;
    }

    val_t make_integer(std::int64_t min_val, std::int64_t max_val) {
        auto* obj = doc.unchecked_make_object();
        add_str(obj, "type", "integer");
        add_int(obj, "minimum", min_val);
        add_int(obj, "maximum", max_val);
        return obj;
    }

    val_t make_unsigned(std::uint64_t max_val) {
        auto* obj = doc.unchecked_make_object();
        add_str(obj, "type", "integer");
        add_int(obj, "minimum", 0);
        add_uint(obj, "maximum", max_val);
        return obj;
    }

    // --- Enum ---

    val_t make_enum(const type_info* ti) {
        auto* ei = static_cast<const enum_type_info*>(ti);
        auto* obj = doc.unchecked_make_object();
        auto* arr = doc.unchecked_make_array();
        for(const auto& name: ei->member_names) {
            doc.unchecked_arr_add_val(arr, doc.unchecked_make_str(name));
        }
        add_val(obj, "enum", arr);
        return obj;
    }

    // --- Array / Set ---

    val_t make_array(const type_info* ti) {
        auto* ai = static_cast<const array_type_info*>(ti);
        auto* obj = doc.unchecked_make_object();
        add_str(obj, "type", "array");
        add_val(obj, "items", build_schema(ai->element));
        if(ti->kind == tk::set) {
            add_bool(obj, "uniqueItems", true);
        }
        return obj;
    }

    // --- Map ---

    val_t make_map(const type_info* ti) {
        auto* mi = static_cast<const map_type_info*>(ti);
        auto* obj = doc.unchecked_make_object();
        add_str(obj, "type", "object");
        add_val(obj, "additionalProperties", build_schema(mi->value));
        return obj;
    }

    // --- Tuple ---

    val_t make_tuple(const type_info* ti) {
        auto* tup = static_cast<const tuple_type_info*>(ti);
        auto* obj = doc.unchecked_make_object();
        add_str(obj, "type", "array");
        auto* items = doc.unchecked_make_array();
        for(const auto* elem: tup->elements) {
            doc.unchecked_arr_add_val(items, build_schema(elem));
        }
        add_val(obj, "prefixItems", items);
        return obj;
    }

    // --- Struct ---

    val_t make_struct_ref(const type_info* ti) {
        auto name = normalize_name(ti->type_name);
        ensure_struct_def(ti, name);
        auto* obj = doc.unchecked_make_object();
        add_str(obj, "$ref", "#/$defs/" + name);
        return obj;
    }

    void ensure_struct_def(const type_info* ti, const std::string& name) {
        if(!visited.insert(name).second) {
            return;
        }

        auto* si = static_cast<const struct_type_info*>(ti);
        auto* schema = doc.unchecked_make_object();
        add_str(schema, "type", "object");

        auto* props = doc.unchecked_make_object();
        auto* required = doc.unchecked_make_array();
        bool any_required = false;

        for(const auto& f: si->fields) {
            add_val(props, f.name, build_schema(f.type));
            bool is_optional =
                f.has_default || f.type->kind == tk::optional || f.type->kind == tk::pointer;
            if(!is_optional) {
                doc.unchecked_arr_add_val(required, doc.unchecked_make_str(f.name));
                any_required = true;
            }
        }

        add_val(schema, "properties", props);
        if(any_required) {
            add_val(schema, "required", required);
        }
        if(si->deny_unknown) {
            add_bool(schema, "additionalProperties", false);
        }

        add_val(defs, name, schema);
        has_defs = true;
    }

    // --- Variant ---

    val_t make_variant(const type_info* ti) {
        auto* vi = static_cast<const variant_type_info*>(ti);
        auto* obj = doc.unchecked_make_object();
        auto* one_of = doc.unchecked_make_array();

        switch(vi->tagging) {
            case tag_mode::none:
                for(const auto* alt: vi->alternatives) {
                    doc.unchecked_arr_add_val(one_of, build_schema(alt));
                }
                break;

            case tag_mode::external:
                for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
                    auto alt_name = i < vi->alt_names.size()
                                        ? std::string(vi->alt_names[i])
                                        : normalize_name(vi->alternatives[i]->type_name);
                    auto* wrapper = doc.unchecked_make_object();
                    add_str(wrapper, "type", "object");
                    auto* p = doc.unchecked_make_object();
                    add_val(p, alt_name, build_schema(vi->alternatives[i]));
                    add_val(wrapper, "properties", p);
                    auto* req = doc.unchecked_make_array();
                    doc.unchecked_arr_add_val(req, doc.unchecked_make_str(alt_name));
                    add_val(wrapper, "required", req);
                    add_bool(wrapper, "additionalProperties", false);
                    doc.unchecked_arr_add_val(one_of, wrapper);
                }
                break;

            case tag_mode::internal:
                for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
                    auto alt_name = i < vi->alt_names.size()
                                        ? std::string(vi->alt_names[i])
                                        : normalize_name(vi->alternatives[i]->type_name);
                    auto* alt_schema = build_schema(vi->alternatives[i]);
                    auto* tag_obj = doc.unchecked_make_object();
                    auto* tag_props = doc.unchecked_make_object();
                    auto* const_obj = doc.unchecked_make_object();
                    add_str(const_obj, "const", alt_name);
                    add_val(tag_props, vi->tag_field, const_obj);
                    add_val(tag_obj, "properties", tag_props);
                    auto* all_of = doc.unchecked_make_array();
                    doc.unchecked_arr_add_val(all_of, alt_schema);
                    doc.unchecked_arr_add_val(all_of, tag_obj);
                    auto* combo = doc.unchecked_make_object();
                    add_val(combo, "allOf", all_of);
                    doc.unchecked_arr_add_val(one_of, combo);
                }
                break;

            case tag_mode::adjacent:
                for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
                    auto alt_name = i < vi->alt_names.size()
                                        ? std::string(vi->alt_names[i])
                                        : normalize_name(vi->alternatives[i]->type_name);
                    auto* wrapper = doc.unchecked_make_object();
                    add_str(wrapper, "type", "object");
                    auto* p = doc.unchecked_make_object();
                    auto* const_obj = doc.unchecked_make_object();
                    add_str(const_obj, "const", alt_name);
                    add_val(p, vi->tag_field, const_obj);
                    add_val(p, vi->content_field, build_schema(vi->alternatives[i]));
                    add_val(wrapper, "properties", p);
                    auto* req = doc.unchecked_make_array();
                    doc.unchecked_arr_add_val(req, doc.unchecked_make_str(vi->tag_field));
                    doc.unchecked_arr_add_val(req, doc.unchecked_make_str(vi->content_field));
                    add_val(wrapper, "required", req);
                    add_bool(wrapper, "additionalProperties", false);
                    doc.unchecked_arr_add_val(one_of, wrapper);
                }
                break;
        }

        add_val(obj, "oneOf", one_of);
        return obj;
    }

    // --- Helpers ---

    void add_str(val_t obj, std::string_view key, std::string_view value) {
        doc.unchecked_obj_add(obj, doc.unchecked_make_str(key), doc.unchecked_make_str(value));
    }

    void add_int(val_t obj, std::string_view key, std::int64_t value) {
        doc.unchecked_obj_add(obj, doc.unchecked_make_str(key), doc.unchecked_make_int(value));
    }

    void add_uint(val_t obj, std::string_view key, std::uint64_t value) {
        doc.unchecked_obj_add(obj, doc.unchecked_make_str(key), doc.unchecked_make_uint(value));
    }

    void add_bool(val_t obj, std::string_view key, bool value) {
        doc.unchecked_obj_add(obj, doc.unchecked_make_str(key), doc.unchecked_make_bool(value));
    }

    void add_val(val_t obj, std::string_view key, val_t value) {
        doc.unchecked_obj_add(obj, doc.unchecked_make_str(key), value);
    }

    /// Copy all key-value pairs from src into dst.
    void merge_into(val_t dst, val_t src) {
        yyjson_mut_obj_iter iter;
        yyjson_mut_obj_iter_init(src, &iter);
        yyjson_mut_val* key;
        while((key = yyjson_mut_obj_iter_next(&iter)) != nullptr) {
            auto* val = yyjson_mut_obj_iter_get_val(key);
            yyjson_mut_obj_add(dst, key, val);
        }
    }

    content::Document doc;
    val_t defs = nullptr;
    bool has_defs = false;
    std::set<std::string> visited;
};

inline std::string render(const type_info* root) {
    return emitter{}.emit(root);
}

}  // namespace eventide::serde::schema::codegen::json_schema
