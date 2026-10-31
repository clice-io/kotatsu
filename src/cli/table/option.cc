#include <cassert>
#include <ostream>
#include <string_view>
#include <utility>

#include "kotatsu/cli/table/table.h"

namespace kotatsu::cli {

OptionRef::OptionRef(const Option& opt, const OptTable& table) : opt(opt), table(table) {
    assert((!this->alias() || !this->alias()->alias()) && "Multi-level aliases are not supported.");

    if(this->alias_args()) {
        assert(this->alias() && "Only alias options can have alias args.");
        assert(this->kind() == OptionKind::Flag && "Only Flag aliases can have alias args.");
        assert(this->alias()->kind() != OptionKind::Flag &&
               "Cannot provide alias args to a flag option.");
    }
}

std::uint32_t OptionRef::id() const {
    return this->opt.id;
}

OptionKind OptionRef::kind() const {
    return this->opt.kind;
}

std::string_view OptionRef::name() const {
    return this->opt.name();
}

std::optional<OptionRef> OptionRef::group() const {
    return this->table.option(this->opt.group_id);
}

std::optional<OptionRef> OptionRef::alias() const {
    return this->table.option(this->opt.alias_id);
}

const char* OptionRef::alias_args() const {
    assert((!this->opt.alias_args || this->opt.alias_args[0] != 0) &&
           "AliasArgs should be either 0 or non-empty.");
    return this->opt.alias_args;
}

std::string_view OptionRef::prefix() const {
    return this->opt.has_no_prefix() ? "" : this->opt.prefixes[0];
}

std::string_view OptionRef::prefixed_name() const {
    return this->opt.prefixed_name;
}

std::string_view OptionRef::help_text() const {
    return this->opt.help_text;
}

std::string_view OptionRef::meta_var() const {
    return this->opt.meta_var;
}

std::uint32_t OptionRef::num_args() const {
    return this->opt.num_args;
}

bool OptionRef::has_no_opt_as_input() const {
    return this->opt.flags & RenderAsInput;
}

RenderStyle OptionRef::render_style() const {
    if(this->opt.flags & RenderJoined)
        return RenderStyle::Joined;
    if(this->opt.flags & RenderSeparate)
        return RenderStyle::Separate;
    switch(this->kind()) {
        case OptionKind::Group:
        case OptionKind::Input:
        case OptionKind::Unknown: return RenderStyle::Values;
        case OptionKind::Joined:
        case OptionKind::JoinedAndSeparate: return RenderStyle::Joined;
        case OptionKind::CommaJoined: return RenderStyle::CommaJoined;
        case OptionKind::Flag:
        case OptionKind::Separate:
        case OptionKind::MultiArg:
        case OptionKind::JoinedOrSeparate:
        case OptionKind::RemainingArgs:
        case OptionKind::RemainingArgsJoined: return RenderStyle::Separate;
    }
    std::unreachable();
}

bool OptionRef::has_flag(std::uint32_t val) const {
    return this->opt.flags & val;
}

bool OptionRef::has_visibility_flag(std::uint32_t val) const {
    return this->opt.visibility & val;
}

void OptionRef::print(std::ostream& o, bool add_new_line) const {
    o << "<";
    switch(this->kind()) {
#define P(N)                                                                                       \
    case N: o << #N; break
        P(OptionKind::Group);
        P(OptionKind::Input);
        P(OptionKind::Unknown);
        P(OptionKind::Flag);
        P(OptionKind::Joined);
        P(OptionKind::Separate);
        P(OptionKind::CommaJoined);
        P(OptionKind::MultiArg);
        P(OptionKind::JoinedOrSeparate);
        P(OptionKind::JoinedAndSeparate);
        P(OptionKind::RemainingArgs);
        P(OptionKind::RemainingArgsJoined);
#undef P
    }

    if(!this->opt.has_no_prefix()) {
        o << " Prefixes:[";
        for(size_t i = 0, n = this->opt.prefixes.size(); i != n; ++i)
            o << '"' << this->opt.prefixes[i] << (i == n - 1 ? "\"" : "\", ");
        o << ']';
    }

    o << " Name:\"" << this->name() << '"';

    if(auto g = this->group()) {
        o << " Group:";
        g->print(o, false);
    }

    if(auto als = this->alias()) {
        o << " Alias:";
        als->print(o, false);
    }

    if(this->kind() == OptionKind::MultiArg)
        o << " NumArgs:" << this->num_args();

    o << ">";
    if(add_new_line) {
        o << "\n";
    }
}

bool OptionRef::matches(std::uint32_t opt_id) const {
    if(auto als = this->alias()) {
        return als->matches(opt_id);
    }

    if(this->id() == opt_id) {
        return true;
    }

    if(auto g = this->group()) {
        return g->matches(opt_id);
    }
    return false;
}

}  // namespace kotatsu::cli
