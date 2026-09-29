#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "kota/support/small_vector.h"

namespace kota::option {

/// Classification of how an option consumes its arguments.
enum class Kind : uint8_t {
    /// A group of related options (not a real option itself).
    Group,

    /// A positional input (e.g. a filename).
    Input,

    /// An unrecognised option.
    Unknown,

    /// A flag that takes no value, e.g. `-v`.
    Flag,

    /// Value is joined to the option name, e.g. `-I/usr/include`.
    Joined,

    /// One or more separate values, e.g. `-o file`.
    Values,

    /// Exactly one separate value, e.g. `-o file`.
    Separate,

    /// Comma-separated values joined to the name, e.g. `-Wl,--gc-sections`.
    CommaJoined,

    /// A fixed number of separate values (see num_args).
    MultiArg,

    /// Value may be joined or separate, e.g. `-I/usr/include` or `-I /usr/include`.
    JoinedOrSeparate,

    /// Value is joined, plus one additional separate value.
    JoinedAndSeparate,

    /// All remaining arguments are values, e.g. `-- ...`.
    RemainingArgs,

    /// Like RemainingArgs, but the first value may be joined.
    RemainingArgsJoined,
};

/// How an option and its values are rendered back into a command line.
enum class RenderStyle : uint8_t {
    /// Comma-separated values joined to the name, e.g. `-Wl,--gc-sections,--strip`.
    CommaJoined,

    /// Value is joined directly to the option name, e.g. `-I/usr/include`.
    Joined,

    /// Value is a separate argv element, e.g. `-o file`.
    Separate,

    /// Render as bare values without the option name.
    Values,
};

/// Base flags for all options. Custom flags may be added after these bits.
enum DriverFlag {
    HelpHidden = (1 << 0),
    RenderAsInput = (1 << 1),
    RenderJoined = (1 << 2),
    RenderSeparate = (1 << 3)
};

enum DriverVisibility {
    DefaultVis = (1 << 0),
};

/// Describes a parse failure: which argv index failed and why.
struct ParseError {
    /// Index into argv where the error occurred.
    std::uint32_t index;

    /// Human-readable description of the failure.
    const char* message;
};

/// A single parsed argument. Values are non-owning views into the argv data
/// passed to parse().
struct ParsedArg {
    /// The option ID that matched, corresponding to Option::id.
    std::uint32_t id = 0;

    /// Index of this argument in the original argv.
    std::uint32_t index = 0;

    /// Index of the next unconsumed argv element after this argument.
    std::uint32_t next_index = 0;

    /// The spelling of the argument as it appeared on the command line,
    /// e.g. "-I", "--optimize". May be a copy when grouped short options
    /// are expanded.
    std::string spelling;

    /// The values associated with the argument, e.g. for `-I/usr/include`
    /// the value is "/usr/include".
    kota::small_vector<std::string_view, 2> values;

    void add_value(std::string_view v) {
        values.push_back(v);
    }

    void clear() {
        id = 0;
        index = 0;
        next_index = 0;
        values.clear();
        spelling.clear();
    }
};

namespace detail {

constexpr inline std::string_view dash_prefixes[] = {"-"};
constexpr inline std::string_view dash_double_prefixes[] = {"-", "--"};
constexpr inline std::string_view double_prefixes[] = {"--"};
constexpr inline std::string_view all_prefixes[] = {"--", "/", "-"};
constexpr inline std::string_view slash_dash_prefixes[] = {"/", "-"};

}  // namespace detail

/// The prefixes an option accepts, the first being the one it is written with.
constexpr inline auto pfx_none = std::span<const std::string_view>();
constexpr inline auto pfx_dash = std::span<const std::string_view>(detail::dash_prefixes);
constexpr inline auto pfx_dash_double =
    std::span<const std::string_view>(detail::dash_double_prefixes);
constexpr inline auto pfx_double = std::span<const std::string_view>(detail::double_prefixes);
constexpr inline auto pfx_all = std::span<const std::string_view>(detail::all_prefixes);
constexpr inline auto pfx_slash_dash =
    std::span<const std::string_view>(detail::slash_dash_prefixes);

/// Type-erased reference to a contiguous range of string-like arguments.
/// Non-owning — the underlying range must outlive this object.
class ArgsRef {
public:
    ArgsRef() = default;

    ArgsRef(const void* data,
            std::uint32_t size,
            std::string_view (*access)(const void*, std::uint32_t)) :
        data(data), count(size), access(access) {}

    template <typename Range>
        requires (!std::is_same_v<Range, ArgsRef>)
    ArgsRef(const Range& range) :
        data(range.data()), count(static_cast<std::uint32_t>(range.size())),
        access([](const void* d, std::uint32_t i) -> std::string_view {
            using Elem = typename Range::value_type;
            return static_cast<const Elem*>(d)[i];
        }) {}

    std::string_view operator[](std::uint32_t i) const {
        return access(data, i);
    }

    std::uint32_t size() const {
        return count;
    }

private:
    const void* data = nullptr;
    std::uint32_t count = 0;
    std::string_view (*access)(const void*, std::uint32_t) = nullptr;
};

}  // namespace kota::option
