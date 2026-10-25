#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <string_view>

#include "kota/support/string_buffer.h"

/// Paths as text: UTF-8 (WTF-8 on Windows) strings taken apart, joined and
/// normalized without asking the operating system. Nothing here reads the
/// file system, the current directory or the environment, and each function
/// takes the style its path is spelled in, so a Windows path is handled the
/// same on any host. A trailing separator is no component: `a/b/` names `b`,
/// whose parent is `a`.
namespace kota::fs::path {

/// How a path is spelled.
enum class Style : std::uint8_t {
    /// `/` separates; nothing else is special, `\` and `:` included.
    Posix,
    /// `/` and `\` separate, but only `\` after a verbatim `\\?\` prefix; a
    /// drive, a UNC share, a device or a verbatim prefix may start the root.
    Windows,
};

/// The style of the host's paths.
#ifdef _WIN32
constexpr inline Style native_style = Style::Windows;
#else
constexpr inline Style native_style = Style::Posix;
#endif

/// What a Windows path starts with before its root directory.
enum class Prefix : std::uint8_t {
    /// Nothing: every POSIX path, and on Windows `\x` and `x`.
    None,
    /// `C:`
    Drive,
    /// `\\server\share`, or `\\server` alone.
    UNC,
    /// `\\.\device`, separators either way.
    Device,
    /// `\\?\C:`
    VerbatimDrive,
    /// `\\?\UNC\server\share`
    VerbatimUNC,
    /// `\\?\` followed by anything else, such as `\\?\Volume{...}`.
    Verbatim,
};

/// The root a path starts with, as written: views of the path.
struct Root {
    Prefix prefix = Prefix::None;

    /// The prefix: `C:`, `\\server\share`, `\\?\UNC\server\share`; empty
    /// when there is none.
    std::string_view name;

    /// The separators after the prefix: `/` in `/x`, `\` in `C:\x`; empty
    /// in `C:x` and `x`, and in `\\server\share`, whose root is implied.
    std::string_view directory;
};

/// Whether `c` separates components.
bool is_separator(char c, Style style = native_style);

/// The root `p` starts with: its prefix and the separators after it.
Root root(std::string_view p, Style style = native_style);

/// Whether `p` names the same file whatever the current directory: on
/// Windows a drive with a root directory (`C:\x`), or a UNC, device or
/// verbatim prefix. `\x` and `C:x` depend on the current drive or its
/// directory, and are not.
bool is_absolute(std::string_view p, Style style = native_style);

/// Whether `p` starts at a root directory, so no directory can be put in
/// front of it: absolute, or on Windows rooted on the current drive (`\x`).
bool has_root(std::string_view p, Style style = native_style);

/// The names after a path's root, `.` and `..` among them, as views of the
/// path; repeated and trailing separators make none. A view, which reads
/// the path as it goes.
class Components : public std::ranges::view_interface<Components> {
public:
    class iterator {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        std::string_view operator*() const {
            return path.substr(start, end - start);
        }

        iterator& operator++();

        iterator operator++(int) {
            auto old = *this;
            ++*this;
            return old;
        }

        friend bool operator==(const iterator& a, const iterator& b) {
            return a.start == b.start;
        }

        friend bool operator==(const iterator& it, std::default_sentinel_t) {
            return it.start == it.path.size();
        }

    private:
        friend class Components;

        iterator(std::string_view path, Style style, Prefix prefix, std::size_t at);

        std::string_view path;

        Style style = native_style;

        Prefix prefix = Prefix::None;

        /// Where the name starts and ends: the path's size at the end.
        std::size_t start = 0;

        std::size_t end = 0;
    };

    Components() = default;

    Components(std::string_view path, Style style) : path(path), style(style) {}

    iterator begin() const;

    std::default_sentinel_t end() const {
        return {};
    }

private:
    std::string_view path;

    Style style = native_style;
};

/// The components of `p`.
inline Components components(std::string_view p, Style style = native_style) {
    return {p, style};
}

/// The last name: `b` of `a/b` and of `a/b/`; empty when there is none, as
/// in `/` and `C:`.
std::string_view filename(std::string_view p, Style style = native_style);

/// `p` without its last name and the separators before it, its root kept:
/// `a` of `a/b/`, `/` of `/a`, `C:` of `C:x`. Empty for a path of one
/// relative name (`a`) and for one with no name (`/`): neither has a parent
/// to walk up to.
std::string_view parent(std::string_view p, Style style = native_style);

/// The filename up to its extension: `archive.tar` of `archive.tar.gz`; a
/// filename starting with its only dot, such as `.bashrc`, and `.` and `..`
/// are all stem.
std::string_view stem(std::string_view p, Style style = native_style);

/// The filename's extension from its last dot on: `.gz` of `archive.tar.gz`,
/// `.` of `name.`; empty when it has none.
std::string_view extension(std::string_view p, Style style = native_style);

/// `p`, then each parent of it in turn, as parent() gives them, while there
/// is one: `a/b/c`, `a/b`, `a`; `/a`, `/`. A view, as components() is.
class Ancestors : public std::ranges::view_interface<Ancestors> {
public:
    class iterator {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        std::string_view operator*() const {
            return path;
        }

        iterator& operator++() {
            path = parent(path, style);
            return *this;
        }

        iterator operator++(int) {
            auto old = *this;
            ++*this;
            return old;
        }

        friend bool operator==(const iterator& a, const iterator& b) {
            return a.path.size() == b.path.size();
        }

        friend bool operator==(const iterator& it, std::default_sentinel_t) {
            return it.path.empty();
        }

    private:
        friend class Ancestors;

        iterator(std::string_view path, Style style) : path(path), style(style) {}

        std::string_view path;

        Style style = native_style;
    };

    Ancestors() = default;

    Ancestors(std::string_view path, Style style) : path(path), style(style) {}

    iterator begin() const {
        return {path, style};
    }

    std::default_sentinel_t end() const {
        return {};
    }

private:
    std::string_view path;

    Style style = native_style;
};

/// The ancestors of `p`, itself first.
inline Ancestors ancestors(std::string_view p, Style style = native_style) {
    return {p, style};
}

// The functions below write a path into a buffer the caller gives, of any
// type (std::string, small_string<N>, llvm::SmallString<N>): one buffer may
// serve a loop of calls without allocating. Those that change a path change
// it in place, as Rust's PathBuf does; a string_view they take must not view
// the buffer they write.

/// Joins `p` onto `path`. An absolute `p`, and on Windows one with another
/// prefix (`D:x` onto `C:\a`), takes its place; a rooted `\x` keeps its
/// prefix; anything else goes after it, behind the separator it already
/// uses, or `p` uses, or `/`.
void push(StringBufferRef path, std::string_view p, Style style = native_style);

/// Cuts `path` to its parent(); false, leaving it as it is, when it has
/// none.
bool pop(StringBufferRef path, Style style = native_style);

/// Replaces the filename of `path` by `name`, or joins `name` onto a path
/// without one, as push() does.
void set_file_name(StringBufferRef path, std::string_view name, Style style = native_style);

/// Replaces the extension of `path`'s filename by `ext`, which gains a
/// leading dot when it has none; an empty `ext` removes the extension.
/// False, leaving it as it is, for a path without a filename.
bool set_extension(StringBufferRef path, std::string_view ext, Style style = native_style);

/// What normalize() does with `..`.
enum class DotDot : std::uint8_t {
    /// Removes it with the name before it, as if no name were a symlink.
    Fold,
    /// Keeps it, so the path still names what the OS would reach.
    Keep,
};

/// Rewrites `path` without `.` components and repeated or trailing
/// separators, with the first separator it uses (only `\` after a verbatim
/// prefix); `..` folded or kept as `dot_dot` says. A `..` never climbs past a
/// root, a UNC share included, but stays in a relative path and after a bare
/// drive (`C:..`), whose directory is unknown. A path left without a
/// component is `.`; an empty one stays empty.
void normalize(StringBufferRef path, DotDot dot_dot = DotDot::Fold, Style style = native_style);

/// Writes into `out` the relative path that leads from `base` to `p`, both
/// taken as written (normalize() them first): `../c` for `a/c` from `a/b`,
/// `.` for `p` itself. False, leaving `out` as it is, when the path cannot be
/// told: the roots differ (prefixes compare in either case), or `base` has a
/// `..` past the part the two share, which climbs out of a directory whose
/// name neither spells.
bool relative(StringBufferRef out,
              std::string_view p,
              std::string_view base,
              Style style = native_style);

/// The rest of `p` after the components of `prefix`, without the separators
/// before it: `c/d` of `a/b/c/d` under `a/b`, empty for `a/b` itself; none
/// when `p` is not under `prefix`. Components compare whole and as written,
/// roots as relative() compares them.
std::optional<std::string_view> strip_prefix(std::string_view p,
                                             std::string_view prefix,
                                             Style style = native_style);

/// Whether `p` is `ancestor` or lies under it, component by component.
bool is_within(std::string_view p, std::string_view ancestor, Style style = native_style);

/// Turns each `\` of `path` into `/` where `\` separates: in a Windows
/// path that is not verbatim. Any other path stays as it is.
void to_slash(StringBufferRef path, Style style = native_style);

/// Turns each `/` of a Windows `path` into `\`, the way Windows programs
/// print paths; a verbatim one, where `/` is a name's character, stays as it
/// is.
void to_backslash(StringBufferRef path);

}  // namespace kota::fs::path
