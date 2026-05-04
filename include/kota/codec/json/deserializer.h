#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "simdjson.h"
#include "kota/support/config.h"
#include "kota/support/expected_try.h"
#include "kota/support/type_traits.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/deserialize.h"
#include "kota/codec/detail/error_context.h"
#include "kota/codec/json/error.h"

namespace kota::codec::json {

/// Tagged-pointer wrapper: holds either a simdjson::ondemand::value* or document*
/// in a single pointer, using the low bit (both types are pointer-aligned) to
/// distinguish them.  Eliminates all document-root special casing in the backend.
struct json_source {
    KOTA_ALWAYS_INLINE explicit json_source(simdjson::ondemand::value& v) :
        ptr(reinterpret_cast<uintptr_t>(&v)) {}

    KOTA_ALWAYS_INLINE explicit json_source(simdjson::ondemand::document& d) :
        ptr(reinterpret_cast<uintptr_t>(&d) | tag) {}

    KOTA_ALWAYS_INLINE static json_source null() {
        return json_source(uintptr_t{0});
    }

    KOTA_ALWAYS_INLINE bool is_null() const {
        return ptr == 0;
    }

    KOTA_ALWAYS_INLINE bool is_document() const {
        return (ptr & tag) != 0;
    }

    KOTA_ALWAYS_INLINE simdjson::ondemand::document& doc() const {
        return *reinterpret_cast<simdjson::ondemand::document*>(ptr & ~tag);
    }

    KOTA_ALWAYS_INLINE simdjson::ondemand::value& value() const {
        return *reinterpret_cast<simdjson::ondemand::value*>(ptr);
    }

    template <typename F>
    KOTA_ALWAYS_INLINE decltype(auto) apply(F&& f) const {
        if(is_document()) {
            return f(doc());
        }
        return f(value());
    }

private:
    uintptr_t ptr;
    constexpr static uintptr_t tag = 1;

    KOTA_ALWAYS_INLINE explicit json_source(uintptr_t raw) : ptr(raw) {}
};

struct simdjson_backend {
    using value_type = json_source;
    using error_type = simdjson::error_code;
    constexpr static error_type success = simdjson::SUCCESS;
    constexpr static error_type type_mismatch = simdjson::INCORRECT_TYPE;
    constexpr static error_type number_out_of_range = simdjson::NUMBER_OUT_OF_RANGE;
    constexpr static error_type invalid_state = simdjson::PARSER_IN_USE;

    KOTA_ALWAYS_INLINE static error_type read_bool(value_type& src, bool& out) {
        if(src.is_document()) {
            return src.doc().get_bool().get(out);
        }
        return src.value().get_bool().get(out);
    }

    KOTA_ALWAYS_INLINE static error_type read_int64(value_type& src, std::int64_t& out) {
        if(src.is_document()) {
            return src.doc().get_int64().get(out);
        }
        return src.value().get_int64().get(out);
    }

    KOTA_ALWAYS_INLINE static error_type read_uint64(value_type& src, std::uint64_t& out) {
        if(src.is_document()) {
            return src.doc().get_uint64().get(out);
        }
        return src.value().get_uint64().get(out);
    }

    KOTA_ALWAYS_INLINE static error_type read_double(value_type& src, double& out) {
        if(src.is_document()) {
            return src.doc().get_double().get(out);
        }
        return src.value().get_double().get(out);
    }

    KOTA_ALWAYS_INLINE static error_type read_string(value_type& src, std::string_view& out) {
        if(src.is_document()) {
            return src.doc().get_string().get(out);
        }
        return src.value().get_string().get(out);
    }

    KOTA_ALWAYS_INLINE static error_type read_is_null(value_type& src, bool& is_null) {
        if(src.is_document()) {
            auto result = src.doc().is_null();
            if(result.error() != simdjson::SUCCESS) [[unlikely]] {
                is_null = false;
                return simdjson::SUCCESS;
            }
            is_null = result.value_unsafe();
            return simdjson::SUCCESS;
        }
        // Use type() instead of is_null() to avoid assert_at_non_root_start()
        // which fails on values obtained from doc.get_value() (depth == 1).
        auto t = src.value().type();
        if(t.error() != simdjson::SUCCESS) [[unlikely]] {
            return t.error();
        }
        is_null = (t.value_unsafe() == simdjson::ondemand::json_type::null);
        return simdjson::SUCCESS;
    }

    template <typename Visitor>
    KOTA_ALWAYS_INLINE static error_type visit_object(value_type& src, Visitor&& vis) {
        auto obj = src.apply([](auto& s) { return s.get_object(); });
        if(obj.error()) [[unlikely]] {
            return obj.error();
        }
        for(auto entry: obj) {
            if(entry.error()) [[unlikely]] {
                return entry.error();
            }
            auto key = entry.unescaped_key();
            if(key.error()) [[unlikely]] {
                return key.error();
            }
            auto val = entry.value_unsafe().value();
            value_type field_src(val);
            auto err = vis.visit_field(key.value_unsafe(), field_src);
            if(err != simdjson::SUCCESS) [[unlikely]] {
                return err;
            }
        }
        return simdjson::SUCCESS;
    }

    template <typename Visitor>
    KOTA_ALWAYS_INLINE static error_type visit_array(value_type& src, Visitor&& vis) {
        auto arr = src.apply([](auto& s) { return s.get_array(); });
        if(arr.error()) [[unlikely]] {
            return arr.error();
        }
        for(auto elem: arr) {
            if(elem.error()) [[unlikely]] {
                return elem.error();
            }
            auto val = elem.value_unsafe();
            value_type elem_src(val);
            auto err = vis.visit_element(elem_src);
            if(err != simdjson::SUCCESS) [[unlikely]] {
                return err;
            }
        }
        return simdjson::SUCCESS;
    }

    KOTA_ALWAYS_INLINE static meta::type_kind kind_of(value_type& src) {
        if(src.is_null()) {
            return meta::type_kind::unknown;
        }
        auto t = src.apply([](auto& s) { return s.type(); });
        if(t.error() != simdjson::SUCCESS) [[unlikely]] {
            return meta::type_kind::null;
        }
        switch(t.value_unsafe()) {
            case simdjson::ondemand::json_type::object: return meta::type_kind::structure;
            case simdjson::ondemand::json_type::array: return meta::type_kind::array;
            case simdjson::ondemand::json_type::string: return meta::type_kind::string;
            case simdjson::ondemand::json_type::number: {
                auto nt = src.apply([](auto& s) { return s.get_number_type(); });
                if(nt.error() != simdjson::SUCCESS) [[unlikely]] {
                    return meta::type_kind::int64;
                }
                if(nt.value_unsafe() == simdjson::ondemand::number_type::floating_point_number) {
                    return meta::type_kind::float64;
                }
                if(nt.value_unsafe() == simdjson::ondemand::number_type::unsigned_integer) {
                    return meta::type_kind::uint64;
                }
                return meta::type_kind::int64;
            }
            case simdjson::ondemand::json_type::boolean: return meta::type_kind::boolean;
            case simdjson::ondemand::json_type::null: return meta::type_kind::null;
            default: return meta::type_kind::unknown;
        }
    }

    template <typename Visitor>
    KOTA_ALWAYS_INLINE static error_type visit_object_keys(value_type& src, Visitor&& vis) {
        auto count = src.apply([](auto& s) { return s.count_fields(); });
        if(count.error()) [[unlikely]] {
            return count.error();
        }
        if(count.value_unsafe() == 0) {
            return simdjson::SUCCESS;
        }

        auto obj = src.apply([](auto& s) { return s.get_object(); });
        if(obj.error()) [[unlikely]] {
            return obj.error();
        }
        for(auto entry: obj) {
            if(entry.error()) [[unlikely]] {
                return entry.error();
            }
            auto key = entry.unescaped_key();
            if(key.error()) [[unlikely]] {
                return key.error();
            }
            auto val = entry.value_unsafe().value();
            value_type field_src(val);
            auto kind = kind_of(field_src);
            auto err = vis.on_field(key.value_unsafe(), kind, field_src);
            if(err != simdjson::SUCCESS) [[unlikely]] {
                return err;
            }
        }
        if(src.is_document()) {
            src.doc().rewind();
        } else {
            auto r = obj.reset();
            if(r.error()) [[unlikely]] {
                return r.error();
            }
        }
        return simdjson::SUCCESS;
    }

    template <typename Visitor>
    KOTA_ALWAYS_INLINE static error_type visit_array_keys(value_type& src, Visitor&& vis) {
        auto count = src.apply([](auto& s) { return s.count_elements(); });
        if(count.error()) [[unlikely]] {
            return count.error();
        }
        auto n = count.value_unsafe();
        if(n == 0) {
            return simdjson::SUCCESS;
        }

        auto arr = src.apply([](auto& s) { return s.get_array(); });
        if(arr.error()) [[unlikely]] {
            return arr.error();
        }
        std::size_t i = 0;
        for(auto elem: arr) {
            if(elem.error()) [[unlikely]] {
                return elem.error();
            }
            auto val = elem.value_unsafe();
            value_type elem_src(val);
            auto kind = kind_of(elem_src);
            auto err = vis.on_element(i, n, kind, elem_src);
            if(err != simdjson::SUCCESS) [[unlikely]] {
                return err;
            }
            ++i;
        }
        if(src.is_document()) {
            src.doc().rewind();
        } else {
            auto r = arr.count_elements();
            if(r.error()) [[unlikely]] {
                return r.error();
            }
        }
        return simdjson::SUCCESS;
    }

    KOTA_ALWAYS_INLINE static auto capture_raw_json(value_type& src)
        -> std::pair<std::string, error_type> {
        std::string_view raw;
        auto err = src.apply([&](auto& s) { return s.raw_json().get(raw); });
        if(err != simdjson::SUCCESS) [[unlikely]] {
            return {{}, err};
        }
        return {std::string(raw), simdjson::SUCCESS};
    }

    template <typename Fn>
    KOTA_ALWAYS_INLINE static auto with_reparsed(std::string_view raw_json, Fn&& fn) -> error_type {
        simdjson::padded_string padded(raw_json);
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document doc;
        auto err = parser.iterate(padded).get(doc);
        if(err != simdjson::SUCCESS) [[unlikely]] {
            return err;
        }
        value_type src(doc);
        return fn(src);
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
inline std::optional<std::size_t> locate_path_in_json(simdjson::padded_string_view json,
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
inline error build_error(simdjson::error_code err, simdjson::padded_string_view json) {
    auto& ctx = codec::detail::thread_error_context();
    if(ctx.has_error()) {
        auto pending = ctx.take_as<error_kind>(make_error(err));
        auto byte_off = locate_path_in_json(json, pending);
        if(byte_off) {
            pending.set_location(
                compute_location(std::string_view(json.data(), json.length()), *byte_off));
        }
        return pending;
    }
    return error(make_error(err));
}

}  // namespace detail

template <typename Config = config::default_config, typename T>
auto from_json(simdjson::padded_string_view json, T& value) -> std::expected<void, error> {
    codec::detail::thread_error_context().clear();

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
