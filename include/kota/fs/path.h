#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

/// The names after the root, `.` and `..` among them; repeated and trailing
/// separators make none.
std::vector<std::string_view> components(std::string_view p, Style style = native_style);

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

/// `p` with its extension replaced by `ext`, which gains a leading dot when
/// it has none; an empty `ext` removes the extension.
std::string replace_extension(std::string_view p, std::string_view ext, Style style = native_style);

/// `p` taken relative to `base`. An absolute `p`, and on Windows one with
/// another prefix (`D:x` onto `C:\a`), stands alone; a rooted `\x` keeps
/// the base's prefix; anything else goes after the base, behind the
/// separator the base already uses, or `p` uses, or `/`.
std::string join(std::string_view base, std::string_view p, Style style = native_style);

/// The parts joined left to right.
std::string join(std::initializer_list<std::string_view> parts, Style style = native_style);

/// What normalize() does with `..`.
enum class DotDot : std::uint8_t {
    /// Removes it with the name before it, as if no name were a symlink.
    Fold,
    /// Keeps it, so the path still names what the OS would reach.
    Keep,
};

/// `p` without `.` components and repeated or trailing separators, written
/// with the first separator `p` uses (only `\` after a verbatim prefix);
/// `..` folded or kept as `dot_dot` says. A `..` never climbs past a root,
/// a UNC share included, but stays in a relative path and after a bare
/// drive (`C:..`), whose directory is unknown. A path left without a
/// component is `.`; an empty one stays empty.
std::string normalize(std::string_view p,
                      DotDot dot_dot = DotDot::Fold,
                      Style style = native_style);

/// The relative path that leads from `base` to `p`, both taken as written
/// (normalize() them first): `../c` for `a/c` from `a/b`, `.` for `p`
/// itself. None when the path cannot be told: the roots differ (prefixes
/// compare in either case), or `base` has a `..` past the part the two
/// share, which climbs out of a directory whose name neither spells.
std::optional<std::string> relative(std::string_view p,
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

/// `p` with each `\` turned into `/` where `\` separates: a Windows path
/// that is not verbatim. Any other path comes back as it is.
std::string to_slash(std::string_view p, Style style = native_style);

/// A Windows path with each `/` turned into `\`, the way Windows programs
/// print paths; a verbatim one, where `/` is a name's character, comes back
/// as it is.
std::string to_backslash(std::string_view p);

}  // namespace kota::fs::path
