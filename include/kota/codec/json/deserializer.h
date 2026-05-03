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

/// Thin wrapper that holds either a simdjson::ondemand::value* or document*,
/// eliminating all scalar-document-root special casing in the backend.
struct json_source {
    simdjson::ondemand::value* val_ = nullptr;
    simdjson::ondemand::document* doc_ = nullptr;

    json_source() = default;
    explicit json_source(simdjson::ondemand::value& v) : val_(&v) {}
    explicit json_source(simdjson::ondemand::document& d) : doc_(&d) {}

    bool is_document() const { return doc_ != nullptr; }
};

struct simdjson_backend {
    using value_type = json_source;
    using error_type = simdjson::error_code;
    static constexpr error_type success = simdjson::SUCCESS;
    static constexpr error_type type_mismatch = simdjson::INCORRECT_TYPE;
    static constexpr error_type number_out_of_range = simdjson::NUMBER_OUT_OF_RANGE;
    static constexpr error_type invalid_state = simdjson::PARSER_IN_USE;

    static error_type read_bool(value_type& src, bool& out) {
        if(src.is_document()) return src.doc_->get_bool().get(out);
        return src.val_->get_bool().get(out);
    }

    static error_type read_int64(value_type& src, std::int64_t& out) {
        if(src.is_document()) return src.doc_->get_int64().get(out);
        return src.val_->get_int64().get(out);
    }

    static error_type read_uint64(value_type& src, std::uint64_t& out) {
        if(src.is_document()) return src.doc_->get_uint64().get(out);
        return src.val_->get_uint64().get(out);
    }

    static error_type read_double(value_type& src, double& out) {
        if(src.is_document()) return src.doc_->get_double().get(out);
        return src.val_->get_double().get(out);
    }

    static error_type read_string(value_type& src, std::string_view& out) {
        if(src.is_document()) return src.doc_->get_string().get(out);
        return src.val_->get_string().get(out);
    }

    static error_type read_is_null(value_type& src, bool& is_null) {
        if(src.is_document()) {
            auto result = src.doc_->is_null();
            if(result.error() != simdjson::SUCCESS) {
                is_null = false;
                return simdjson::SUCCESS;
            }
            is_null = result.value_unsafe();
            return simdjson::SUCCESS;
        }
        // Use type() instead of is_null() to avoid assert_at_non_root_start()
        // which fails on values obtained from doc.get_value() (depth == 1).
        auto t = src.val_->type();
        if(t.error() != simdjson::SUCCESS)
            return t.error();
        is_null = (t.value_unsafe() == simdjson::ondemand::json_type::null);
        return simdjson::SUCCESS;
    }

    template <typename Visitor>
    static error_type visit_object(value_type& src, Visitor&& vis) {
        simdjson::ondemand::object obj;
        auto err = src.is_document()
            ? src.doc_->get_object().get(obj)
            : src.val_->get_object().get(obj);
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
            simdjson::ondemand::value val = field.value();
            value_type field_src(val);
            err = vis.visit_field(key, field_src);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
        }
        return simdjson::SUCCESS;
    }

    template <typename Visitor>
    static error_type visit_array(value_type& src, Visitor&& vis) {
        simdjson::ondemand::array arr;
        auto err = src.is_document()
            ? src.doc_->get_array().get(arr)
            : src.val_->get_array().get(arr);
        if(err != simdjson::SUCCESS) [[unlikely]]
            return err;
        for(auto elem_result: arr) {
            simdjson::ondemand::value elem;
            err = std::move(elem_result).get(elem);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
            value_type elem_src(elem);
            err = vis.visit_element(elem_src);
            if(err != simdjson::SUCCESS) [[unlikely]]
                return err;
        }
        return simdjson::SUCCESS;
    }

    static meta::type_kind kind_of(value_type& src) {
        simdjson::ondemand::json_type t;
        auto err = src.is_document()
            ? src.doc_->type().get(t)
            : src.val_->type().get(t);
        if(err != simdjson::SUCCESS)
            return meta::type_kind::null;
        switch(t) {
            case simdjson::ondemand::json_type::object: return meta::type_kind::structure;
            case simdjson::ondemand::json_type::array: return meta::type_kind::array;
            case simdjson::ondemand::json_type::string: return meta::type_kind::string;
            case simdjson::ondemand::json_type::number: {
                simdjson::ondemand::number_type nt;
                auto nt_err = src.is_document()
                    ? src.doc_->get_number_type().get(nt)
                    : src.val_->get_number_type().get(nt);
                if(nt_err != simdjson::SUCCESS)
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
        auto err = src.is_document()
            ? src.doc_->get_object().get(obj)
            : src.val_->get_object().get(obj);
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
            simdjson::ondemand::value val = field.value();
            value_type field_src(val);
            meta::type_kind kind = kind_of(field_src);
            err = vis.on_field(key, kind, field_src);
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
        if(src.is_document()) {
            // Documents don't have count_elements; not expected for array scoring
            return type_mismatch;
        }
        std::size_t count = 0;
        auto count_err = src.val_->count_elements().get(count);
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
        if(src.is_document()) {
            // Documents need find_field via get_object path
            simdjson::ondemand::object obj;
            auto err = src.doc_->get_object().get(obj);
            if(err != simdjson::SUCCESS) return err;
            simdjson::ondemand::value val;
            err = obj.find_field(field_name).get(val);
            if(err != simdjson::SUCCESS) return err;
            return val.get_string().get(out);
        }
        // Use value::find_field which internally calls start_or_resume_object().
        // After reading the tag, calling find_field again or iterating the object
        // via value's [] operator will use start_or_resume_object() for correct
        // re-entry into the object.
        simdjson::ondemand::value val;
        auto err = src.val_->find_field(field_name).get(val);
        if(err != simdjson::SUCCESS)
            return err;
        return val.get_string().get(out);
    }

    /// Capture the raw JSON text of a value. Consumes the value.
    /// Returns the raw JSON string and an error code.
    static auto capture_raw_json(value_type& src) -> std::pair<std::string, error_type> {
        if(src.is_document()) {
            std::string_view raw;
            auto err = src.doc_->raw_json().get(raw);
            if(err != simdjson::SUCCESS)
                return {{}, err};
            return {std::string(raw), simdjson::SUCCESS};
        }
        std::string_view raw;
        auto err = src.val_->raw_json().get(raw);
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
            simdjson::ondemand::value val;
            doc_err = std::move(elem_result).get(val);
            if(doc_err != simdjson::SUCCESS)
                return doc_err;
            value_type src(val);
            return fn(src);
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

namespace detail {

/// Deserialize from a simdjson document root using the visitor-based path.
/// Uses json_source to wrap the document, unifying scalar and compound handling.
template <typename Config, typename T>
auto from_document(simdjson::ondemand::document& doc, T& out) -> simdjson::error_code {
    using Backend = simdjson_backend_with_config<Config>;
    json_source src(doc);
    return codec::deserialize<Backend>(src, out);
}

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

    auto err = detail::from_document<Config>(doc, value);
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
