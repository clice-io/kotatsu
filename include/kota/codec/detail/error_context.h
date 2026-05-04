#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kota/codec/detail/error.h"

namespace kota::codec::detail {

/// Backend-agnostic error context that collects error info (message, path, location)
/// and can transfer it into a typed serde_error<Kind>.
struct error_context {
    void set_message(std::string msg) {
        message_ = std::move(msg);
        has_error_ = true;
    }

    void clear() {
        message_.clear();
        path_.clear();
        location_.reset();
        has_error_ = false;
    }

    bool has_error() const {
        return has_error_;
    }

    void prepend_field(std::string_view name) {
        path_.insert(path_.begin(), std::string(name));
        has_error_ = true;
    }

    void prepend_index(std::size_t index) {
        path_.insert(path_.begin(), index);
        has_error_ = true;
    }

    void set_location(source_location loc) {
        location_ = loc;
    }

    /// Transfer collected error info into a serde_error<Kind>, then clear the context.
    /// Uses the public API of serde_error (constructor + prepend_field/prepend_index/set_location).
    template <typename Kind>
    serde_error<Kind> take_as(Kind kind) {
        if(!has_error_) {
            return serde_error<Kind>(kind);
        }

        // Only construct with message if one was set; otherwise let the
        // serde_error fall back to the default message for the error kind.
        serde_error<Kind> err = message_.empty() ? serde_error<Kind>(kind)
                                                 : serde_error<Kind>(kind, std::move(message_));

        // Path is stored root→leaf. prepend_field/prepend_index insert at front,
        // so iterate in reverse to reconstruct the correct order.
        for(auto it = path_.rbegin(); it != path_.rend(); ++it) {
            if(auto* field = std::get_if<std::string>(&*it)) {
                err.prepend_field(*field);
            } else {
                err.prepend_index(std::get<std::size_t>(*it));
            }
        }

        if(location_) {
            err.set_location(*location_);
        }

        clear();
        return err;
    }

private:
    std::string message_;
    std::vector<path_segment> path_;
    std::optional<source_location> location_;
    bool has_error_ = false;
};

/// Thread-local accessor for the shared error context.
inline error_context& thread_error_context() {
    thread_local error_context ctx;
    return ctx;
}

}  // namespace kota::codec::detail
