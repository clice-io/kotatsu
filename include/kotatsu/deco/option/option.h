#pragma once

#include <optional>
#include <ostream>
#include <span>
#include <string_view>

#include "arg.h"

namespace kotatsu::option {

struct OptTable;

/// Static definition of a command-line option. Constexpr-friendly POD used in
/// compile-time option tables. Use the factory methods to construct entries.
struct Option {
    /// The set of accepted prefixes, e.g. {"--", "-"}.
    std::span<const std::string_view> prefixes;

    /// Option name with its first prefix (prefixes[0]), e.g. "--optimize".
    std::string_view prefixed_name;

    /// Unique identifier (1-based index into the option table).
    std::uint32_t id;

    /// How this option consumes arguments.
    Kind kind;

    /// ID of the group this option belongs to (0 = no group).
    unsigned short group_id;

    /// ID of the option this is an alias for (0 = not an alias).
    unsigned short alias_id;

    /// Extra arguments appended when resolving this alias (\0-separated).
    const char* alias_args;

    /// Bitmask of DriverFlag values.
    std::uint32_t flags;

    /// Bitmask of DriverVisibility values.
    std::uint32_t visibility;

    /// Number of separate values this option takes (for MultiArg).
    unsigned char num_args;

    /// Help text shown in usage output.
    const char* help_text;

    /// Placeholder name for the value in help text, e.g. "<value>".
    const char* meta_var;

    bool has_no_prefix() const {
        return this->prefixes.size() == 0;
    }

    std::string_view name() const {
        std::uint32_t prefix_length =
            this->has_no_prefix() ? 0 : static_cast<std::uint32_t>(this->prefixes[0].size());
        return this->prefixed_name.substr(prefix_length);
    }

    constexpr static Option unaliased_one(std::span<const std::string_view> prefixes,
                                          std::string_view prefixed_name,
                                          std::uint32_t id,
                                          Kind kind,
                                          unsigned char num_args,
                                          const char* help_text = "no help text",
                                          const char* meta_var = "<nullptr>",
                                          unsigned short group_id = 0,
                                          std::uint32_t flags = 0,
                                          std::uint32_t visibility = 1 << 0) {
        return Option{
            .prefixes = prefixes,
            .prefixed_name = prefixed_name,
            .id = id,
            .kind = kind,
            .group_id = group_id,
            .alias_id = 0,
            .alias_args = nullptr,
            .flags = flags,
            .visibility = visibility,
            .num_args = num_args,
            .help_text = help_text,
            .meta_var = meta_var,
        };
    };

    constexpr static Option unknown(std::uint32_t id) {
        return Option::unaliased_one(pfx_none, "<unknown>", id, Kind::Unknown, 0, "Unknown option");
    }

    constexpr static Option input(std::uint32_t id) {
        return Option::unaliased_one(pfx_none, "<input>", id, Kind::Input, 0, "input content");
    }

    constexpr auto alias_of(std::uint32_t origin_id, const char* origin_args = nullptr) {
        this->alias_args = origin_args;
        this->alias_id = static_cast<unsigned short>(origin_id);
        return *this;
    }
};

/// Non-owning view into an Option within an OptTable. Always valid — use
/// std::optional<OptionRef> to represent a possibly-absent option.
class OptionRef {
public:
    OptionRef(const Option& opt, const OptTable& table);

    std::uint32_t id() const;
    Kind kind() const;

    /// Get the name of this option without any prefix.
    std::string_view name() const;

    /// Get the group this option belongs to, if any.
    std::optional<OptionRef> group() const;

    /// Get the option this is an alias for, if any.
    std::optional<OptionRef> alias() const;

    /// Get the alias arguments as a \0-separated list.
    const char* alias_args() const;

    /// Get the default prefix for this option.
    std::string_view prefix() const;

    /// Get the name with the default prefix, e.g. "--optimize".
    std::string_view prefixed_name() const;

    /// Get the help text for this option.
    std::string_view help_text() const;

    /// Get the meta-variable name for help text, e.g. "<value>".
    std::string_view meta_var() const;

    /// Get the number of separate values this option takes.
    std::uint32_t num_args() const;

    bool has_no_opt_as_input() const;

    RenderStyle render_style() const;

    /// Test if this option has the given driver flag.
    bool has_flag(std::uint32_t val) const;

    /// Test if this option has the given visibility flag.
    bool has_visibility_flag(std::uint32_t val) const;

    /// Return the final option this aliases (itself if not an alias).
    OptionRef unaliased_option() const {
        if(auto als = this->alias())
            return als->unaliased_option();
        return *this;
    }

    /// Return the name to use when rendering this option.
    std::string_view render_name() const {
        return this->unaliased_option().name();
    }

    /// Test whether this option is part of the given option (which may be
    /// a group). Aliases are resolved before matching.
    bool matches(std::uint32_t opt_id) const;

    void print(std::ostream& o, bool add_new_line) const;

protected:
    const Option& opt;
    const OptTable& table;
};

}  // namespace kotatsu::option
