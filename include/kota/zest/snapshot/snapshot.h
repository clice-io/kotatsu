#pragma once

#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

#include "kota/zest/assert/check.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/debug/encode.h"

// Snapshot predicates, e.g. `ZEXPECT(zest::snapshot(render(document)))`: the
// value is compared with a file under --snapshot-dir named after the running
// test. A snapshot that is missing is written and the check passes; one that
// differs fails it, leaving the new value beside the file in `<file>.new`,
// unless --update-snapshots rewrites the file instead.

namespace kota::zest {

namespace detail {

/// Names the snapshots of the test about to run. Snapshots are the process's,
/// like the test's state, so the test's other threads take them too.
void reset_snapshot_context(std::string_view suite, std::string_view test);

/// Turns --update-snapshots on or off; returns what it was.
bool set_update_snapshots(bool enabled);

void set_snapshot_dir(std::string_view dir);

/// Removes the snapshot files under the directories this run checked
/// snapshots in that it did not check.
std::size_t cleanup_unused_snapshots();

/// Checks `text` against the running test's snapshot `name`, or its one
/// unnamed snapshot.
Match check_snapshot(std::string_view text, std::string_view name, std::source_location location);

/// What `value` is snapshotted as, or why it cannot be.
template <typename T>
std::expected<std::string, std::string> snapshot_text(const T& value) {
    if constexpr(meta::str_like<T>) {
        if(auto text = as_text(value)) {
            return std::string(*text);
        }
        return std::unexpected("got a null pointer, not text");
    } else {
        auto text = codec::debug::to_string(value, true);
        if(!text) {
            return std::unexpected(
                std::format("cannot render the value: {}", text.error().to_string()));
        }
        return std::move(*text);
    }
}

}  // namespace detail

/// `value` matches the running test's snapshot `name`, which is the file
/// `<suite>/<test>/<name>.snap.yml`, or with no name its one unnamed snapshot,
/// `<suite>/<test>.snap.yml`. Text is kept as it is; any other value as the
/// debug codec renders it, over several lines.
template <typename T>
Match snapshot(const T& value,
               std::string_view name = {},
               std::source_location location = std::source_location::current()) {
    auto text = detail::snapshot_text(value);
    if(!text) {
        return Match{.held = false, .explain = [error = std::move(text.error())] { return error; }};
    }
    return detail::check_snapshot(*text, name, location);
}

/// Each file under `dir` whose path relative to it matches the glob `pattern`
/// matches the running test's snapshot `<suite>/<test>/<relative path>.snap.yml`
/// of `transform(path)`, `path` being `dir` joined with the relative path. At
/// least one file must match.
Match snapshot_glob(std::string_view dir,
                    std::string_view pattern,
                    const std::function<std::string(std::string_view)>& transform,
                    std::source_location location = std::source_location::current());

}  // namespace kota::zest
