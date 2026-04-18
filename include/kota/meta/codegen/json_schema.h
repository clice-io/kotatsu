#pragma once

#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/meta/type_info.h"

namespace kota::meta::codegen::json_schema {

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
    std::string emit(const type_info& root) {
        out += '{';
        key("$schema");
        str("https://json-schema.org/draft/2020-12/schema");

        write_schema_fields(&root);

        if(!defs.empty()) {
            out += ',';
            key("$defs");
            out += '{';
            for(std::size_t i = 0; i < defs.size(); ++i) {
                if(i > 0) {
                    out += ',';
                }
                key(defs[i].first);
                out += defs[i].second;
            }
            out += '}';
        }

        out += '}';
        return std::move(out);
    }

private:
    const static type_info* unwrap(const type_info* ti) {
        while(ti->kind == tk::optional || ti->kind == tk::pointer) {
            ti = &static_cast<const optional_type_info*>(ti)->inner();
        }
        return ti;
    }

    static void escape_json(std::string& buf, std::string_view sv) {
        buf += '"';
        for(char c: sv) {
            switch(c) {
                case '"': buf += "\\\""; break;
                case '\\': buf += "\\\\"; break;
                case '\b': buf += "\\b"; break;
                case '\f': buf += "\\f"; break;
                case '\n': buf += "\\n"; break;
                case '\r': buf += "\\r"; break;
                case '\t': buf += "\\t"; break;
                default: buf += c; break;
            }
        }
        buf += '"';
    }

    void key(std::string_view k) {
        escape_json(out, k);
        out += ':';
    }

    void str(std::string_view s) {
        escape_json(out, s);
    }

    void write_schema(const type_info* ti) {
        ti = unwrap(ti);

        switch(ti->kind) {
            case tk::null: write_type("null"); return;
            case tk::boolean: write_type("boolean"); return;
            case tk::character:
            case tk::string:
            case tk::bytes: write_type("string"); return;
            case tk::float32:
            case tk::float64: write_type("number"); return;
            case tk::int8: write_integer(-128, 127); return;
            case tk::int16: write_integer(-32768, 32767); return;
            case tk::int32:
                write_integer(std::numeric_limits<std::int32_t>::min(),
                              std::numeric_limits<std::int32_t>::max());
                return;
            case tk::int64:
                write_integer(std::numeric_limits<std::int64_t>::min(),
                              std::numeric_limits<std::int64_t>::max());
                return;
            case tk::uint8: write_unsigned(255); return;
            case tk::uint16: write_unsigned(65535); return;
            case tk::uint32: write_unsigned(std::numeric_limits<std::uint32_t>::max()); return;
            case tk::uint64: write_unsigned(std::numeric_limits<std::uint64_t>::max()); return;
            case tk::enumeration: write_enum(ti); return;
            case tk::array:
            case tk::set: write_array(ti); return;
            case tk::map: write_map(ti); return;
            case tk::tuple: write_tuple(ti); return;
            case tk::structure: write_struct_ref(ti); return;
            case tk::variant: write_variant(ti); return;
            default: out += "{}"; return;
        }
    }

    void write_schema_fields(const type_info* ti) {
        ti = unwrap(ti);
        if(ti->kind != tk::structure) {
            auto saved = std::move(out);
            out.clear();
            write_schema(ti);
            auto fragment = std::move(out);
            out = std::move(saved);
            if(fragment.size() > 2) {
                out += ',';
                out.append(fragment, 1, fragment.size() - 2);
            }
            return;
        }
        auto* si = static_cast<const struct_type_info*>(ti);
        auto name = normalize_name(ti->type_name);
        visited.insert(name);

        out += ',';
        key("type");
        str("object");
        out += ',';
        key("properties");
        write_properties(si);
        write_required(si);
        if(si->deny_unknown) {
            out += ',';
            key("additionalProperties");
            out += "false";
        }
    }

    void write_type(std::string_view type_name) {
        out += '{';
        key("type");
        str(type_name);
        out += '}';
    }

    void write_integer(std::int64_t min_val, std::int64_t max_val) {
        out += '{';
        key("type");
        str("integer");
        out += ',';
        key("minimum");
        out += std::to_string(min_val);
        out += ',';
        key("maximum");
        out += std::to_string(max_val);
        out += '}';
    }

    void write_unsigned(std::uint64_t max_val) {
        out += '{';
        key("type");
        str("integer");
        out += ',';
        key("minimum");
        out += '0';
        out += ',';
        key("maximum");
        out += std::to_string(max_val);
        out += '}';
    }

    void write_enum(const type_info* ti) {
        auto* ei = static_cast<const enum_type_info*>(ti);
        out += '{';
        key("enum");
        out += '[';
        for(std::size_t i = 0; i < ei->member_names.size(); ++i) {
            if(i > 0) {
                out += ',';
            }
            str(ei->member_names[i]);
        }
        out += ']';
        out += '}';
    }

    void write_array(const type_info* ti) {
        auto* ai = static_cast<const array_type_info*>(ti);
        out += '{';
        key("type");
        str("array");
        out += ',';
        key("items");
        write_schema(&ai->element());
        if(ti->kind == tk::set) {
            out += ',';
            key("uniqueItems");
            out += "true";
        }
        out += '}';
    }

    void write_map(const type_info* ti) {
        auto* mi = static_cast<const map_type_info*>(ti);
        out += '{';
        key("type");
        str("object");
        out += ',';
        key("additionalProperties");
        write_schema(&mi->value());
        out += '}';
    }

    void write_tuple(const type_info* ti) {
        auto* tup = static_cast<const tuple_type_info*>(ti);
        out += '{';
        key("type");
        str("array");
        out += ',';
        key("prefixItems");
        out += '[';
        for(std::size_t i = 0; i < tup->elements.size(); ++i) {
            if(i > 0) {
                out += ',';
            }
            write_schema(&tup->elements[i]());
        }
        out += ']';
        out += '}';
    }

    void write_struct_ref(const type_info* ti) {
        auto name = normalize_name(ti->type_name);
        ensure_struct_def(ti, name);
        out += '{';
        key("$ref");
        str("#/$defs/" + name);
        out += '}';
    }

    void ensure_struct_def(const type_info* ti, const std::string& name) {
        if(!visited.insert(name).second) {
            return;
        }

        auto* si = static_cast<const struct_type_info*>(ti);

        auto saved = std::move(out);
        out.clear();

        out += '{';
        key("type");
        str("object");
        out += ',';
        key("properties");
        write_properties(si);
        write_required(si);
        if(si->deny_unknown) {
            out += ',';
            key("additionalProperties");
            out += "false";
        }
        out += '}';

        defs.emplace_back(name, std::move(out));
        out = std::move(saved);
    }

    void write_properties(const struct_type_info* si) {
        out += '{';
        for(std::size_t i = 0; i < si->fields.size(); ++i) {
            if(i > 0) {
                out += ',';
            }
            key(si->fields[i].name);
            write_schema(&si->fields[i].type());
        }
        out += '}';
    }

    void write_required(const struct_type_info* si) {
        bool first = true;
        for(const auto& f: si->fields) {
            const type_info& ft = f.type();
            bool is_optional = f.has_default || ft.kind == tk::optional || ft.kind == tk::pointer;
            if(!is_optional) {
                if(first) {
                    out += ',';
                    key("required");
                    out += '[';
                    first = false;
                } else {
                    out += ',';
                }
                str(f.name);
            }
        }
        if(!first) {
            out += ']';
        }
    }

    void write_variant(const type_info* ti) {
        auto* vi = static_cast<const variant_type_info*>(ti);
        out += '{';
        key("oneOf");
        out += '[';

        switch(vi->tagging) {
            case tag_mode::none:
                for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
                    if(i > 0) {
                        out += ',';
                    }
                    write_schema(&vi->alternatives[i]());
                }
                break;

            case tag_mode::external:
                for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
                    if(i > 0) {
                        out += ',';
                    }
                    auto alt_name = i < vi->alt_names.size()
                                        ? std::string(vi->alt_names[i])
                                        : normalize_name(vi->alternatives[i]().type_name);
                    out += '{';
                    key("type");
                    str("object");
                    out += ',';
                    key("properties");
                    out += '{';
                    key(alt_name);
                    write_schema(&vi->alternatives[i]());
                    out += '}';
                    out += ',';
                    key("required");
                    out += '[';
                    str(alt_name);
                    out += ']';
                    out += ',';
                    key("additionalProperties");
                    out += "false";
                    out += '}';
                }
                break;

            case tag_mode::internal:
                for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
                    if(i > 0) {
                        out += ',';
                    }
                    auto alt_name = i < vi->alt_names.size()
                                        ? std::string(vi->alt_names[i])
                                        : normalize_name(vi->alternatives[i]().type_name);
                    out += '{';
                    key("allOf");
                    out += '[';
                    write_schema(&vi->alternatives[i]());
                    out += ',';
                    out += '{';
                    key("properties");
                    out += '{';
                    key(vi->tag_field);
                    out += '{';
                    key("const");
                    str(alt_name);
                    out += '}';
                    out += '}';
                    out += ',';
                    key("required");
                    out += '[';
                    str(vi->tag_field);
                    out += ']';
                    out += '}';
                    out += ']';
                    out += '}';
                }
                break;

            case tag_mode::adjacent:
                for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
                    if(i > 0) {
                        out += ',';
                    }
                    auto alt_name = i < vi->alt_names.size()
                                        ? std::string(vi->alt_names[i])
                                        : normalize_name(vi->alternatives[i]().type_name);
                    out += '{';
                    key("type");
                    str("object");
                    out += ',';
                    key("properties");
                    out += '{';
                    key(vi->tag_field);
                    out += '{';
                    key("const");
                    str(alt_name);
                    out += '}';
                    out += ',';
                    key(vi->content_field);
                    write_schema(&vi->alternatives[i]());
                    out += '}';
                    out += ',';
                    key("required");
                    out += '[';
                    str(vi->tag_field);
                    out += ',';
                    str(vi->content_field);
                    out += ']';
                    out += ',';
                    key("additionalProperties");
                    out += "false";
                    out += '}';
                }
                break;
        }

        out += ']';
        out += '}';
    }

    std::string out;
    std::vector<std::pair<std::string, std::string>> defs;
    std::set<std::string> visited;
};

inline std::string render(const type_info& root) {
    return emitter{}.emit(root);
}

}  // namespace kota::meta::codegen::json_schema
