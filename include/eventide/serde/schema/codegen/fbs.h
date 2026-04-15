#pragma once

#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

#include "eventide/serde/schema/field_info.h"

namespace eventide::serde::schema::codegen::fbs {

inline std::string normalize_identifier(std::string_view text) {
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

inline std::string map_entry_identifier(std::string_view owner_name, std::string_view field_name) {
    return normalize_identifier(std::string(owner_name) + "_" + std::string(field_name) + "Entry");
}

class emitter {
    using tk = type_kind;

public:
    std::string emit(const type_info* root) {
        if(root == nullptr || root->kind != tk::structure) {
            return {};
        }
        root = unwrap(root);
        emit_object(root);
        out += "root_type " + normalize_identifier(root->type_name) + ";\n";
        return std::move(out);
    }

private:
    const static type_info* unwrap(const type_info* ti) {
        while(ti->kind == tk::optional || ti->kind == tk::pointer) {
            ti = static_cast<const optional_type_info*>(ti)->inner;
        }
        return ti;
    }

    static std::string scalar_fbs_name(tk k) {
        switch(k) {
            case tk::boolean: return "bool";
            case tk::int8: return "byte";
            case tk::uint8: return "ubyte";
            case tk::int16: return "short";
            case tk::uint16: return "ushort";
            case tk::int32: return "int";
            case tk::uint32: return "uint";
            case tk::int64: return "long";
            case tk::uint64: return "ulong";
            case tk::float32: return "float";
            case tk::float64: return "double";
            case tk::character: return "byte";
            default: return "int";
        }
    }

    static bool is_fbs_struct(const type_info* ti) {
        if(ti->kind != tk::structure) {
            return false;
        }
        auto* si = static_cast<const struct_type_info*>(ti);
        if(!si->is_trivial_layout) {
            return false;
        }
        for(const auto& field: si->fields) {
            auto* ft = unwrap(field.type);
            if(ft->kind == tk::boolean || ft->kind == tk::int8 || ft->kind == tk::int16 ||
               ft->kind == tk::int32 || ft->kind == tk::int64 || ft->kind == tk::uint8 ||
               ft->kind == tk::uint16 || ft->kind == tk::uint32 || ft->kind == tk::uint64 ||
               ft->kind == tk::float32 || ft->kind == tk::float64 ||
               ft->kind == tk::character || ft->kind == tk::enumeration) {
                continue;
            }
            if(!is_fbs_struct(ft)) {
                return false;
            }
        }
        return true;
    }

    std::string wire_fbs_name(const type_info* ti) {
        ti = unwrap(ti);
        switch(ti->kind) {
            case tk::boolean:
            case tk::int8:
            case tk::int16:
            case tk::int32:
            case tk::int64:
            case tk::uint8:
            case tk::uint16:
            case tk::uint32:
            case tk::uint64:
            case tk::float32:
            case tk::float64:
            case tk::character: return scalar_fbs_name(ti->kind);
            case tk::bytes: return "[ubyte]";
            case tk::enumeration:
            case tk::structure: return normalize_identifier(ti->type_name);
            case tk::string: return "string";
            case tk::array:
            case tk::set: {
                auto* ai = static_cast<const array_type_info*>(ti);
                auto* element = unwrap(ai->element);
                if(element->kind == tk::array || element->kind == tk::set) {
                    return "string";
                }
                return "[" + wire_fbs_name(ai->element) + "]";
            }
            default: return "string";
        }
    }

    void emit_deps(const type_info* ti) {
        ti = unwrap(ti);
        switch(ti->kind) {
            case tk::enumeration: emit_enum(ti); break;
            case tk::array:
            case tk::set: {
                auto* ai = static_cast<const array_type_info*>(ti);
                emit_deps(ai->element);
                break;
            }
            case tk::map: {
                auto* mi = static_cast<const map_type_info*>(ti);
                emit_deps(mi->key);
                emit_deps(mi->value);
                break;
            }
            case tk::structure: emit_object(ti); break;
            default: break;
        }
    }

    void emit_enum(const type_info* ti) {
        auto name = normalize_identifier(ti->type_name);
        if(!emitted_enums.insert(name).second) {
            return;
        }

        auto* ei = static_cast<const enum_type_info*>(ti);
        out += "enum " + name + ":" + scalar_fbs_name(ei->underlying_kind) + " {\n";
        for(std::size_t i = 0; i < ei->member_names.size(); ++i) {
            out += "  " + normalize_identifier(ei->member_names[i]);
            if(ei->underlying_kind == tk::uint64) {
                out += " = " + std::to_string(ei->member_u64_values[i]);
            } else {
                out += " = " + std::to_string(ei->member_values[i]);
            }
            out += (i + 1 < ei->member_names.size()) ? ",\n" : "\n";
        }
        out += "}\n\n";
    }

    void emit_object(const type_info* ti) {
        auto object_name = normalize_identifier(ti->type_name);
        if(!emitted_objects.insert(object_name).second) {
            return;
        }

        auto* si = static_cast<const struct_type_info*>(ti);
        auto fields = si->fields;

        // Phase 1: emit dependencies (enums, nested structs/tables)
        for(const auto& f: fields) {
            emit_deps(f.type);
        }

        // Phase 2: emit map entry tables
        for(const auto& f: fields) {
            auto* ft = unwrap(f.type);
            if(ft->kind == tk::map) {
                auto* mi = static_cast<const map_type_info*>(ft);
                auto entry_name = map_entry_identifier(object_name, f.name);
                if(!emitted_entries.insert(entry_name).second) {
                    continue;
                }
                emit_deps(mi->key);
                emit_deps(mi->value);
                out += "table " + entry_name + " {\n";
                out += "  key:" + wire_fbs_name(mi->key) + " (key);\n";
                out += "  value:" + wire_fbs_name(mi->value) + ";\n";
                out += "}\n\n";
            }
        }

        // Phase 3: emit the object definition
        out += (is_fbs_struct(ti) ? "struct " : "table ");
        out += object_name + " {\n";
        for(const auto& f: fields) {
            auto fname = normalize_identifier(f.name);
            auto* ft = unwrap(f.type);
            if(ft->kind == tk::map) {
                auto entry_name = map_entry_identifier(object_name, f.name);
                out += "  " + fname + ":[" + entry_name + "];\n";
            } else {
                out += "  " + fname + ":" + wire_fbs_name(f.type) + ";\n";
            }
        }
        out += "}\n\n";
    }

    std::string out;
    std::set<std::string> emitted_objects;
    std::set<std::string> emitted_enums;
    std::set<std::string> emitted_entries;
};

inline std::string render(const type_info* root) {
    return emitter{}.emit(root);
}

}  // namespace eventide::serde::schema::codegen::fbs
