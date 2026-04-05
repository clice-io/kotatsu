#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace eventide::serde {

/// Source location within serialized input.
struct source_location {
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t byte_offset = 0;
};

/// A single segment in a deserialization path: field name or array index.
using path_segment = std::variant<std::string, std::size_t>;

/// Generic serde error carrying rich context: message, field path, and source location.
/// Parameterized by a backend-specific error_kind enum.
///
/// Satisfies serde_error concept via static constexpr members and implicit Kind constructor.
/// Existing code like `std::unexpected(E::type_mismatch)` continues to work unchanged.
template <typename Kind>
struct basic_error {
    Kind kind;
    std::string message;
    std::vector<path_segment> path;
    std::optional<source_location> location;

    // --- serde_error concept compatibility ---
    constexpr static Kind type_mismatch = Kind::type_mismatch;
    constexpr static Kind number_out_of_range = Kind::number_out_of_range;
    constexpr static Kind invalid_state = Kind::invalid_state;

    // --- constructors ---
    basic_error() : kind(Kind::ok) {}

    basic_error(Kind k) : kind(k), message(std::string(error_message(k))) {}

    basic_error(Kind k, std::string msg) : kind(k), message(std::move(msg)) {}

    // --- semantic factory methods ---
    static basic_error missing_field(std::string_view field_name) {
        return {Kind::type_mismatch, "missing required field '" + std::string(field_name) + "'"};
    }

    static basic_error unknown_field(std::string_view field_name) {
        return {Kind::type_mismatch, "unknown field '" + std::string(field_name) + "'"};
    }

    static basic_error duplicate_field(std::string_view field_name) {
        return {Kind::type_mismatch, "duplicate field '" + std::string(field_name) + "'"};
    }

    static basic_error invalid_type(std::string_view expected, std::string_view got) {
        return {Kind::type_mismatch,
                "invalid type: expected " + std::string(expected) + ", got " + std::string(got)};
    }

    static basic_error invalid_length(std::size_t expected, std::size_t got) {
        return {Kind::type_mismatch,
                "invalid length: expected " + std::to_string(expected) + ", got " +
                    std::to_string(got)};
    }

    static basic_error custom(std::string_view msg) {
        return {Kind::type_mismatch, std::string(msg)};
    }

    static basic_error custom(Kind k, std::string_view msg) {
        return {k, std::string(msg)};
    }

    // --- path manipulation ---
    void prepend_field(std::string_view name) {
        path.insert(path.begin(), std::string(name));
    }

    void prepend_index(std::size_t index) {
        path.insert(path.begin(), index);
    }

    // --- formatting ---
    std::string format_path() const {
        std::string result;
        for(std::size_t i = 0; i < path.size(); ++i) {
            if(auto* field = std::get_if<std::string>(&path[i])) {
                if(i > 0 && std::holds_alternative<std::string>(path[i - 1])) {
                    result += '.';
                }
                result += *field;
            } else {
                result += '[';
                result += std::to_string(std::get<std::size_t>(path[i]));
                result += ']';
            }
        }
        return result;
    }

    std::string to_string() const {
        std::string result = message;
        auto p = format_path();
        if(!p.empty()) {
            result += " at ";
            result += p;
        }
        if(location) {
            result += " (line ";
            result += std::to_string(location->line);
            result += ", column ";
            result += std::to_string(location->column);
            result += ')';
        }
        return result;
    }

    // --- comparison (backward compat: error == error_kind::xxx) ---
    friend bool operator==(const basic_error& lhs, Kind rhs) noexcept {
        return lhs.kind == rhs;
    }

    friend bool operator==(Kind lhs, const basic_error& rhs) noexcept {
        return lhs == rhs.kind;
    }

    friend bool operator==(const basic_error& lhs, const basic_error& rhs) noexcept {
        return lhs.kind == rhs.kind;
    }
};

/// ADL error_message for serde_error concept compatibility.
template <typename Kind>
constexpr auto error_message(const basic_error<Kind>& e) -> std::string_view {
    return error_message(e.kind);
}

}  // namespace eventide::serde
