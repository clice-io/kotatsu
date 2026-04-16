#pragma once

#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace kota::codec {

struct source_location {
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t byte_offset = 0;
};

using path_segment = std::variant<std::string, std::size_t>;

/// Rich serde error with lazy allocation. Constructing from Kind is zero-cost (no heap).
/// Message, path, and location are stored behind a unique_ptr, allocated only when needed.
template <typename Kind>
struct serde_error {
    Kind kind;

    constexpr static Kind type_mismatch = Kind::type_mismatch;
    constexpr static Kind number_out_of_range = Kind::number_out_of_range;
    constexpr static Kind invalid_state = Kind::invalid_state;

    serde_error() : kind(Kind::ok) {}

    serde_error(Kind k) : kind(k) {}

    serde_error(Kind k, std::string msg) :
        kind(k), detail(std::make_unique<detail_data>(std::move(msg))) {}

    serde_error(serde_error&&) noexcept = default;
    serde_error& operator=(serde_error&&) noexcept = default;

    serde_error(const serde_error& other) : kind(other.kind) {
        if(other.detail) {
            detail = std::make_unique<detail_data>(*other.detail);
        }
    }

    serde_error& operator=(const serde_error& other) {
        if(this != &other) {
            kind = other.kind;
            detail = other.detail ? std::make_unique<detail_data>(*other.detail) : nullptr;
        }
        return *this;
    }

    static serde_error missing_field(std::string_view field_name) {
        return {Kind::type_mismatch, std::format("missing required field '{}'", field_name)};
    }

    static serde_error unknown_field(std::string_view field_name) {
        return {Kind::type_mismatch, std::format("unknown field '{}'", field_name)};
    }

    static serde_error duplicate_field(std::string_view field_name) {
        return {Kind::type_mismatch, std::format("duplicate field '{}'", field_name)};
    }

    static serde_error invalid_type(std::string_view expected, std::string_view got) {
        return {Kind::type_mismatch,
                std::format("invalid type: expected {}, got {}", expected, got)};
    }

    static serde_error invalid_length(std::size_t expected, std::size_t got) {
        return {Kind::type_mismatch,
                std::format("invalid length: expected {}, got {}", expected, got)};
    }

    static serde_error custom(std::string_view msg) {
        return {Kind::type_mismatch, std::string(msg)};
    }

    static serde_error custom(Kind k, std::string_view msg) {
        return {k, std::string(msg)};
    }

    void prepend_field(std::string_view name) {
        ensure_detail();
        detail->path.insert(detail->path.begin(), std::string(name));
    }

    void prepend_index(std::size_t index) {
        ensure_detail();
        detail->path.insert(detail->path.begin(), index);
    }

    std::optional<source_location> location() const {
        return detail ? detail->location : std::nullopt;
    }

    void set_location(source_location loc) {
        ensure_detail();
        detail->location = loc;
    }

    std::string_view message() const {
        return detail ? std::string_view(detail->message) : error_message(kind);
    }

    std::string format_path() const {
        if(!detail) {
            return {};
        }
        std::string result;
        for(std::size_t i = 0; i < detail->path.size(); ++i) {
            if(auto* field = std::get_if<std::string>(&detail->path[i])) {
                if(i > 0) {
                    result += '.';
                }
                result += *field;
            } else {
                result += '[';
                result += std::to_string(std::get<std::size_t>(detail->path[i]));
                result += ']';
            }
        }
        return result;
    }

    std::string to_string() const {
        std::string result(message());
        auto p = format_path();
        if(!p.empty()) {
            result += " at ";
            result += p;
        }
        auto loc = location();
        if(loc) {
            result += std::format(" (line {}, column {})", loc->line, loc->column);
        }
        return result;
    }

    bool operator==(const serde_error& rhs) const noexcept {
        return kind == rhs.kind;
    }

    bool operator==(Kind rhs) const noexcept {
        return kind == rhs;
    }

private:
    struct detail_data {
        std::string message;
        std::vector<path_segment> path;
        std::optional<source_location> location;

        detail_data() = default;

        explicit detail_data(std::string msg) : message(std::move(msg)) {}
    };

    std::unique_ptr<detail_data> detail;

    void ensure_detail() {
        if(!detail) {
            detail = std::make_unique<detail_data>(std::string(error_message(kind)));
        }
    }
};

}  // namespace kota::codec
