#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "simdjson.h"
#include "kota/support/expected_try.h"
#include "kota/support/type_traits.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/deserialize.h"
#include "kota/codec/json/error.h"
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
        simdjson::ondemand::json_type t;
        if(src.type().get(t) != simdjson::SUCCESS)
            return meta::type_kind::null;
        switch(t) {
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
            bool reset_ok;
            auto reset_err = obj.reset().get(reset_ok);
            if(reset_err != simdjson::SUCCESS)
                return reset_err;
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
        detail::thread_error_context().set(json::error::missing_field(field_name));
    }

    static void report_unknown_field(std::string_view field_name) {
        detail::thread_error_context().set(json::error::unknown_field(field_name));
    }

    static void report_unknown_enum(std::string_view value) {
        detail::thread_error_context().set(
            json::error(error_kind::type_mismatch,
                        std::format("unknown enum string value '{}'", value)));
    }

    static void report_prepend_field(std::string_view name) {
        detail::thread_error_context().prepend_field(name);
    }

    static void report_prepend_index(std::size_t index) {
        detail::thread_error_context().prepend_index(index);
    }
};

/// Config-aware simdjson backend: inherits all functionality from simdjson_backend
/// but carries a config_type that deserialize() can extract for field renaming, etc.
/// The base_backend_type allows deserialize_traits specializations for simdjson_backend
/// to be found when the config-aware variant is used.
template <typename Config>
struct simdjson_backend_with_config : simdjson_backend {
    using config_type = Config;
    using base_backend_type = simdjson_backend;
};

/// Scalar-only backend for deserializing scalar JSON documents directly.
/// simdjson cannot convert scalar documents to ondemand::value, so this backend
/// operates on ondemand::document& for scalar reads. Compound-type methods
/// (visit_object, visit_array, etc.) are stubs that return errors — they exist
/// only to satisfy template instantiation requirements and are never called at
/// runtime for scalar documents.
struct simdjson_scalar_backend {
    using value_type = simdjson::ondemand::document;
    using error_type = simdjson::error_code;
    static constexpr error_type success = simdjson::SUCCESS;
    static constexpr error_type type_mismatch = simdjson::INCORRECT_TYPE;
    static constexpr error_type number_out_of_range = simdjson::NUMBER_OUT_OF_RANGE;
    static constexpr error_type invalid_state = simdjson::PARSER_IN_USE;

    using base_backend_type = simdjson_backend;

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
        auto result = v.is_null();
        if(result.error() != simdjson::SUCCESS) {
            is_null = false;
            return simdjson::SUCCESS;
        }
        is_null = result.value_unsafe();
        return simdjson::SUCCESS;
    }

    static meta::type_kind kind_of(value_type& src) {
        simdjson::ondemand::json_type t;
        if(src.type().get(t) != simdjson::SUCCESS)
            return meta::type_kind::null;
        switch(t) {
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

    // Stubs for compound-type visitors — never called at runtime for scalar
    // documents but required for template instantiation of deserialize<>.

    template <typename Visitor>
    static error_type visit_object(value_type&, Visitor&&) {
        return type_mismatch;
    }

    template <typename Visitor>
    static error_type visit_array(value_type&, Visitor&&) {
        return type_mismatch;
    }

    template <typename Visitor>
    static error_type visit_object_keys(value_type&, Visitor&&) {
        return success;
    }

    template <typename Visitor>
    static error_type visit_array_keys(value_type&, Visitor&&) {
        return success;
    }

    static error_type scan_field(value_type&, std::string_view, std::string_view&) {
        return type_mismatch;
    }

    // Error context helpers (same as simdjson_backend)

    static void report_missing_field(std::string_view field_name) {
        detail::thread_error_context().set(json::error::missing_field(field_name));
    }

    static void report_unknown_field(std::string_view field_name) {
        detail::thread_error_context().set(json::error::unknown_field(field_name));
    }

    static void report_unknown_enum(std::string_view value) {
        detail::thread_error_context().set(
            json::error(error_kind::type_mismatch,
                        std::format("unknown enum string value '{}'", value)));
    }

    static void report_prepend_field(std::string_view name) {
        detail::thread_error_context().prepend_field(name);
    }

    static void report_prepend_index(std::size_t index) {
        detail::thread_error_context().prepend_index(index);
    }
};

/// Config-aware scalar backend variant.
template <typename Config>
struct simdjson_scalar_backend_with_config : simdjson_scalar_backend {
    using config_type = Config;
};

namespace detail {

/// Deserialize from a simdjson document root using the visitor-based path.
/// For compound types (objects/arrays), converts document to value via get_value()
/// and delegates to deserialize<Backend>(). For scalar documents, uses the
/// simdjson_scalar_backend which operates directly on the document.
template <typename Config, typename T>
auto from_document(simdjson::ondemand::parser& parser,
                   simdjson::ondemand::document& doc,
                   T& out,
                   simdjson::padded_string_view json) -> simdjson::error_code {
    using Backend = simdjson_backend_with_config<Config>;
    using ScalarBackend = simdjson_scalar_backend_with_config<Config>;
    using U = std::remove_cvref_t<T>;

    simdjson::ondemand::value val;
    auto err = doc.get_value().get(val);
    if(err == simdjson::SUCCESS) {
        return codec::deserialize<Backend>(val, out);
    }

    // Scalar documents cannot be converted to value — handle directly
    if(err == simdjson::SCALAR_DOCUMENT_AS_VALUE) {
        // For types with deserialize_traits or annotated types, re-parse
        // the scalar wrapped in a JSON array to obtain a value instance.
        if constexpr(codec::has_deserialize_traits<Backend, U> || meta::annotated_type<U>) {
            std::string wrapped;
            wrapped.reserve(json.length() + 2);
            wrapped += '[';
            wrapped.append(json.data(), json.length());
            wrapped += ']';
            simdjson::padded_string padded_wrapped(wrapped);
            simdjson::ondemand::document arr_doc;
            auto doc_err = parser.iterate(padded_wrapped).get(arr_doc);
            if(doc_err != simdjson::SUCCESS)
                return doc_err;
            simdjson::ondemand::array arr;
            doc_err = arr_doc.get_array().get(arr);
            if(doc_err != simdjson::SUCCESS)
                return doc_err;
            for(auto elem_result: arr) {
                simdjson::ondemand::value elem;
                doc_err = std::move(elem_result).get(elem);
                if(doc_err != simdjson::SUCCESS)
                    return doc_err;
                return codec::deserialize<Backend>(elem, out);
            }
            return simdjson::INCORRECT_TYPE;
        } else {
            return codec::deserialize<ScalarBackend>(doc, out);
        }
    }

    return err;
}

}  // namespace detail

namespace detail {

/// Compute line and column from a JSON string and a byte offset.
inline source_location compute_location(std::string_view json, std::size_t byte_offset) {
    source_location loc;
    loc.byte_offset = byte_offset;
    loc.line = 1;
    loc.column = 1;
    for(std::size_t i = 0; i < byte_offset && i < json.size(); ++i) {
        if(json[i] == '\n') {
            ++loc.line;
            loc.column = 1;
        } else {
            ++loc.column;
        }
    }
    return loc;
}

/// Locate the byte offset of a field value in the JSON string by navigating
/// the path segments (field names and array indices) using simdjson re-parse.
inline std::optional<std::size_t> locate_path_in_json(
    simdjson::padded_string_view json,
    const error& err) {
    auto path = err.format_path();
    if(path.empty()) {
        return std::nullopt;
    }

    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    auto doc_err = parser.iterate(json).get(doc);
    if(doc_err != simdjson::SUCCESS) {
        return std::nullopt;
    }

    simdjson::ondemand::value current;
    doc_err = doc.get_value().get(current);
    if(doc_err != simdjson::SUCCESS) {
        return std::nullopt;
    }

    // Parse the path and navigate into the document
    std::string_view remaining(path);
    while(!remaining.empty()) {
        if(remaining.front() == '[') {
            // Array index
            auto close = remaining.find(']');
            if(close == std::string_view::npos) {
                return std::nullopt;
            }
            auto idx_str = remaining.substr(1, close - 1);
            std::size_t idx = 0;
            for(char c: idx_str) {
                idx = idx * 10 + (c - '0');
            }
            remaining = remaining.substr(close + 1);
            if(!remaining.empty() && remaining.front() == '.') {
                remaining = remaining.substr(1);
            }

            simdjson::ondemand::array arr;
            auto err2 = current.get_array().get(arr);
            if(err2 != simdjson::SUCCESS) {
                return std::nullopt;
            }
            std::size_t i = 0;
            bool found = false;
            for(auto elem_result: arr) {
                if(i == idx) {
                    auto err3 = std::move(elem_result).get(current);
                    if(err3 != simdjson::SUCCESS) {
                        return std::nullopt;
                    }
                    found = true;
                    break;
                }
                ++i;
            }
            if(!found) {
                return std::nullopt;
            }
        } else {
            // Field name
            auto dot = remaining.find('.');
            auto bracket = remaining.find('[');
            auto end = std::min(dot, bracket);
            auto field_name = remaining.substr(0, end);
            if(end == std::string_view::npos) {
                remaining = {};
            } else {
                remaining = remaining.substr(end);
                if(!remaining.empty() && remaining.front() == '.') {
                    remaining = remaining.substr(1);
                }
            }

            simdjson::ondemand::object obj;
            auto err2 = current.get_object().get(obj);
            if(err2 != simdjson::SUCCESS) {
                return std::nullopt;
            }
            simdjson::ondemand::value val;
            auto err3 = obj.find_field_unordered(field_name).get(val);
            if(err3 != simdjson::SUCCESS) {
                return std::nullopt;
            }
            current = val;
        }
    }

    // Get the current location of the value we navigated to
    const char* loc_ptr = nullptr;
    auto loc_err = current.current_location().get(loc_ptr);
    if(loc_err != simdjson::SUCCESS) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(loc_ptr - json.data());
}

/// Build a rich serde_error from the thread-local error context and the raw
/// simdjson error code. If the context has a pending error, use it. Otherwise
/// fall back to constructing an error from the error code alone.
inline error build_error(simdjson::error_code err,
                         simdjson::padded_string_view json) {
    auto& ctx = thread_error_context();
    auto pending = ctx.take();
    if(pending) {
        // Try to add source location for errors with a path
        auto byte_off = locate_path_in_json(json, *pending);
        if(byte_off) {
            pending->set_location(compute_location(
                std::string_view(json.data(), json.length()), *byte_off));
        }
        return std::move(*pending);
    }
    return error(make_error(err));
}

}  // namespace detail

template <typename Config = config::default_config, typename T>
auto from_json(simdjson::padded_string_view json, T& value) -> std::expected<void, error> {
    detail::thread_error_context().clear();

    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    auto doc_err = parser.iterate(json).get(doc);
    if(doc_err != simdjson::SUCCESS) {
        return std::unexpected(error(make_error(doc_err)));
    }

    auto err = detail::from_document<Config>(parser, doc, value, json);
    if(err != simdjson::SUCCESS) {
        return std::unexpected(detail::build_error(err, json));
    }

    return {};
}

template <typename Config = config::default_config, typename T>
auto from_json(std::string_view json, T& value) -> std::expected<void, error> {
    simdjson::padded_string padded_json(json);
    return from_json<Config>(static_cast<simdjson::padded_string_view>(padded_json), value);
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_json(std::string_view json) -> std::expected<T, error> {
    T value{};
    KOTA_EXPECTED_TRY(from_json<Config>(json, value));
    return value;
}

template <typename T, typename Config = config::default_config>
    requires std::default_initializable<T>
auto from_json(simdjson::padded_string_view json) -> std::expected<T, error> {
    T value{};
    KOTA_EXPECTED_TRY(from_json<Config>(json, value));
    return value;
}

}  // namespace kota::codec::json
