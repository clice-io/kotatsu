#pragma once

#include <cstddef>
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

void set_update_snapshots(bool enabled);

void set_snapshot_dir(std::string_view dir);

/// Removes the snapshot files under the directories this run checked
/// snapshots in that it did not check.
std::size_t cleanup_unused_snapshots();

/// Checks `text` against the running test's snapshot `name`, or its one
/// unnamed snapshot; returns the report if the check fails.
std::optional<std::string> check_snapshot(std::string_view text,
                                          std::string_view name,
                                          std::source_location location);

std::optional<std::string>
    check_snapshot_glob(std::string_view dir,
                        std::string_view pattern,
                        const std::function<std::string(std::string_view)>& transform,
                        std::source_location location);

/// A check that held if it reported nothing.
Match snapshot_match(std::optional<std::string> report);

}  // namespace detail

/// `value` matches the running test's snapshot `name`, which is the file
/// `<suite>/<test>/<name>.snap.yml`, or with no name its one unnamed snapshot,
/// `<suite>/<test>.snap.yml`. Text is kept as it is; any other value as the
/// debug codec renders it, over several lines.
template <typename T>
Match snapshot(const T& value,
               std::string_view name = {},
               std::source_location location = std::source_location::current()) {
    if constexpr(meta::str_like<T>) {
        auto text = detail::as_text(value);
        if(!text) {
            return detail::snapshot_match("got a null pointer, not text");
        }
        return detail::snapshot_match(detail::check_snapshot(*text, name, location));
    } else {
        auto text = codec::debug::to_string(value, true);
        if(!text) {
            return detail::snapshot_match(
                std::format("cannot render the value: {}", text.error().to_string()));
        }
        return detail::snapshot_match(detail::check_snapshot(*text, name, location));
    }
}

/// Each file under `dir` whose path relative to it matches the glob `pattern`
/// matches the running test's snapshot `<suite>/<test>/<relative path>.snap.yml`
/// of `transform(path)`, `path` being `dir` joined with the relative path. At
/// least one file must match.
inline Match snapshot_glob(std::string_view dir,
                           std::string_view pattern,
                           const std::function<std::string(std::string_view)>& transform,
                           std::source_location location = std::source_location::current()) {
    return detail::snapshot_match(detail::check_snapshot_glob(dir, pattern, transform, location));
}

}  // namespace kota::zest
