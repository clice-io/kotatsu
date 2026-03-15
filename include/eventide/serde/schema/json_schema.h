#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>

#include "eventide/serde/schema/descriptor.h"
#include "eventide/serde/schema/kind.h"

namespace eventide::serde::schema {

namespace detail {

inline std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for(char c: s) {
        switch(c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out;
}

inline std::string make_ref(std::string_view type_name) {
    return "{\"$ref\":\"#/$defs/" + json_escape(type_name) + "\"}";
}

class json_schema_builder {
public:
    std::string build(const type_schema_view* root) {
        std::string root_schema = emit(root);

        if(defs_.empty()) {
            return root_schema;
        }

        std::string result = "{";
        auto pos = root_schema.find('{');
        if(pos != std::string::npos && root_schema.size() > 2) {
            result += root_schema.substr(pos + 1, root_schema.size() - pos - 2);
        }
        result += ",\"$defs\":{";
        bool first = true;
        for(auto& [name, schema]: defs_) {
            if(!first) result += ',';
            first = false;
            result += "\"" + json_escape(name) + "\":" + schema;
        }
        result += "}}";
        return result;
    }

private:
    std::set<std::string> visiting_;
    std::map<std::string, std::string> defs_;

    std::string emit(const type_schema_view* sv) {
        if(!sv) return "{}";

        bool is_named_composite =
            (sv->kind == type_kind::structure || sv->kind == type_kind::variant) &&
            !sv->type_name.empty();

        if(is_named_composite) {
            std::string name(sv->type_name);
            if(defs_.contains(name)) {
                return make_ref(sv->type_name);
            }
            if(visiting_.contains(name)) {
                return make_ref(sv->type_name);
            }
            visiting_.insert(name);
            std::string schema = emit_inline(sv);
            visiting_.erase(name);
            defs_[name] = schema;
            return make_ref(sv->type_name);
        }

        return emit_inline(sv);
    }

    std::string emit_inline(const type_schema_view* sv) {
        switch(sv->kind) {
        case type_kind::null_like: return "{\"type\":\"null\"}";
        case type_kind::boolean: return "{\"type\":\"boolean\"}";
        case type_kind::integer:
        case type_kind::enumeration: return "{\"type\":\"integer\"}";
        case type_kind::floating: return "{\"type\":\"number\"}";
        case type_kind::string:
        case type_kind::character: return "{\"type\":\"string\"}";
        case type_kind::bytes:
            return "{\"type\":\"string\",\"contentEncoding\":\"base64\"}";
        case type_kind::array:
        case type_kind::set: return emit_array(sv);
        case type_kind::map: return emit_map(sv);
        case type_kind::tuple: return emit_tuple(sv);
        case type_kind::structure: return emit_structure(sv);
        case type_kind::variant: return emit_variant(sv);
        case type_kind::optional:
        case type_kind::pointer:
            if(sv->element) return emit(sv->element);
            return "{}";
        case type_kind::any: return "{}";
        }
        return "{}";
    }

    std::string emit_array(const type_schema_view* sv) {
        std::string result = "{\"type\":\"array\"";
        if(sv->element) {
            result += ",\"items\":" + emit(sv->element);
        }
        if(sv->kind == type_kind::set) {
            result += ",\"uniqueItems\":true";
        }
        result += "}";
        return result;
    }

    std::string emit_map(const type_schema_view* sv) {
        std::string result = "{\"type\":\"object\"";
        if(sv->value) {
            result += ",\"additionalProperties\":" + emit(sv->value);
        }
        result += "}";
        return result;
    }

    std::string emit_tuple(const type_schema_view* sv) {
        std::string result = "{\"type\":\"array\",\"prefixItems\":[";
        for(std::size_t i = 0; i < sv->tuple_elements.size(); ++i) {
            if(i > 0) result += ',';
            result += emit(sv->tuple_elements[i]);
        }
        result += "],\"items\":false}";
        return result;
    }

    std::string emit_structure(const type_schema_view* sv) {
        std::string result = "{\"type\":\"object\",\"properties\":{";
        bool first = true;
        for(const auto& field: sv->fields) {
            if(!first) result += ',';
            first = false;
            result += "\"" + json_escape(field.wire_name) + "\":";
            if(field.nested) {
                result += emit(field.nested);
            } else {
                result += emit_kind_only(field.kind);
            }
        }
        result += "}";

        std::string required;
        for(const auto& field: sv->fields) {
            if(field.required) {
                if(!required.empty()) required += ',';
                required += "\"" + json_escape(field.wire_name) + "\"";
            }
        }
        if(!required.empty()) {
            result += ",\"required\":[" + required + "]";
        }

        result += ",\"additionalProperties\":false}";
        return result;
    }

    std::string emit_variant(const type_schema_view* sv) {
        std::string result = "{\"oneOf\":[";
        for(std::size_t i = 0; i < sv->alternatives.size(); ++i) {
            if(i > 0) result += ',';
            result += emit(sv->alternatives[i]);
        }
        result += "]}";
        return result;
    }

    static std::string emit_kind_only(type_kind k) {
        switch(k) {
        case type_kind::null_like: return "{\"type\":\"null\"}";
        case type_kind::boolean: return "{\"type\":\"boolean\"}";
        case type_kind::integer:
        case type_kind::enumeration: return "{\"type\":\"integer\"}";
        case type_kind::floating: return "{\"type\":\"number\"}";
        case type_kind::string:
        case type_kind::character: return "{\"type\":\"string\"}";
        default: return "{}";
        }
    }
};

}  // namespace detail

template <typename T, typename Config = config::default_config>
std::string to_json_schema() {
    const auto* root = get_schema<T, Config>();
    detail::json_schema_builder builder;
    return builder.build(root);
}

}  // namespace eventide::serde::schema
