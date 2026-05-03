#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "kota/codec/json/error.h"

namespace kota::codec::json::detail_v2 {

/// Thread-local error context for carrying rich error information from the
/// visitor-based deserialization path (which uses simdjson::error_code) back
/// to from_json (which constructs serde_error).
struct error_context {
    std::optional<json::error> pending;

    void set(json::error err) {
        pending = std::move(err);
    }

    void clear() {
        pending.reset();
    }

    std::optional<json::error> take() {
        auto result = std::move(pending);
        pending.reset();
        return result;
    }

    void prepend_field(std::string_view name) {
        ensure_pending();
        pending->prepend_field(name);
    }

    void prepend_index(std::size_t index) {
        ensure_pending();
        pending->prepend_index(index);
    }

private:
    void ensure_pending() {
        if(!pending) {
            pending = json::error(error_kind::type_mismatch);
        }
    }
};

inline error_context& thread_error_context() {
    thread_local error_context ctx;
    return ctx;
}

}  // namespace kota::codec::json::detail_v2
