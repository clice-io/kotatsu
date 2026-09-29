#include "kota/deco/option/table.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstring>
#include <expected>
#include <functional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace kota::option;

namespace {

enum class AcceptResult {
    Matched,
    NoMatch,
    MissingValue,
};

std::string_view ltrim_all_of(std::string_view str, const std::vector<char>& prefixes) {
    auto pos = str.find_first_not_of(prefixes.data(), 0, prefixes.size());
    if(pos != std::string_view::npos)
        return str.substr(pos, str.size());
    return "";
}

char safe_tolower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool starts_with_insensitive(std::string_view text, std::string_view prefix) {
    if(prefix.size() > text.size())
        return false;
    for(size_t i = 0; i < prefix.size(); ++i) {
        if(safe_tolower(text[i]) != safe_tolower(prefix[i]))
            return false;
    }
    return true;
}

int compare_insensitive(std::string_view str1, std::string_view str2) {
    size_t mn = std::min(str1.size(), str2.size());
    for(size_t i = 0; i < mn; ++i) {
        auto c1 = safe_tolower(str1[i]);
        auto c2 = safe_tolower(str2[i]);
        if(c1 == c2)
            continue;
        return (c1 < c2) ? -1 : 1;
    }
    if(str1.size() == str2.size())
        return 0;
    return str1.size() < str2.size() ? -1 : 1;
}

int str_cmp_opt_name(std::string_view a, std::string_view b, bool fallback_case_sensitive) {
    size_t min_sz = std::min(a.size(), b.size());
    if(int res = compare_insensitive(a.substr(0, min_sz), b.substr(0, min_sz)))
        return res;
    if(a.size() == b.size())
        return fallback_case_sensitive ? a.compare(b) : 0;
    return (a.size() == min_sz) ? 1 : -1;
}

struct OptNameLess {
    inline bool operator()(const Option& i, std::string_view name) const {
        return str_cmp_opt_name(i.name(), name, false) < 0;
    }
};

bool is_input(const OptTable& table, std::string_view arg) {
    if(arg == "-")
        return true;
    for(const auto& prefix: table.prefixes_union) {
        if(arg.starts_with(prefix))
            return false;
    }
    return true;
}

std::uint32_t match_opt(const Option* i, std::string_view str, bool ignore_case) {
    auto name = i->name();
    for(auto prefix: i->prefixes) {
        if(str.starts_with(prefix)) {
            auto rest = str.substr(prefix.size());
            bool matched =
                ignore_case ? starts_with_insensitive(rest, name) : rest.starts_with(name);
            if(matched)
                return static_cast<std::uint32_t>(prefix.size() + name.size());
        }
    }
    return 0;
}

AcceptResult accept_internal(const OptionRef& opt,
                             ArgsRef args,
                             std::string_view spelling,
                             std::uint32_t& index,
                             ParsedArg& out) {
    const size_t spelling_sz = spelling.size();
    const size_t args_idx_sz = args[index].size();

    out.clear();
    out.spelling = spelling;

    switch(opt.kind()) {
        case Kind::Flag: {
            if(spelling_sz != args_idx_sz)
                return AcceptResult::NoMatch;
            out.index = index++;
            return AcceptResult::Matched;
        }
        case Kind::Joined: {
            auto value = args[index].substr(spelling_sz);
            out.add_value(value);
            out.index = index++;
            return AcceptResult::Matched;
        }
        case Kind::CommaJoined: {
            out.index = index;
            auto rest = args[index].substr(spelling_sz);
            for(const auto& part: std::views::split(rest, ',') |
                                      std::views::filter([](auto&& r) { return !r.empty(); })) {
                out.add_value(std::string_view(part));
            }
            index++;
            return AcceptResult::Matched;
        }
        case Kind::Separate: {
            if(spelling_sz != args_idx_sz)
                return AcceptResult::NoMatch;

            out.index = index;
            index += 2;
            if(index > args.size() || args[index - 1].empty())
                return AcceptResult::MissingValue;

            out.add_value(args[index - 1]);
            return AcceptResult::Matched;
        }
        case Kind::MultiArg: {
            if(spelling_sz != args_idx_sz)
                return AcceptResult::NoMatch;

            out.index = index;
            index += 1 + opt.num_args();
            if(index > args.size())
                return AcceptResult::MissingValue;

            for(std::uint32_t i = 0; i != opt.num_args(); ++i)
                out.add_value(args[index - opt.num_args() + i]);
            return AcceptResult::Matched;
        }
        case Kind::JoinedOrSeparate: {
            if(spelling_sz != args_idx_sz) {
                auto value = args[index].substr(spelling_sz);
                out.add_value(value);
                out.index = index++;
                return AcceptResult::Matched;
            }

            out.index = index;
            index += 2;
            if(index > args.size() || args[index - 1].empty())
                return AcceptResult::MissingValue;

            out.add_value(args[index - 1]);
            return AcceptResult::Matched;
        }
        case Kind::JoinedAndSeparate: {
            out.index = index;
            index += 2;
            if(index > args.size() || args[index - 1].empty())
                return AcceptResult::MissingValue;

            out.add_value(args[index - 2].substr(spelling_sz));
            out.add_value(args[index - 1]);
            return AcceptResult::Matched;
        }
        case Kind::RemainingArgs: {
            if(spelling_sz != args_idx_sz)
                return AcceptResult::NoMatch;
            out.index = index++;
            while(index < args.size() && !args[index].empty())
                out.add_value(args[index++]);
            return AcceptResult::Matched;
        }
        case Kind::RemainingArgsJoined: {
            out.index = index;
            if(spelling_sz != args_idx_sz) {
                out.add_value(args[index].substr(spelling_sz));
            }
            index++;
            while(index < args.size() && !args[index].empty())
                out.add_value(args[index++]);
            return AcceptResult::Matched;
        }

        default: std::unreachable();
    }
}

AcceptResult accept_opt(const OptionRef& opt,
                        ArgsRef args,
                        std::string_view spelling,
                        bool grouped_short_option,
                        std::uint32_t& index,
                        ParsedArg& out) {
    AcceptResult result;

    if(grouped_short_option && opt.kind() == Kind::Flag) {
        out.clear();
        out.spelling = spelling;
        out.index = index;
        result = AcceptResult::Matched;
    } else {
        result = accept_internal(opt, args, spelling, index, out);
    }

    if(result != AcceptResult::Matched)
        return result;

    OptionRef unaliased_opt = opt.unaliased_option();
    if(opt.id() == unaliased_opt.id()) {
        out.id = opt.id();
        return AcceptResult::Matched;
    }

    out.id = unaliased_opt.id();

    if(opt.kind() != Kind::Flag)
        return AcceptResult::Matched;

    if(const char* val = opt.alias_args()) {
        while(*val != '\0') {
            out.add_value(std::string_view(val));
            val += std::strlen(val) + 1;
        }
    }
    if(unaliased_opt.kind() == Kind::Joined && !opt.alias_args()) {
        out.add_value(std::string_view(""));
    }
    return AcceptResult::Matched;
}

struct SearchRange {
    const Option* begin;
    const Option* end;
};

SearchRange search_range(const OptTable& table) {
    auto offset = table.search_includes_input ? 0U : table.first_searchable_index;
    return {
        table.option_infos.data() + offset,
        table.option_infos.data() + table.option_infos.size(),
    };
}

struct ScanResult {
    AcceptResult result = AcceptResult::NoMatch;
    ParsedArg out;
    std::uint32_t new_index = 0;
    const Option* fallback_flag = nullptr;
};

/// Scan [range.begin, range.end) for the best option that accepts args[index].
///
/// first_match = true:  return immediately on first acceptance (tablegen / grouped).
/// first_match = false: scan all options and return the longest accepted match.
///
/// Also tracks fallback_flag: the last 2-char Flag candidate that didn't accept
/// (used by parse_step_grouped for short-option group expansion).
ScanResult scan_and_accept(const OptTable& table,
                           SearchRange range,
                           ArgsRef args,
                           std::uint32_t index,
                           bool first_match,
                           const ParseOptions& options) {
    auto* begin = range.begin;
    auto* end = range.end;

    if(table.tablegen_mode) {
        auto name = ltrim_all_of(args[index], table.prefix_chars);
        begin = std::lower_bound(begin, end, name, OptNameLess());
    }

    ScanResult result;
    std::uint32_t best_match_size = 0;
    std::uint32_t best_index = index;
    std::uint32_t missing_match_size = 0;
    std::uint32_t missing_index = index;

    for(auto* it = begin; it != end; ++it) {
        auto arg_sz = match_opt(it, args[index], table.ignore_case);
        if(!arg_sz)
            continue;

        OptionRef opt(*it, table);
        if(options.excludes(opt))
            continue;

        std::uint32_t try_index = index;
        ParsedArg try_out;
        auto a = accept_opt(opt, args, args[index].substr(0, arg_sz), false, try_index, try_out);

        if(a == AcceptResult::Matched) {
            if(first_match) {
                result.result = AcceptResult::Matched;
                result.out = try_out;
                result.new_index = try_index;
                return result;
            }
            if(best_match_size < arg_sz) {
                result.out = try_out;
                best_match_size = arg_sz;
                best_index = try_index;
            }
            continue;
        }

        if(arg_sz == 2 && it->kind == Kind::Flag)
            result.fallback_flag = it;

        if(try_index != index) {
            if(first_match) {
                result.result = AcceptResult::MissingValue;
                result.out.index = index;
                result.new_index = try_index;
                return result;
            }
            if(missing_match_size < arg_sz) {
                missing_index = try_index;
                missing_match_size = arg_sz;
            }
        }
    }

    if(best_match_size > 0) {
        result.result = AcceptResult::Matched;
        result.new_index = best_index;
        return result;
    }

    if(missing_match_size != 0) {
        result.result = AcceptResult::MissingValue;
        result.out.index = index;
        result.new_index = missing_index;
    }

    return result;
}

/// Check whether arg would be accepted as a known option in [range).
/// Uses kind-aware matching: partial prefix matches only count for
/// joined-style options (Joined, CommaJoined, JoinedOrSeparate, etc.).
bool is_known_option(SearchRange range,
                     std::string_view arg,
                     const OptTable& table,
                     const ParseOptions& options) {
    for(auto* s = range.begin; s != range.end; ++s) {
        auto arg_sz = match_opt(s, arg, table.ignore_case);
        if(!arg_sz)
            continue;

        if(arg_sz != arg.size()) {
            switch(s->kind) {
                case Kind::Joined:
                case Kind::CommaJoined:
                case Kind::JoinedOrSeparate:
                case Kind::JoinedAndSeparate:
                case Kind::RemainingArgsJoined: break;
                default: continue;
            }
        }

        OptionRef opt(*s, table);
        if(!options.excludes(opt))
            return true;
    }
    return false;
}

void consume_unknown_values(const OptTable& table,
                            SearchRange range,
                            ArgsRef args,
                            std::uint32_t& index,
                            ParsedArg& out,
                            const ParseOptions& options) {
    while(index < args.size()) {
        auto next = args[index];
        if(next.empty() || next == "--")
            break;
        if(!is_input(table, next) && is_known_option(range, next, table, options))
            break;
        out.add_value(next);
        ++index;
    }
}

AcceptResult parse_step(const OptTable& table,
                        ArgsRef args,
                        std::uint32_t& index,
                        ParsedArg& out,
                        const ParseOptions& options) {
    std::uint32_t prev = index;
    auto str = args[index];

    if(is_input(table, str)) {
        out.clear();
        out.id = table.input_option_id;
        out.spelling = str;
        out.index = index++;
        return AcceptResult::Matched;
    }

    auto range = search_range(table);
    auto scan = scan_and_accept(table, range, args, index, table.tablegen_mode, options);

    if(scan.result == AcceptResult::Matched) {
        index = scan.new_index;
        out = scan.out;
        return AcceptResult::Matched;
    }

    if(scan.result == AcceptResult::MissingValue) {
        index = scan.new_index;
        out.index = prev;
        return AcceptResult::MissingValue;
    }

    if(str[0] == '/') {
        out.clear();
        out.id = table.input_option_id;
        out.spelling = str;
        out.index = index++;
        return AcceptResult::Matched;
    }

    out.clear();
    out.id = table.unknown_option_id;
    out.spelling = str;
    out.index = index++;

    if(options.greedy_unknown && str != "--") {
        consume_unknown_values(table, range, args, index, out, options);
    }

    return AcceptResult::Matched;
}

struct GroupedStep {
    AcceptResult result;

    /// What is left of a group of short options after the one parsed, e.g. "bc" of "-abc";
    /// empty once the element is used up.
    std::string_view rest;
};

/// parse_step() with grouped short options: an element that names no option but starts with
/// a one-letter flag, "-abc", parses as that flag and leaves the rest of the group.
GroupedStep parse_step_grouped(const OptTable& table,
                               ArgsRef args,
                               std::uint32_t& index,
                               ParsedArg& out,
                               const ParseOptions& options) {
    auto str = args[index];

    if(is_input(table, str)) {
        out.clear();
        out.id = table.input_option_id;
        out.spelling = str;
        out.index = index++;
        return {AcceptResult::Matched, {}};
    }

    auto range = search_range(table);
    std::uint32_t prev = index;

    auto scan = scan_and_accept(table, range, args, index, true, options);

    if(scan.result == AcceptResult::Matched) {
        index = scan.new_index;
        out = scan.out;
        return {AcceptResult::Matched, {}};
    }

    if(scan.result == AcceptResult::MissingValue) {
        index = scan.new_index;
        out.index = prev;
        return {AcceptResult::MissingValue, {}};
    }

    if(scan.fallback_flag) {
        // A flag given a value, "-a=1", is no group.
        if(str[2] == '=') {
            out.clear();
            out.id = table.unknown_option_id;
            out.spelling = str;
            out.index = index++;
            return {AcceptResult::Matched, {}};
        }

        OptionRef opt(*scan.fallback_flag, table);
        accept_opt(opt, args, str.substr(0, 2), true, index, out);
        return {AcceptResult::Matched, str.substr(2)};
    }

    if(str.size() > 1 && str[1] != '-') {
        out.clear();
        out.id = table.unknown_option_id;
        out.spelling = str.substr(0, 2);
        out.index = index;
        if(str.size() > 2) {
            return {AcceptResult::Matched, str.substr(2)};
        }
        ++index;
        return {AcceptResult::Matched, {}};
    }

    out.clear();
    out.id = table.unknown_option_id;
    out.spelling = str;
    out.index = index++;

    if(options.greedy_unknown && str != "--") {
        consume_unknown_values(table, range, args, index, out, options);
    }

    return {AcceptResult::Matched, {}};
}

/// argv with its element at `index` read as `element` instead: the rest of a group of short
/// options, behind a '-', so that an option at the end of the group takes its values from
/// the elements after it.
struct GroupOverlay {
    ArgsRef args;
    std::uint32_t index;
    std::string_view element;

    ArgsRef view() const {
        return ArgsRef(this, args.size(), [](const void* data, std::uint32_t i) {
            const auto& overlay = *static_cast<const GroupOverlay*>(data);
            return i == overlay.index ? overlay.element : overlay.args[i];
        });
    }
};

/// Points the values of `out` that lie in `group`, the rest of a group behind its '-', at the
/// same text in `element`, the argv element the group is written in, which outlives the parse.
void rebase_group_values(ParsedArg& out, std::string_view group, std::string_view element) {
    const std::less_equal<const char*> before_or_at;
    const auto* group_end = group.data() + group.size();
    for(auto& value: out.values) {
        if(before_or_at(group.data(), value.data()) && before_or_at(value.data(), group_end)) {
            const auto from_end = static_cast<std::size_t>(group_end - value.data());
            value = std::string_view(element.data() + element.size() - from_end, value.size());
        }
    }
}

}  // namespace

OptTable::OptTable(std::span<const Option> option_infos,
                   bool ignore_case,
                   std::vector<std::string_view> prefixes_union) :
    option_infos(option_infos), prefixes_union(std::move(prefixes_union)),
    ignore_case(ignore_case) {
    bool found_searchable = false;
    for(std::uint32_t i = 0, e = static_cast<std::uint32_t>(option_infos.size()); i != e; ++i) {
        auto& info = option_infos[i];
        if(info.kind == Kind::Input) {
            assert(!this->input_option_id && "Cannot have multiple input options!");
            this->input_option_id = info.id;
        } else if(info.kind == Kind::Unknown) {
            assert(!this->unknown_option_id && "Cannot have multiple unknown options!");
            this->unknown_option_id = info.id;
        } else if(info.kind != Kind::Group && !found_searchable) {
            this->first_searchable_index = i;
            found_searchable = true;
        }
    }

    assert(this->unknown_option_id && "OptTable requires an Unknown option.");

    if(this->prefixes_union.empty()) {
        std::set<std::string_view> tmp;
        for(const auto& i: option_infos.subspan(this->first_searchable_index)) {
            for(auto prefix: i.prefixes)
                tmp.insert(prefix);
        }
        this->prefixes_union = std::vector<std::string_view>(tmp.begin(), tmp.end());
    }

    std::set<char> seen_chars;
    for(auto& prefix: this->prefixes_union) {
        seen_chars.insert(prefix.begin(), prefix.end());
    }
    this->prefix_chars.assign(seen_chars.begin(), seen_chars.end());
}

std::optional<OptionRef> OptTable::option(std::uint32_t opt_id) const {
    if(opt_id == 0)
        return std::nullopt;
    assert((opt_id - 1) < static_cast<std::uint32_t>(this->option_infos.size()) && "Invalid ID.");
    return OptionRef(this->option_infos[opt_id - 1], *this);
}

std::optional<OptionRef> OptTable::find_option(std::string_view argument, std::uint32_t vis) const {
    std::string arg_str(argument);
    if(argument.ends_with("="))
        arg_str += "placeholder";

    std::array<std::string_view, 2> argv = {arg_str, "placeholder"};
    ArgsRef args(argv);

    ParseOptions opts;
    opts.visibility = vis;
    auto range = search_range(*this);
    auto scan = scan_and_accept(*this, range, args, 0, false, opts);

    if(scan.result == AcceptResult::Matched)
        return this->option(scan.out.id);
    return std::nullopt;
}

void OptTable::render(const ParsedArg& arg, kota::function_ref<void(std::string_view)> cb) const {
    auto emit = [&](std::string_view sv) {
        cb(sv);
    };
    auto opt = option(arg.id);

    if(!opt) {
        emit(arg.spelling);
        for(auto& v: arg.values)
            emit(v);
        return;
    }

    auto kind = opt->kind();
    if(kind == Kind::Input || kind == Kind::Unknown) {
        emit(arg.spelling);
        for(auto& v: arg.values)
            emit(v);
        return;
    }

    if(opt->has_no_opt_as_input()) {
        for(auto& v: arg.values)
            emit(v);
        return;
    }

    auto target = opt->unaliased_option();
    auto name = target.prefixed_name();

    switch(opt->render_style()) {
        case RenderStyle::Values:
            for(auto& v: arg.values)
                emit(v);
            break;
        case RenderStyle::Joined: {
            std::string joined(name);
            if(!arg.values.empty())
                joined += arg.values[0];
            emit(joined);
            for(std::size_t i = 1; i < arg.values.size(); ++i)
                emit(arg.values[i]);
            break;
        }
        case RenderStyle::Separate:
            emit(name);
            for(auto& v: arg.values)
                emit(v);
            break;
        case RenderStyle::CommaJoined: {
            std::string token(name);
            for(std::size_t i = 0; i < arg.values.size(); ++i) {
                if(i > 0)
                    token += ',';
                token += arg.values[i];
            }
            emit(token);
            break;
        }
    }
}

bool ParseOptions::excludes(const OptionRef& opt) const {
    if(!opt.has_visibility_flag(visibility))
        return true;
    if(include_flags && !opt.has_flag(include_flags))
        return true;
    if(exclude_flags && opt.has_flag(exclude_flags))
        return true;
    return false;
}

ParseIter::ParseIter(const OptTable* table, ArgsRef args, ParseOptions options) :
    table(table), args(args), options(options), done(false) {
    advance();
}

void ParseIter::advance() {
    if(done)
        return;

    while(index < args.size()) {
        auto str = args[index];
        if(str.empty()) {
            ++index;
            continue;
        }

        if(!past_dash_dash && options.dash_dash_parsing && str == "--") {
            past_dash_dash = true;
            if(options.dash_dash_packing) {
                ParsedArg out;
                out.id = table->input_option_id;
                out.spelling = "--";
                out.index = index;
                for(std::uint32_t i = index + 1; i < args.size(); ++i)
                    out.add_value(args[i]);
                index = args.size();
                out.next_index = index;
                current = out;
                return;
            }
            ++index;
            continue;
        }

        if(past_dash_dash) {
            ParsedArg out;
            out.id = table->input_option_id;
            out.spelling = args[index];
            out.index = index;
            ++index;
            out.next_index = index;
            current = out;
            return;
        }

        ParsedArg out;
        AcceptResult result;
        if(!options.grouped_short_options) {
            result = parse_step(*table, args, index, out, options);
        } else {
            const bool in_group = !group_buf.empty();
            const GroupOverlay overlay{.args = args, .index = index, .element = group_buf};
            const auto step =
                parse_step_grouped(*table, in_group ? overlay.view() : args, index, out, options);
            result = step.result;
            if(in_group) {
                rebase_group_values(out, group_buf, args[out.index]);
            }
            // The rest may view the buffer it replaces.
            group_buf = step.rest.empty() ? std::string() : "-" + std::string(step.rest);
        }

        if(result == AcceptResult::MissingValue) {
            current = std::unexpected(ParseError{out.index, "missing argument value"});
            return;
        }
        out.next_index = index;
        current = std::move(out);
        return;
    }
    done = true;
}
