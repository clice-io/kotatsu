#pragma once

// Runtime FlatBuffers schema (.fbs) emitter — drives emission purely from
// the `kota::meta::type_info` tree, with zero template parameters at the
// call site.  Used by the schema-driven flatbuffers codec.

#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

#include "kota/meta/type_info.h"

namespace kota::codec::flatbuffers::fbs {

inline std::string normalize_identifier(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    for(char c: text) {
        const auto u = static_cast<unsigned char>(c);
        out.push_back(std::isalnum(u) ? c : '_');
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
    using tk = meta::type_kind;
    using type_info = meta::type_info;

public:
    auto emit(const type_info& root) -> std::string {
        emit_object(root);
        out_ += "root_type " + normalize_identifier(root.type_name) + ";\n";
        return std::move(out_);
    }

private:
    static auto unwrap(const type_info& ti) -> const type_info& {
        const type_info* cur = &ti;
        while(cur->kind == tk::optional || cur->kind == tk::pointer) {
            cur = &static_cast<const meta::optional_type_info*>(cur)->inner();
        }
        return *cur;
    }

    static auto scalar_fbs_name(tk k) -> std::string {
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

    static auto is_fbs_struct(const type_info& ti) -> bool {
        if(ti.kind != tk::structure) {
            return false;
        }
        const auto& si = static_cast<const meta::struct_type_info&>(ti);
        if(!si.is_trivial_layout) {
            return false;
        }
        for(const auto& f: si.fields) {
            const auto& ft = unwrap(f.type());
            if(ft.is_scalar() || ft.kind == tk::enumeration) {
                continue;
            }
            if(!is_fbs_struct(ft)) {
                return false;
            }
        }
        return true;
    }

    auto wire_fbs_name(const type_info& raw) -> std::string {
        const auto& ti = unwrap(raw);
        switch(ti.kind) {
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
            case tk::character: return scalar_fbs_name(ti.kind);
            case tk::enumeration:
            case tk::structure: return normalize_identifier(ti.type_name);
            case tk::string: return "string";
            case tk::array:
            case tk::set: {
                const auto& ai = static_cast<const meta::array_type_info&>(ti);
                return "[" + wire_fbs_name(ai.element()) + "]";
            }
            default: return "string";
        }
    }

    void emit_deps(const type_info& raw) {
        const auto& ti = unwrap(raw);
        switch(ti.kind) {
            case tk::enumeration: emit_enum(ti); break;
            case tk::array:
            case tk::set: emit_deps(static_cast<const meta::array_type_info&>(ti).element()); break;
            case tk::map: {
                const auto& mi = static_cast<const meta::map_type_info&>(ti);
                emit_deps(mi.key());
                emit_deps(mi.value());
                break;
            }
            case tk::structure: emit_object(ti); break;
            default: break;
        }
    }

    static auto read_enum_value(const meta::enum_type_info& ei, std::size_t i) -> std::int64_t {
        switch(ei.underlying_kind) {
            case tk::boolean: return static_cast<const bool*>(ei.member_values)[i] ? 1 : 0;
            case tk::int8: return static_cast<const std::int8_t*>(ei.member_values)[i];
            case tk::int16: return static_cast<const std::int16_t*>(ei.member_values)[i];
            case tk::int32: return static_cast<const std::int32_t*>(ei.member_values)[i];
            case tk::int64: return static_cast<const std::int64_t*>(ei.member_values)[i];
            case tk::uint8:
                return static_cast<std::int64_t>(
                    static_cast<const std::uint8_t*>(ei.member_values)[i]);
            case tk::uint16:
                return static_cast<std::int64_t>(
                    static_cast<const std::uint16_t*>(ei.member_values)[i]);
            case tk::uint32:
                return static_cast<std::int64_t>(
                    static_cast<const std::uint32_t*>(ei.member_values)[i]);
            case tk::uint64:
                // May truncate for huge values; FBS can only encode signed 64-bit literals.
                return static_cast<std::int64_t>(
                    static_cast<const std::uint64_t*>(ei.member_values)[i]);
            default: return 0;
        }
    }

    void emit_enum(const type_info& ti) {
        auto name = normalize_identifier(ti.type_name);
        if(!emitted_enums_.insert(name).second) {
            return;
        }

        const auto& ei = static_cast<const meta::enum_type_info&>(ti);
        out_ += "enum " + name + ":" + scalar_fbs_name(ei.underlying_kind) + " {\n";
        for(std::size_t i = 0; i < ei.member_names.size(); ++i) {
            out_ += "  " + normalize_identifier(ei.member_names[i]);
            out_ += " = " + std::to_string(read_enum_value(ei, i));
            out_ += (i + 1 < ei.member_names.size()) ? ",\n" : "\n";
        }
        out_ += "}\n\n";
    }

    void emit_object(const type_info& ti) {
        auto object_name = normalize_identifier(ti.type_name);
        if(!emitted_objects_.insert(object_name).second) {
            return;
        }

        const auto& si = static_cast<const meta::struct_type_info&>(ti);
        const auto& fields = si.fields;

        // Phase 1 — emit dependencies first (enums, nested structs/tables).
        for(const auto& f: fields) {
            emit_deps(f.type());
        }

        // Phase 2 — synthesize map-entry tables per map-typed field.
        for(const auto& f: fields) {
            const auto& ft = unwrap(f.type());
            if(ft.kind != tk::map) {
                continue;
            }
            const auto& mi = static_cast<const meta::map_type_info&>(ft);
            auto entry_name = map_entry_identifier(object_name, f.name);
            if(!emitted_entries_.insert(entry_name).second) {
                continue;
            }
            emit_deps(mi.key());
            emit_deps(mi.value());
            out_ += "table " + entry_name + " {\n";
            out_ += "  key:" + wire_fbs_name(mi.key()) + " (key);\n";
            out_ += "  value:" + wire_fbs_name(mi.value()) + ";\n";
            out_ += "}\n\n";
        }

        // Phase 3 — emit the object definition (struct if inline-eligible).
        out_ += (is_fbs_struct(ti) ? "struct " : "table ");
        out_ += object_name + " {\n";
        for(const auto& f: fields) {
            auto fname = normalize_identifier(f.name);
            const auto& ft = unwrap(f.type());
            if(ft.kind == tk::map) {
                auto entry_name = map_entry_identifier(object_name, f.name);
                out_ += "  " + fname + ":[" + entry_name + "];\n";
            } else {
                out_ += "  " + fname + ":" + wire_fbs_name(f.type()) + ";\n";
            }
        }
        out_ += "}\n\n";
    }

    std::string out_;
    std::set<std::string> emitted_objects_;
    std::set<std::string> emitted_enums_;
    std::set<std::string> emitted_entries_;
};

inline auto render(const meta::type_info& root) -> std::string {
    return emitter{}.emit(root);
}

template <typename T, typename Config = meta::default_config>
inline auto render() -> std::string {
    return render(meta::type_info_of<T, Config>());
}

}  // namespace kota::codec::flatbuffers::fbs
