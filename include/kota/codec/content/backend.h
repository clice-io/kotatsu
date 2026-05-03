#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "kota/meta/type_kind.h"
#include "kota/codec/content/document.h"
#include "kota/codec/content/error.h"

namespace kota::codec::content {

struct content_backend {
    using value_type = const Value*;
    using error_type = error_kind;
    static constexpr error_type success = error_kind::ok;
    static constexpr error_type type_mismatch = error_kind::type_mismatch;
    static constexpr error_type number_out_of_range = error_kind::number_out_of_range;

    static error_type read_bool(value_type& v, bool& out) {
        if(!v)
            return type_mismatch;
        auto parsed = v->get_bool();
        if(!parsed)
            return type_mismatch;
        out = *parsed;
        return success;
    }

    static error_type read_int64(value_type& v, std::int64_t& out) {
        if(!v)
            return type_mismatch;
        auto parsed = v->get_int();
        if(!parsed)
            return type_mismatch;
        out = *parsed;
        return success;
    }

    static error_type read_uint64(value_type& v, std::uint64_t& out) {
        if(!v)
            return type_mismatch;
        auto parsed = v->get_uint();
        if(!parsed)
            return type_mismatch;
        out = *parsed;
        return success;
    }

    static error_type read_double(value_type& v, double& out) {
        if(!v)
            return type_mismatch;
        auto parsed = v->get_double();
        if(!parsed)
            return type_mismatch;
        out = *parsed;
        return success;
    }

    static error_type read_string(value_type& v, std::string_view& out) {
        if(!v)
            return type_mismatch;
        auto parsed = v->get_string();
        if(!parsed)
            return type_mismatch;
        out = *parsed;
        return success;
    }

    static error_type read_is_null(value_type& v, bool& is_null) {
        is_null = (!v || v->is_null());
        return success;
    }

    template <typename Visitor>
    static error_type visit_object(value_type& src, Visitor&& vis) {
        if(!src)
            return type_mismatch;
        const Object* obj = src->get_object();
        if(!obj)
            return type_mismatch;
        for(const auto& entry: *obj) {
            value_type val = &entry.value;
            auto err = vis.visit_field(std::string_view(entry.key), val);
            if(err != success) [[unlikely]]
                return err;
        }
        return success;
    }

    template <typename Visitor>
    static error_type visit_array(value_type& src, Visitor&& vis) {
        if(!src)
            return type_mismatch;
        const Array* arr = src->get_array();
        if(!arr)
            return type_mismatch;
        for(std::size_t i = 0; i < arr->size(); ++i) {
            value_type elem = &(*arr)[i];
            auto err = vis.visit_element(elem);
            if(err != success) [[unlikely]]
                return err;
        }
        return success;
    }

    static meta::type_kind kind_of(value_type& src) {
        if(!src)
            return meta::type_kind::null;
        switch(src->kind()) {
            case ValueKind::null_value: return meta::type_kind::null;
            case ValueKind::boolean: return meta::type_kind::boolean;
            case ValueKind::signed_int: return meta::type_kind::int64;
            case ValueKind::unsigned_int: return meta::type_kind::uint64;
            case ValueKind::floating: return meta::type_kind::float64;
            case ValueKind::string: return meta::type_kind::string;
            case ValueKind::array: return meta::type_kind::array;
            case ValueKind::object: return meta::type_kind::structure;
            default: return meta::type_kind::unknown;
        }
    }

    template <typename Visitor>
    static error_type visit_object_keys(value_type& src, Visitor&& vis) {
        if(!src)
            return type_mismatch;
        const Object* obj = src->get_object();
        if(!obj)
            return type_mismatch;
        for(const auto& entry: *obj) {
            value_type val = &entry.value;
            meta::type_kind k = kind_of(val);
            auto err = vis.on_field(std::string_view(entry.key), k, val);
            if(err != success) [[unlikely]]
                return err;
        }
        return success;
    }

    template <typename Visitor>
    static error_type visit_array_keys(value_type& src, Visitor&& vis) {
        if(!src)
            return type_mismatch;
        const Array* arr = src->get_array();
        if(!arr)
            return type_mismatch;
        std::size_t total = arr->size();
        for(std::size_t i = 0; i < total; ++i) {
            value_type elem = &(*arr)[i];
            meta::type_kind elem_kind = kind_of(elem);
            auto err = vis.on_element(i, total, elem_kind, elem);
            if(err != success) [[unlikely]]
                return err;
        }
        return success;
    }

    static error_type scan_field(value_type& src, std::string_view field_name, std::string_view& out) {
        if(!src)
            return type_mismatch;
        const Object* obj = src->get_object();
        if(!obj)
            return type_mismatch;
        const Value* val = obj->find(field_name);
        if(!val)
            return type_mismatch;
        auto s = val->get_string();
        if(!s)
            return type_mismatch;
        out = *s;
        return success;
    }
};

template <typename Config>
struct content_backend_with_config : content_backend {
    using config_type = Config;
    using base_backend_type = content_backend;
};

}  // namespace kota::codec::content
