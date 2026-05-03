#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "simdjson.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/json/error_context.h"

namespace kota::codec::json {

struct simdjson_backend {
    using value_type = simdjson::ondemand::value;
    using error_type = simdjson::error_code;
    static constexpr error_type success = simdjson::SUCCESS;
    static constexpr error_type type_mismatch = simdjson::INCORRECT_TYPE;
    static constexpr error_type number_out_of_range = simdjson::NUMBER_OUT_OF_RANGE;
    static constexpr error_type invalid_state = simdjson::PARSER_IN_USE;

    static error_type read_bool(value_type& v, bool& out) {
        return v.get_bool().get(out);
    }

    static error_type read_int64(value_type& v, std::int64_t& out) {
        return v.get_int64().get(out);
    }

    static error_type read_uint64(value_type& v, std::uint64_t& out) {
        return v.get_uint64().get(out);
    }

    static error_type read_double(value_type& v, double& out) {
        return v.get_double().get(out);
    }

    static error_type read_string(value_type& v, std::string_view& out) {
        return v.get_string().get(out);
    }

    static error_type read_is_null(value_type& v, bool& is_null) {
        // Use type() instead of is_null() to avoid assert_at_non_root_start()
        // which fails on values obtained from doc.get_value() (depth == 1).
        auto t = v.type();
        if(t.error() != simdjson::SUCCESS)
            return t.error();
        is_null = (t.value_unsafe() == simdjson::ondemand::json_type::null);
        return simdjson::SUCCESS;
    }

    template <typename Visitor>
    static error_type visit_object(value_type& src, Visitor&& vis) {
        simdjson::ondemand::object obj;
        auto err = src.get_object().get(obj);
        if(err != simdjson::SUCCESS) [[unlikely]]
            return err;
        for(auto field_result: obj) {
            simdjson::ondemand::field field;
            err = std::move(field_result).get(field);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
            std::string_view key;
            err = field.unescaped_key().get(key);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
            value_type val = field.value();
            err = vis.visit_field(key, val);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
        }
        return simdjson::SUCCESS;
    }

    template <typename Visitor>
    static error_type visit_array(value_type& src, Visitor&& vis) {
        simdjson::ondemand::array arr;
        auto err = src.get_array().get(arr);
        if(err != simdjson::SUCCESS) [[unlikely]]
            return err;
        for(auto elem_result: arr) {
            value_type elem;
            err = std::move(elem_result).get(elem);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
            err = vis.visit_element(elem);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
        }
        return simdjson::SUCCESS;
    }

    static meta::type_kind kind_of(value_type& src) {
        switch(src.type()) {
            case simdjson::ondemand::json_type::object: return meta::type_kind::structure;
            case simdjson::ondemand::json_type::array: return meta::type_kind::array;
            case simdjson::ondemand::json_type::string: return meta::type_kind::string;
            case simdjson::ondemand::json_type::number: {
                simdjson::ondemand::number_type nt;
                if(src.get_number_type().get(nt) != simdjson::SUCCESS)
                    return meta::type_kind::int64;
                if(nt == simdjson::ondemand::number_type::floating_point_number)
                    return meta::type_kind::float64;
                if(nt == simdjson::ondemand::number_type::unsigned_integer)
                    return meta::type_kind::uint64;
                return meta::type_kind::int64;
            }
            case simdjson::ondemand::json_type::boolean: return meta::type_kind::boolean;
            case simdjson::ondemand::json_type::null: return meta::type_kind::null;
            default: return meta::type_kind::unknown;
        }
    }

    template <typename Visitor>
    static error_type visit_object_keys(value_type& src, Visitor&& vis) {
        simdjson::ondemand::object obj;
        auto err = src.get_object().get(obj);
        if(err != simdjson::SUCCESS) [[unlikely]]
            return err;
        for(auto field_result: obj) {
            simdjson::ondemand::field field;
            err = std::move(field_result).get(field);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
            std::string_view key;
            err = field.unescaped_key().get(key);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
            value_type val = field.value();
            meta::type_kind kind = kind_of(val);
            err = vis.on_field(key, kind, val);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
        }
        {
            auto reset_err = obj.reset();
            if(reset_err.error() != simdjson::SUCCESS)
                return reset_err.error();
        }
        return simdjson::SUCCESS;
    }

    /// Array key visitor for variant scoring.
    /// Uses count_elements() to get the total (which rewinds the value),
    /// then reports each element with unknown kind. Deep per-element type
    /// scoring is not available because simdjson ondemand is forward-only
    /// and iterating elements would consume the value.
    template <typename Visitor>
    static error_type visit_array_keys(value_type& src, Visitor&& vis) {
        std::size_t count = 0;
        auto count_err = src.count_elements().get(count);
        if(count_err != simdjson::SUCCESS) [[unlikely]]
            return count_err;
        // count_elements() rewinds the value, so src remains consumable.
        // Report the total count but unknown element kinds.
        for(std::size_t i = 0; i < count; ++i) {
            auto err = vis.on_element(i, count, meta::type_kind::unknown, src);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
        }
        return simdjson::SUCCESS;
    }

    static error_type scan_field(value_type& src,
                                 std::string_view field_name,
                                 std::string_view& out) {
        // Use value::find_field which internally calls start_or_resume_object().
        // After reading the tag, calling find_field again or iterating the object
        // via value's [] operator will use start_or_resume_object() for correct
        // re-entry into the object.
        simdjson::ondemand::value val;
        auto err = src.find_field(field_name).get(val);
        if(err != simdjson::SUCCESS)
            return err;
        return val.get_string().get(out);
    }

    /// Capture the raw JSON text of a value. Consumes the value.
    /// Returns the raw JSON string and an error code.
    static auto capture_raw_json(value_type& src) -> std::pair<std::string, error_type> {
        std::string_view raw;
        auto err = src.raw_json().get(raw);
        if(err != simdjson::SUCCESS)
            return {{}, err};
        return {std::string(raw), simdjson::SUCCESS};
    }

    /// Re-parse captured raw JSON and invoke a callback with a fresh value.
    /// The callback receives a value_type& that can be fully consumed.
    template <typename Fn>
    static auto with_reparsed(std::string_view raw_json, Fn&& fn) -> error_type {
        // Wrap in array to convert scalar JSON into a value-extractable form
        std::string wrapped;
        wrapped.reserve(raw_json.size() + 2);
        wrapped += '[';
        wrapped.append(raw_json.data(), raw_json.size());
        wrapped += ']';
        simdjson::padded_string padded(wrapped);
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        auto doc_err = parser.iterate(padded).get(doc);
        if(doc_err != simdjson::SUCCESS)
            return doc_err;
        simdjson::ondemand::array arr;
        doc_err = doc.get_array().get(arr);
        if(doc_err != simdjson::SUCCESS)
            return doc_err;
        for(auto elem_result: arr) {
            value_type val;
            doc_err = std::move(elem_result).get(val);
            if(doc_err != simdjson::SUCCESS)
                return doc_err;
            return fn(val);
        }
        return simdjson::INCORRECT_TYPE;
    }

    /// Error context helpers: store rich error info in the thread-local context.
    /// The from_json entry point checks this after a failed deserialization.

    static void report_missing_field(std::string_view field_name) {
        detail_v2::thread_error_context().set(json::error::missing_field(field_name));
    }

    static void report_unknown_field(std::string_view field_name) {
        detail_v2::thread_error_context().set(json::error::unknown_field(field_name));
    }

    static void report_unknown_enum(std::string_view value) {
        detail_v2::thread_error_context().set(
            json::error(error_kind::type_mismatch,
                        std::format("unknown enum string value '{}'", value)));
    }

    static void report_prepend_field(std::string_view name) {
        detail_v2::thread_error_context().prepend_field(name);
    }

    static void report_prepend_index(std::size_t index) {
        detail_v2::thread_error_context().prepend_index(index);
    }
};

/// Config-aware simdjson backend: inherits all functionality from simdjson_backend
/// but carries a config_type that deserialize() can extract for field renaming, etc.
/// The base_backend_type allows custom_deserialize specializations for simdjson_backend
/// to be found when the config-aware variant is used.
template <typename Config>
struct simdjson_backend_with_config : simdjson_backend {
    using config_type = Config;
    using base_backend_type = simdjson_backend;
};

}  // namespace kota::codec::json
