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

#include "simdjson.h"

#include "kota/meta/type_info.h"
#include "kota/support/naming.h"

namespace kota::codec::json::schema {

class emitter {
    using tk = meta::type_kind;
    using string_builder = simdjson::builder::string_builder;

public:
    std::string emit(const meta::type_info& root) {
        root_ti = unwrap(&root);
        if(root_ti->kind == tk::structure) {
            used_names.insert(kota::naming::normalize_identifier(root_ti->type_name));
        }

        builder.start_object();
        key("$schema");
        builder.escape_and_append_with_quotes("https://json-schema.org/draft/2020-12/schema");

        write_schema_fields(&root);

        if(!defs.empty()) {
            builder.append_comma();
            key("$defs");
            builder.start_object();
            for(std::size_t i = 0; i < defs.size(); ++i) {
                if(i > 0) {
                    builder.append_comma();
                }
                key(defs[i].first);
                builder.append_raw(defs[i].second);
            }
            builder.end_object();
        }

        builder.end_object();

        std::string_view view_result{};
        [[maybe_unused]] auto ec = builder.view().get(view_result);
        return std::string(view_result);
    }

private:
    static const meta::type_info* unwrap(const meta::type_info* ti) {
        while(ti->kind == tk::optional || ti->kind == tk::pointer) {
            ti = &static_cast<const meta::optional_type_info*>(ti)->inner();
        }
        return ti;
    }

    void key(std::string_view k) {
        builder.escape_and_append_with_quotes(k);
        builder.append_colon();
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

    template <typename F>
    std::string render_fragment(F&& f) {
        string_builder saved;
        std::swap(builder, saved);
        f();
        std::string_view view_result{};
        [[maybe_unused]] auto ec = builder.view().get(view_result);
        std::string fragment(view_result);
        std::swap(builder, saved);
        return fragment;
    }

    void write_schema(const meta::type_info* ti) {
        ti = unwrap(ti);

        switch(ti->kind) {
            case tk::null: write_type("null"); return;
            case tk::boolean: write_type("boolean"); return;
            case tk::character:
            case tk::string:
            case tk::bytes: write_type("string"); return;
            case tk::float32:
            case tk::float64: write_type("number"); return;
            case tk::int8:
                write_integer(std::numeric_limits<std::int8_t>::min(),
                              std::numeric_limits<std::int8_t>::max());
                return;
            case tk::int16:
                write_integer(std::numeric_limits<std::int16_t>::min(),
                              std::numeric_limits<std::int16_t>::max());
                return;
            case tk::int32:
                write_integer(std::numeric_limits<std::int32_t>::min(),
                              std::numeric_limits<std::int32_t>::max());
                return;
            case tk::int64:
                write_integer(std::numeric_limits<std::int64_t>::min(),
                              std::numeric_limits<std::int64_t>::max());
                return;
            case tk::uint8: write_unsigned(std::numeric_limits<std::uint8_t>::max()); return;
            case tk::uint16: write_unsigned(std::numeric_limits<std::uint16_t>::max()); return;
            case tk::uint32: write_unsigned(std::numeric_limits<std::uint32_t>::max()); return;
            case tk::uint64: write_unsigned(std::numeric_limits<std::uint64_t>::max()); return;
            case tk::enumeration: write_enum(ti); return;
            case tk::array:
            case tk::set: write_array(ti); return;
            case tk::map: write_map(ti); return;
            case tk::tuple: write_tuple(ti); return;
            case tk::structure: write_struct_ref(ti); return;
            case tk::variant: write_variant(ti); return;
            default:
                builder.start_object();
                builder.end_object();
                return;
        }
    }

    void write_schema_fields(const meta::type_info* ti) {
        ti = unwrap(ti);
        if(ti->kind == tk::structure) {
            builder.append_comma();
            write_struct_body(static_cast<const meta::struct_type_info*>(ti));
            return;
        }
        auto fragment = render_fragment([&] { write_schema(ti); });
        if(fragment.size() > 2) {
            builder.append_comma();
            builder.append_raw(std::string_view(fragment).substr(1, fragment.size() - 2));
        }
    }

    void write_struct_body(const meta::struct_type_info* si) {
        key("type");
        builder.escape_and_append_with_quotes("object");
        builder.append_comma();
        key("properties");
        write_properties(si);
        write_required(si);
        if(si->deny_unknown) {
            builder.append_comma();
            key("additionalProperties");
            builder.append_raw("false");
        }
    }

    void write_type(std::string_view type_name) {
        builder.start_object();
        key("type");
        builder.escape_and_append_with_quotes(type_name);
        builder.end_object();
    }

    void write_integer(std::int64_t min_val, std::int64_t max_val) {
        builder.start_object();
        key("type");
        builder.escape_and_append_with_quotes("integer");
        builder.append_comma();
        key("minimum");
        builder.append(min_val);
        builder.append_comma();
        key("maximum");
        builder.append(max_val);
        builder.end_object();
    }

    void write_unsigned(std::uint64_t max_val) {
        builder.start_object();
        key("type");
        builder.escape_and_append_with_quotes("integer");
        builder.append_comma();
        key("minimum");
        builder.append(std::uint64_t{0});
        builder.append_comma();
        key("maximum");
        builder.append(max_val);
        builder.end_object();
    }

    void write_enum(const meta::type_info* ti) {
        auto* ei = static_cast<const meta::enum_type_info*>(ti);
        builder.start_object();
        key("enum");
        builder.start_array();
        for(std::size_t i = 0; i < ei->member_names.size(); ++i) {
            if(i > 0) {
                builder.append_comma();
            }
            builder.escape_and_append_with_quotes(ei->member_names[i]);
        }
        builder.end_array();
        builder.end_object();
    }

    void write_array(const meta::type_info* ti) {
        auto* ai = static_cast<const meta::array_type_info*>(ti);
        builder.start_object();
        key("type");
        builder.escape_and_append_with_quotes("array");
        builder.append_comma();
        key("items");
        write_schema(&ai->element());
        if(ti->kind == tk::set) {
            builder.append_comma();
            key("uniqueItems");
            builder.append_raw("true");
        }
        builder.end_object();
    }

    void write_map(const meta::type_info* ti) {
        auto* mi = static_cast<const meta::map_type_info*>(ti);
        builder.start_object();
        key("type");
        builder.escape_and_append_with_quotes("object");
        builder.append_comma();
        key("additionalProperties");
        write_schema(&mi->value());
        builder.end_object();
    }

    void write_tuple(const meta::type_info* ti) {
        auto* tup = static_cast<const meta::tuple_type_info*>(ti);
        builder.start_object();
        key("type");
        builder.escape_and_append_with_quotes("array");
        builder.append_comma();
        key("prefixItems");
        builder.start_array();
        for(std::size_t i = 0; i < tup->elements.size(); ++i) {
            if(i > 0) {
                builder.append_comma();
            }
            write_schema(&tup->elements[i]());
        }
        builder.end_array();
        builder.end_object();
    }

    void write_struct_ref(const meta::type_info* ti) {
        if(ti == root_ti) {
            builder.start_object();
            key("$ref");
            builder.escape_and_append_with_quotes("#");
            builder.end_object();
            return;
        }
        const auto& name = canonical_name(ti);
        ensure_struct_def(ti);
        builder.start_object();
        key("$ref");
        builder.escape_and_append_with_quotes(std::format("#/$defs/{}", name));
        builder.end_object();
    }

    void ensure_struct_def(const meta::type_info* ti) {
        if(!emitted.insert(ti).second) {
            return;
        }
        auto* si = static_cast<const meta::struct_type_info*>(ti);
        auto body = render_fragment([&] {
            builder.start_object();
            write_struct_body(si);
            builder.end_object();
        });
        defs.emplace_back(canonical_name(ti), std::move(body));
    }

    void write_properties(const meta::struct_type_info* si) {
        builder.start_object();
        for(std::size_t i = 0; i < si->fields.size(); ++i) {
            if(i > 0) {
                builder.append_comma();
            }
            key(si->fields[i].name);
            write_schema(&si->fields[i].type());
        }
        builder.end_object();
    }

    void write_required(const meta::struct_type_info* si) {
        bool first = true;
        for(const auto& f: si->fields) {
            const meta::type_info& ft = f.type();
            bool is_optional = f.has_default || ft.kind == tk::optional || ft.kind == tk::pointer;
            if(is_optional) {
                continue;
            }
            if(first) {
                builder.append_comma();
                key("required");
                builder.start_array();
                first = false;
            } else {
                builder.append_comma();
            }
            builder.escape_and_append_with_quotes(f.name);
        }
        if(!first) {
            builder.end_array();
        }
    }

    void write_tag_const(std::string_view tag_field, std::string_view alt_name) {
        builder.start_object();
        key("properties");
        builder.start_object();
        key(tag_field);
        builder.start_object();
        key("const");
        builder.escape_and_append_with_quotes(alt_name);
        builder.end_object();
        builder.end_object();
        builder.append_comma();
        key("required");
        builder.start_array();
        builder.escape_and_append_with_quotes(tag_field);
        builder.end_array();
        builder.end_object();
    }

    void write_variant(const meta::type_info* ti) {
        auto* vi = static_cast<const meta::variant_type_info*>(ti);
        builder.start_object();
        key("oneOf");
        builder.start_array();

        for(std::size_t i = 0; i < vi->alternatives.size(); ++i) {
            if(i > 0) {
                builder.append_comma();
            }

            switch(vi->tagging) {
                case meta::tag_mode::none: write_schema(&vi->alternatives[i]()); break;

                case meta::tag_mode::external: {
                    auto alt_name = alternative_name(vi, i);
                    builder.start_object();
                    key("type");
                    builder.escape_and_append_with_quotes("object");
                    builder.append_comma();
                    key("properties");
                    builder.start_object();
                    key(alt_name);
                    write_schema(&vi->alternatives[i]());
                    builder.end_object();
                    builder.append_comma();
                    key("required");
                    builder.start_array();
                    builder.escape_and_append_with_quotes(alt_name);
                    builder.end_array();
                    builder.append_comma();
                    key("additionalProperties");
                    builder.append_raw("false");
                    builder.end_object();
                    break;
                }

                case meta::tag_mode::internal: {
                    auto alt_name = alternative_name(vi, i);
                    builder.start_object();
                    key("allOf");
                    builder.start_array();
                    write_schema(&vi->alternatives[i]());
                    builder.append_comma();
                    write_tag_const(vi->tag_field, alt_name);
                    builder.end_array();
                    builder.end_object();
                    break;
                }

                case meta::tag_mode::adjacent: {
                    auto alt_name = alternative_name(vi, i);
                    builder.start_object();
                    key("type");
                    builder.escape_and_append_with_quotes("object");
                    builder.append_comma();
                    key("properties");
                    builder.start_object();
                    key(vi->tag_field);
                    builder.start_object();
                    key("const");
                    builder.escape_and_append_with_quotes(alt_name);
                    builder.end_object();
                    builder.append_comma();
                    key(vi->content_field);
                    write_schema(&vi->alternatives[i]());
                    builder.end_object();
                    builder.append_comma();
                    key("required");
                    builder.start_array();
                    builder.escape_and_append_with_quotes(vi->tag_field);
                    builder.append_comma();
                    builder.escape_and_append_with_quotes(vi->content_field);
                    builder.end_array();
                    builder.append_comma();
                    key("additionalProperties");
                    builder.append_raw("false");
                    builder.end_object();
                    break;
                }
            }
        }

        builder.end_array();
        builder.end_object();
    }

    string_builder builder;
    std::vector<std::pair<std::string, std::string>> defs;
    std::unordered_map<const meta::type_info*, std::string> def_names;
    std::unordered_set<std::string> used_names;
    std::unordered_set<const meta::type_info*> emitted;
    const meta::type_info* root_ti = nullptr;
};

inline std::string render(const meta::type_info& root) {
    return emitter{}.emit(root);
}

}  // namespace kota::codec::json::schema
