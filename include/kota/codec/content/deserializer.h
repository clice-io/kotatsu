#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "kota/support/expected_try.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/content/document.h"
#include "kota/codec/content/error.h"
#include "kota/codec/deserialize.h"

namespace kota::codec::content {

struct content_backend {
    using value_type = const Value*;
    using error_type = error_kind;
    constexpr static error_type success = error_kind::ok;
    constexpr static error_type type_mismatch = error_kind::type_mismatch;
    constexpr static error_type number_out_of_range = error_kind::number_out_of_range;

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
};

template <typename Config>
struct content_backend_with_config : content_backend {
    using config_type = Config;
    using base_backend_type = content_backend;
};

template <typename Config = config::default_config, typename T>
auto from_content(const Value& value, T& out) -> std::expected<void, error> {
    using Backend = content_backend_with_config<Config>;
    typename Backend::value_type src = &value;
    auto err = codec::deserialize<Backend>(src, out);
    if(err != error_kind::ok) {
        return std::unexpected(error(err));
    }
    return {};
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_content(const Value& value) -> std::expected<T, error> {
    T out{};
    KOTA_EXPECTED_TRY(from_content<Config>(value, out));
    return out;
}

}  // namespace kota::codec::content

namespace kota::codec {

/// deserialize_traits for content::Value: copy the value directly
template <>
struct deserialize_traits<content::content_backend, content::Value> {
    static auto read(const content::Value*& src, content::Value& out) -> content::error_kind {
        if(!src)
            return content::error_kind::type_mismatch;
        out = *src;
        return content::error_kind::ok;
    }
};

/// deserialize_traits for content::Array: extract array from value
template <>
struct deserialize_traits<content::content_backend, content::Array> {
    static auto read(const content::Value*& src, content::Array& out) -> content::error_kind {
        if(!src)
            return content::error_kind::type_mismatch;
        const content::Array* arr = src->get_array();
        if(!arr)
            return content::error_kind::type_mismatch;
        out = *arr;
        return content::error_kind::ok;
    }
};

/// deserialize_traits for content::Object: extract object from value
template <>
struct deserialize_traits<content::content_backend, content::Object> {
    static auto read(const content::Value*& src, content::Object& out) -> content::error_kind {
        if(!src)
            return content::error_kind::type_mismatch;
        const content::Object* obj = src->get_object();
        if(!obj)
            return content::error_kind::type_mismatch;
        out = *obj;
        return content::error_kind::ok;
    }
};

/// Generic deserialize_traits for content::Value from any DOM backend with kind_of support.
/// The full specialization for content_backend above takes priority (more efficient copy path).
template <typename Backend>
    requires requires(typename Backend::value_type& v) {
        { Backend::kind_of(v) } -> std::same_as<meta::type_kind>;
    }
struct deserialize_traits<Backend, content::Value> {
    static auto read(typename Backend::value_type& val, content::Value& out) ->
        typename Backend::error_type {
        auto kind = Backend::kind_of(val);

        if(kind == meta::type_kind::null) {
            bool is_null = false;
            auto err = Backend::read_is_null(val, is_null);
            if(err != Backend::success)
                return err;
            out = content::Value(nullptr);
            return Backend::success;
        }
        if(kind == meta::type_kind::boolean) {
            bool b = false;
            auto err = Backend::read_bool(val, b);
            if(err != Backend::success)
                return err;
            out = content::Value(b);
            return Backend::success;
        }
        if(kind == meta::type_kind::uint64) {
            std::uint64_t u = 0;
            auto err = Backend::read_uint64(val, u);
            if(err != Backend::success)
                return err;
            out = content::Value(u);
            return Backend::success;
        }
        if(meta::is_integer_kind(kind)) {
            std::int64_t i = 0;
            auto err = Backend::read_int64(val, i);
            if(err != Backend::success)
                return err;
            out = content::Value(i);
            return Backend::success;
        }
        if(meta::is_floating_kind(kind)) {
            double d = 0.0;
            auto err = Backend::read_double(val, d);
            if(err != Backend::success)
                return err;
            out = content::Value(d);
            return Backend::success;
        }
        if(kind == meta::type_kind::string) {
            std::string_view sv;
            auto err = Backend::read_string(val, sv);
            if(err != Backend::success)
                return err;
            out = content::Value(std::string(sv));
            return Backend::success;
        }
        if(meta::is_sequence_kind(kind)) {
            content::Array arr;

            struct array_visitor {
                content::Array& arr;

                typename Backend::error_type visit_element(typename Backend::value_type& elem) {
                    content::Value v;
                    auto err = deserialize_traits::read(elem, v);
                    if(err != Backend::success)
                        return err;
                    arr.push_back(std::move(v));
                    return Backend::success;
                }
            };

            array_visitor vis{arr};
            auto err = Backend::visit_array(val, vis);
            if(err != Backend::success)
                return err;
            out = content::Value(std::move(arr));
            return Backend::success;
        }
        if(meta::is_object_kind(kind)) {
            content::Object obj;

            struct object_visitor {
                content::Object& obj;

                typename Backend::error_type visit_field(std::string_view key,
                                                         typename Backend::value_type& field_val) {
                    content::Value v;
                    auto err = deserialize_traits::read(field_val, v);
                    if(err != Backend::success)
                        return err;
                    obj.insert(std::string(key), std::move(v));
                    return Backend::success;
                }
            };

            object_visitor vis{obj};
            auto err = Backend::visit_object(val, vis);
            if(err != Backend::success)
                return err;
            out = content::Value(std::move(obj));
            return Backend::success;
        }
        return Backend::type_mismatch;
    }
};

}  // namespace kota::codec
