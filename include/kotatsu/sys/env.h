#pragma once

#include <cstddef>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "kotatsu/support/buffer_ref.h"
#include "kotatsu/support/small_string.h"
#include "kotatsu/support/string_list_ref.h"
#include "kotatsu/fs/path.h"

// The environment of this process. On Windows it is the process's own block
// (GetEnvironmentVariableW and the like), never the copy the CRT's getenv and
// _putenv keep, which a change through one does not reach. On POSIX the
// functions here hold a lock between them, which code calling getenv and
// setenv directly does not take.

namespace kotatsu::sys {

/// Writes into `out` the value of variable `name`: false, leaving `out` as
/// it is, when there is none, or `name` is no variable's name.
bool get_env(buffer_ref out, std::string_view name);

/// Sets variable `name` to `value`. invalid_argument for a name that is
/// empty, holds a NUL or holds '=' (on Windows past its first character, as
/// the `=C:` variables do), and for a value holding a NUL.
std::error_code set_env(std::string_view name, std::string_view value);

/// Removes variable `name`; nothing when there is none. invalid_argument for
/// a name set_env refuses.
std::error_code unset_env(std::string_view name);

/// A variable of an environment.
struct EnvVar {
    std::string name;

    std::string value;

    friend bool operator==(const EnvVar&, const EnvVar&) = default;
};

/// Every variable. On Windows the `=C:` variables, the current directory of
/// each drive, are among them, their names starting with '='.
std::vector<EnvVar> environment();

/// Whether two variable names are the same: as Windows compares them,
/// ignoring case, on Windows; byte for byte elsewhere.
bool env_name_equal(std::string_view left, std::string_view right);

/// The entries of a list of paths such as PATH, as split_paths() gives
/// them: a view that reads the list as it goes, each entry a view of the
/// list, or of the iterator for a Windows entry its quotes are left out of,
/// which holds until that iterator moves on or goes.
class split_paths_view : public std::ranges::view_interface<split_paths_view> {
public:
    class iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        std::string_view operator*() const {
            return quoted ? std::string_view(unquoted) : entry;
        }

        iterator& operator++();

        void operator++(int) {
            ++*this;
        }

        friend bool operator==(const iterator& it, std::default_sentinel_t) {
            return it.done;
        }

    private:
        friend class split_paths_view;

        iterator(std::string_view list, fs::path::Style style);

        /// What is left of the list after the entry.
        std::string_view rest;

        fs::path::Style style = fs::path::native_style;

        /// The entry as the list has it.
        std::string_view entry;

        /// A Windows entry that had quotes, without them.
        small_string<256> unquoted;

        /// Whether the entry had quotes: it is `unquoted`.
        bool quoted = false;

        /// Whether the entry is past the last.
        bool done = false;

        /// Whether no entry is left after this one.
        bool last = false;
    };

    split_paths_view() = default;

    split_paths_view(std::string_view list, fs::path::Style style) : list(list), style(style) {}

    iterator begin() const {
        return {list, style};
    }

    std::default_sentinel_t end() const {
        return {};
    }

private:
    std::string_view list;

    fs::path::Style style = fs::path::native_style;
};

/// The entries of a list of paths such as PATH: separated by ':' in the
/// POSIX style, by ';' in the Windows style, where a '"' opens or closes a
/// part in which ';' separates nothing and is itself left out. Empty entries
/// are kept: POSIX reads them as the current directory. An empty list has
/// one empty entry in the POSIX style and none in the Windows style.
inline split_paths_view split_paths(std::string_view list,
                                    fs::path::Style style = fs::path::native_style) {
    return {list, style};
}

/// Writes into `out` the list split_paths reads back as `paths`: in the
/// Windows style an entry holding ';' is quoted, and so is a lone empty one.
/// In the POSIX style no list has no entries: none joins to "", which reads
/// back as one empty entry. invalid_argument for an entry holding ':' in the
/// POSIX style, or '"' in the Windows style, which no list can hold.
std::error_code join_paths(buffer_ref out,
                           string_list_ref paths,
                           fs::path::Style style = fs::path::native_style);

}  // namespace kotatsu::sys
