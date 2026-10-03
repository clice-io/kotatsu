#pragma once

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
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
    /// Path from root to error site, built by prepend_field/prepend_index during stack unwinding.
    /// Decoding a keyed document (json, toml, dyn), a member is named by its
    /// key as the document spells it; otherwise a field is named by its name,
    /// and an element or a map entry, encoding too, by its index.
    std::vector<path_segment> path;
    /// Source position in the input document (e.g. TOML line/column).
    std::optional<source_location> location;

    rich_error() = default;

    explicit rich_error(std::string msg) : message(std::move(msg)) {}

    explicit operator bool() const {
        return !message.empty();
    }

    void prepend_field(std::string_view name) {
        path.insert(path.begin(), std::string(name));
    }

    void prepend_index(std::size_t idx) {
        path.insert(path.begin(), idx);
    }

    /// Formats path as "foo.bar[3].baz".
    std::string format_path() const {
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

/// The keys that the decodes on this thread pass over while it is installed
/// (scoped_context<UnknownFields>): object keys no field of the struct they
/// are read into answers to, and the keys of an adjacently tagged object
/// other than its tag and content. Each is the error a decode under
/// deny_unknown_fields would fail with: "unknown field 'name'", the path of
/// the object holding the key, as the document spells it, and where the key
/// is, for a backend that knows (json, toml). Decoding goes on as it would
/// without the sink, so one decode reads the value and reports every key it
/// passed over; where unknown fields are denied, the first still fails the
/// decode and is not reported. An untagged probe that fails takes back what
/// it reported; a decode that fails keeps what it reported before.
struct UnknownFields {
    /// In the order the decodes met the keys.
    std::vector<rich_error> entries;
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
