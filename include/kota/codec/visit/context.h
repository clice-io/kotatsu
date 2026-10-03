#pragma once

#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace kota::codec {

/// All visitor methods return bool (success/failure). Detailed error info and user context are
/// propagated via thread_local sideband channels (error_sink, scoped_context), avoiding the
/// overhead of passing std::expected through every call in the hot path.
struct rich_error {
    struct source_location {
        std::size_t line = 0;
        std::size_t column = 0;
        std::size_t byte_offset = 0;
    };

    /// Field name (string) or array index (size_t).
    using path_segment = std::variant<std::string, std::size_t>;

    std::string message;
    /// Path from root to error site, built by prepend_segment during stack unwinding.
    std::vector<path_segment> path;
    /// Source position in the input document (e.g. TOML line/column).
    std::optional<source_location> location;

    rich_error() = default;

    explicit rich_error(std::string msg) : message(std::move(msg)) {}

    explicit operator bool() const {
        return !message.empty();
    }

    /// Formats path as "foo.bar[3].baz".
    std::string format_path() const {
        return format_path(path);
    }

    /// Puts a step, a field name or an element index, in front of a path
    /// from the root, as each frame of a decode does on its way out.
    template <typename Step>
    static void prepend_segment(std::vector<path_segment>& path, const Step& at) {
        if constexpr(std::is_convertible_v<const Step&, std::string_view>) {
            path.emplace(path.begin(), std::in_place_type<std::string>, std::string_view(at));
        } else {
            path.emplace(path.begin(), std::in_place_type<std::size_t>, at);
        }
    }

    /// Formats a path from the root as "foo.bar[3].baz".
    static std::string format_path(std::span<const path_segment> path) {
        std::string result;
        for(std::size_t i = 0; i < path.size(); ++i) {
            if(auto* field = std::get_if<std::string>(&path[i])) {
                if(i > 0) {
                    result += '.';
                }
                result += *field;
            } else {
                result += std::format("[{}]", std::get<std::size_t>(path[i]));
            }
        }
        return result;
    }

    /// Formats as "message at path (line X, column Y)".
    std::string to_string() const {
        if(message.empty()) {
            return {};
        }
        std::string result = message;
        auto p = format_path();
        if(!p.empty()) {
            result += " at ";
            result += p;
        }
        if(location) {
            result += std::format(" (line {}, column {})", location->line, location->column);
        }
        return result;
    }

    static rich_error missing_field(std::string_view name) {
        return rich_error(std::format("missing required field '{}'", name));
    }

    static rich_error unknown_field(std::string_view name) {
        return rich_error(std::format("unknown field '{}'", name));
    }

    static rich_error invalid_type(std::string_view expected, std::string_view got) {
        return rich_error(std::format("invalid type: expected {}, got {}", expected, got));
    }
};

/// The unknown fields of the decodes run on this thread while it is
/// installed (scoped_context<UnknownFields>): the keys of an object that no
/// field of the struct read from it answers to. Decoding goes on as without
/// it, so one decode both reads the value and reports every key it passed
/// over. A struct that denies unknown fields still fails on the first, which
/// is not collected, and a decode that fails keeps what it collected before.
struct UnknownFields {
    struct Entry {
        /// From the root to the key, the key last, in the segments of a
        /// decode error's path: "section.key", "rules[0].key".
        std::vector<rich_error::path_segment> path;
        /// Where the key's value starts in the input, when the backend knows
        /// (json, toml).
        std::optional<rich_error::source_location> location;

        std::string format_path() const {
            return rich_error::format_path(path);
        }
    };

    /// In the order the decodes met them.
    std::vector<Entry> entries;
};

/// RAII thread_local context slot. Each type T gets an independent thread_local pointer.
/// Used directly for user context (e.g. scoped_context<EntityRegistry>), and internally by
/// error_sink for error propagation.
template <typename T>
class scoped_context {
    inline static thread_local T* active = nullptr;
    T* prev;

public:
    explicit scoped_context(T& ctx) : prev(active) {
        active = &ctx;
    }

    ~scoped_context() {
        active = prev;
    }

    scoped_context(const scoped_context&) = delete;
    scoped_context& operator=(const scoped_context&) = delete;

    static T* try_current() {
        return active;
    }

    /// Writes value to current slot and returns false, for one-liner `return
    /// scoped_context<E>::fail(...)`.
    static bool fail(T value) {
        if(active)
            *active = std::move(value);
        return false;
    }
};

}  // namespace kota::codec

#define KOTA_CODEC_TRY(expr)                                                                       \
    do {                                                                                           \
        if(!(expr))                                                                                \
            return false;                                                                          \
    } while(0)
