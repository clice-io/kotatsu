#pragma once

#include <cstdint>
#include <format>
#include <iterator>
#include <set>
#include <string>
#include <string_view>

#include "kota/meta/type_info.h"
#include "kota/support/naming.h"

namespace kota::codec::schema::fbs {

inline std::string map_entry_identifier(std::string_view owner, std::string_view field) {
    return kota::naming::normalize_identifier(std::format("{}_{}Entry", owner, field));
}

class emitter {
    using tk = meta::type_kind;

public:
    std::string emit(const meta::type_info& root) {
        if(root.kind != tk::structure) {
            return {};
        }
        const auto* ti = unwrap(&root);
        emit_object(ti);
        std::format_to(std::back_inserter(out),
                       "root_type {};\n",
                       kota::naming::normalize_identifier(ti->type_name));
        return std::move(out);
    }

private:
    const static meta::type_info* unwrap(const meta::type_info* ti) {
        while(ti->kind == tk::optional || ti->kind == tk::pointer) {
            ti = &static_cast<const meta::optional_type_info*>(ti)->inner();
        }
        return ti;
    }

    static std::string_view scalar_fbs_name(tk k) {
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

    static bool is_fbs_scalar(tk k) {
        switch(k) {
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
            case tk::character:
            case tk::enumeration: return true;
            default: return false;
        }
    }

    static bool is_fbs_struct(const meta::type_info* ti) {
        if(ti->kind != tk::structure) {
            return false;
        }
        auto* si = static_cast<const meta::struct_type_info*>(ti);
        if(!si->is_trivial_layout) {
            return false;
        }
        for(const auto& field: si->fields) {
            auto* ft = unwrap(&field.type());
            if(is_fbs_scalar(ft->kind)) {
                continue;
            }
            if(!is_fbs_struct(ft)) {
                return false;
            }
        }
        return true;
    }

    std::string wire_fbs_name(const meta::type_info* ti) {
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
            case tk::character: return std::string(scalar_fbs_name(ti->kind));
            case tk::bytes: return "[ubyte]";
            case tk::enumeration:
            case tk::structure: return kota::naming::normalize_identifier(ti->type_name);
            case tk::string: return "string";
            case tk::array:
            case tk::set: {
                auto* ai = static_cast<const meta::array_type_info*>(ti);
                auto* element = unwrap(&ai->element());
                if(element->kind == tk::array || element->kind == tk::set) {
                    return "string";
                }
                return std::format("[{}]", wire_fbs_name(&ai->element()));
            }
            default: return "string";
        }
    }

    void emit_deps(const meta::type_info* ti) {
        ti = unwrap(ti);
        switch(ti->kind) {
            case tk::enumeration: emit_enum(ti); break;
            case tk::array:
            case tk::set: {
                auto* ai = static_cast<const meta::array_type_info*>(ti);
                emit_deps(&ai->element());
                break;
            }
            case tk::map: {
                auto* mi = static_cast<const meta::map_type_info*>(ti);
                emit_deps(&mi->key());
                emit_deps(&mi->value());
                break;
            }
            case tk::structure: emit_object(ti); break;
            default: break;
        }
    }

    template <typename T>
    static std::string format_enum_element(const void* base, std::size_t i) {
        return std::format("{}", static_cast<const T*>(base)[i]);
    }

    static std::string format_enum_value(const meta::enum_type_info* ei, std::size_t i) {
        switch(ei->underlying_kind) {
            case tk::int8: return format_enum_element<std::int8_t>(ei->member_values, i);
            case tk::uint8: return format_enum_element<std::uint8_t>(ei->member_values, i);
            case tk::int16: return format_enum_element<std::int16_t>(ei->member_values, i);
            case tk::uint16: return format_enum_element<std::uint16_t>(ei->member_values, i);
            case tk::int32: return format_enum_element<std::int32_t>(ei->member_values, i);
            case tk::uint32: return format_enum_element<std::uint32_t>(ei->member_values, i);
            case tk::int64: return format_enum_element<std::int64_t>(ei->member_values, i);
            case tk::uint64: return format_enum_element<std::uint64_t>(ei->member_values, i);
            default: return "0";
        }
    }

    void emit_enum(const meta::type_info* ti) {
        auto name = kota::naming::normalize_identifier(ti->type_name);
        if(!emitted_enums.insert(name).second) {
            return;
        }
        auto* ei = static_cast<const meta::enum_type_info*>(ti);
        auto it = std::back_inserter(out);
        std::format_to(it, "enum {}:{} {{\n", name, scalar_fbs_name(ei->underlying_kind));
        for(std::size_t i = 0; i < ei->member_names.size(); ++i) {
            std::format_to(it,
                           "  {} = {}{}\n",
                           kota::naming::normalize_identifier(ei->member_names[i]),
                           format_enum_value(ei, i),
                           i + 1 < ei->member_names.size() ? "," : "");
        }
        out += "}\n\n";
    }

    void emit_object(const meta::type_info* ti) {
        auto object_name = kota::naming::normalize_identifier(ti->type_name);
        if(!emitted_objects.insert(object_name).second) {
            return;
        }

        auto* si = static_cast<const meta::struct_type_info*>(ti);
        const auto& fields = si->fields;

        for(const auto& f: fields) {
            emit_deps(&f.type());
        }

        for(const auto& f: fields) {
            auto* ft = unwrap(&f.type());
            if(ft->kind != tk::map) {
                continue;
            }
            auto* mi = static_cast<const meta::map_type_info*>(ft);
            auto entry_name = map_entry_identifier(object_name, f.name);
            if(!emitted_entries.insert(entry_name).second) {
                continue;
            }
            emit_deps(&mi->key());
            emit_deps(&mi->value());
            std::format_to(std::back_inserter(out),
                           "table {} {{\n  key:{} (key);\n  value:{};\n}}\n\n",
                           entry_name,
                           wire_fbs_name(&mi->key()),
                           wire_fbs_name(&mi->value()));
        }

        std::format_to(std::back_inserter(out),
                       "{} {} {{\n",
                       is_fbs_struct(ti) ? "struct" : "table",
                       object_name);
        for(const auto& f: fields) {
            auto fname = kota::naming::normalize_identifier(f.name);
            auto* ft = unwrap(&f.type());
            if(ft->kind == tk::map) {
                auto entry_name = map_entry_identifier(object_name, f.name);
                std::format_to(std::back_inserter(out), "  {}:[{}];\n", fname, entry_name);
            } else {
                std::format_to(std::back_inserter(out),
                               "  {}:{};\n",
                               fname,
                               wire_fbs_name(&f.type()));
            }
        }
        out += "}\n\n";
    }

    std::string out;
    std::set<std::string> emitted_objects;
    std::set<std::string> emitted_enums;
    std::set<std::string> emitted_entries;
};

inline std::string render(const meta::type_info& root) {
    return emitter{}.emit(root);
}

}  // namespace kota::codec::schema::fbs
