#include "kota/fs/path.h"

#include <algorithm>
#include <cstddef>

namespace kota::fs::path {

namespace {

bool is_ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool is_verbatim(Prefix prefix) {
    return prefix == Prefix::VerbatimDrive || prefix == Prefix::VerbatimUNC ||
           prefix == Prefix::Verbatim;
}

/// Whether the prefix brings a root directory of its own, so `\\server\share`
/// is absolute without the separator after it.
bool implies_root(Prefix prefix) {
    return prefix != Prefix::None && prefix != Prefix::Drive;
}

/// Whether `c` separates components after `prefix`.
bool separates(char c, Style style, Prefix prefix) {
    return is_verbatim(prefix) ? c == '\\' : is_separator(c, style);
}

/// The end of the run of characters from `at` that are not separators.
std::size_t name_end(std::string_view p, std::size_t at, Style style) {
    while(at < p.size() && !is_separator(p[at], style)) {
        ++at;
    }
    return at;
}

/// The prefix of a Windows path, or None.
Root windows_prefix(std::string_view p) {
    auto sep = [](char c) {
        return c == '/' || c == '\\';
    };
    auto prefix_of = [&](Prefix kind, std::size_t end) {
        return Root{.prefix = kind, .name = p.substr(0, end), .directory = {}};
    };
    if(p.starts_with(R"(\\?\)")) {
        auto rest = p.substr(4);
        // Only `\` separates past a verbatim prefix.
        auto component_end = [&](std::size_t at) {
            auto found = p.find('\\', at);
            return found == std::string_view::npos ? p.size() : found;
        };
        if(rest.size() >= 3 && ascii_lower(rest[0]) == 'u' && ascii_lower(rest[1]) == 'n' &&
           ascii_lower(rest[2]) == 'c' && (rest.size() == 3 || rest[3] == '\\')) {
            auto server_end = component_end(std::min(p.size(), std::size_t{8}));
            auto share_end = server_end < p.size() ? component_end(server_end + 1) : server_end;
            return prefix_of(Prefix::VerbatimUNC, share_end);
        }
        if(rest.size() >= 2 && is_ascii_letter(rest[0]) && rest[1] == ':' &&
           (rest.size() == 2 || rest[2] == '\\')) {
            return prefix_of(Prefix::VerbatimDrive, 6);
        }
        return prefix_of(Prefix::Verbatim, component_end(4));
    }
    if(p.size() >= 4 && sep(p[0]) && sep(p[1]) && (p[2] == '.' || p[2] == '?') && sep(p[3])) {
        return prefix_of(Prefix::Device, name_end(p, 4, Style::Windows));
    }
    if(p.size() >= 3 && sep(p[0]) && sep(p[1]) && !sep(p[2])) {
        auto server_end = name_end(p, 2, Style::Windows);
        if(server_end + 1 < p.size() && !sep(p[server_end + 1])) {
            return prefix_of(Prefix::UNC, name_end(p, server_end + 1, Style::Windows));
        }
        return prefix_of(Prefix::UNC, server_end);
    }
    if(p.size() >= 2 && is_ascii_letter(p[0]) && p[1] == ':') {
        return prefix_of(Prefix::Drive, 2);
    }
    return {};
}

/// The names after the root of `p`, as views of `p`.
std::vector<std::string_view> names_after(std::string_view p, const Root& r, Style style) {
    std::vector<std::string_view> names;
    std::size_t at = r.name.size() + r.directory.size();
    while(at < p.size()) {
        if(separates(p[at], style, r.prefix)) {
            ++at;
            continue;
        }
        auto start = at;
        while(at < p.size() && !separates(p[at], style, r.prefix)) {
            ++at;
        }
        names.push_back(p.substr(start, at - start));
    }
    return names;
}

/// Whether `r` starts at a root directory, written or implied.
bool rooted(const Root& r) {
    return implies_root(r.prefix) || !r.directory.empty();
}

/// Whether two prefixes are the same, compared in either case and with
/// either separator.
bool same_prefix(const Root& a, const Root& b, Style style) {
    if(a.prefix != b.prefix || a.name.size() != b.name.size()) {
        return false;
    }
    for(std::size_t i = 0; i < a.name.size(); ++i) {
        auto x = a.name[i];
        auto y = b.name[i];
        if(is_separator(x, style) && is_separator(y, style)) {
            continue;
        }
        if(ascii_lower(x) != ascii_lower(y)) {
            return false;
        }
    }
    return true;
}

/// Whether two roots name the same place: the same prefix, and both rooted
/// or neither.
bool same_root(const Root& a, const Root& b, Style style) {
    return same_prefix(a, b, style) && rooted(a) == rooted(b);
}

/// The separator a path written after `p` takes: `\` past a verbatim
/// prefix, otherwise the first one `p` uses, or `fallback`.
char separator_of(std::string_view p, const Root& r, Style style, char fallback) {
    if(is_verbatim(r.prefix)) {
        return '\\';
    }
    for(auto c: p) {
        if(is_separator(c, style)) {
            return c;
        }
    }
    return fallback;
}

/// The names joined with `sep`.
void append_names(std::string& out, const std::vector<std::string_view>& names, char sep) {
    for(std::size_t i = 0; i < names.size(); ++i) {
        if(i != 0) {
            out.push_back(sep);
        }
        out.append(names[i]);
    }
}

/// Where the last name of `p` ends: before its trailing separators.
std::size_t trimmed_end(std::string_view p, const Root& r, Style style) {
    auto end = p.size();
    auto root_end = r.name.size() + r.directory.size();
    while(end > root_end && separates(p[end - 1], style, r.prefix)) {
        --end;
    }
    return end;
}

/// Where the dot starting the extension of `name` is, or npos.
std::size_t extension_dot(std::string_view name) {
    if(name == "." || name == "..") {
        return std::string_view::npos;
    }
    auto dot = name.rfind('.');
    return dot == 0 ? std::string_view::npos : dot;
}

}  // namespace

bool is_separator(char c, Style style) {
    return c == '/' || (style == Style::Windows && c == '\\');
}

Root root(std::string_view p, Style style) {
    Root result = style == Style::Windows ? windows_prefix(p) : Root{};
    auto at = result.name.size();
    auto end = at;
    while(end < p.size() && separates(p[end], style, result.prefix)) {
        ++end;
    }
    result.directory = p.substr(at, end - at);
    return result;
}

bool is_absolute(std::string_view p, Style style) {
    auto r = root(p, style);
    return implies_root(r.prefix) || (r.prefix == Prefix::Drive && !r.directory.empty()) ||
           (style == Style::Posix && !r.directory.empty());
}

bool has_root(std::string_view p, Style style) {
    return rooted(root(p, style));
}

std::vector<std::string_view> components(std::string_view p, Style style) {
    return names_after(p, root(p, style), style);
}

std::string_view filename(std::string_view p, Style style) {
    auto r = root(p, style);
    auto end = trimmed_end(p, r, style);
    auto root_end = r.name.size() + r.directory.size();
    auto start = end;
    while(start > root_end && !separates(p[start - 1], style, r.prefix)) {
        --start;
    }
    return p.substr(start, end - start);
}

std::string_view parent(std::string_view p, Style style) {
    auto r = root(p, style);
    auto root_end = r.name.size() + r.directory.size();
    auto name = filename(p, style);
    if(name.empty()) {
        return {};
    }
    auto end = static_cast<std::size_t>(name.data() - p.data());
    while(end > root_end && separates(p[end - 1], style, r.prefix)) {
        --end;
    }
    return p.substr(0, end);
}

std::string_view stem(std::string_view p, Style style) {
    auto name = filename(p, style);
    return name.substr(0, extension_dot(name));
}

std::string_view extension(std::string_view p, Style style) {
    auto name = filename(p, style);
    auto dot = extension_dot(name);
    return dot == std::string_view::npos ? std::string_view() : name.substr(dot);
}

std::string replace_extension(std::string_view p, std::string_view ext, Style style) {
    auto name = filename(p, style);
    if(name.empty()) {
        return std::string(p);
    }
    auto name_start = static_cast<std::size_t>(name.data() - p.data());
    auto dot = extension_dot(name);
    std::string result(p.substr(0, name_start + std::min(dot, name.size())));
    if(!ext.empty()) {
        if(!ext.starts_with('.')) {
            result.push_back('.');
        }
        result.append(ext);
    }
    return result;
}

std::string join(std::string_view base, std::string_view p, Style style) {
    if(base.empty() || is_absolute(p, style)) {
        return std::string(p);
    }
    auto b = root(base, style);
    auto r = root(p, style);
    if(r.prefix != Prefix::None && !same_prefix(r, b, style)) {
        return std::string(p);
    }
    std::string result;
    std::string_view rest;
    if(!r.directory.empty()) {
        result.append(b.name);
        rest = p.substr(r.name.size());
    } else {
        result.append(base);
        rest = p.substr(r.name.size());
        bool ends_in_separator =
            base.size() > b.name.size() && separates(base.back(), style, b.prefix);
        if(!rest.empty() && !ends_in_separator &&
           (base.size() > b.name.size() || implies_root(b.prefix))) {
            // The separator nearest the join, so that the result reads in
            // the base's spelling.
            char sep = separator_of(rest, r, style, '/');
            for(auto c: base) {
                if(is_separator(c, style)) {
                    sep = c;
                }
            }
            result.push_back(is_verbatim(b.prefix) ? '\\' : sep);
        }
    }
    auto appended = result.size();
    result.append(rest);
    if(is_verbatim(b.prefix)) {
        std::replace(result.begin() + static_cast<std::ptrdiff_t>(appended),
                     result.end(),
                     '/',
                     '\\');
    }
    return result;
}

std::string join(std::initializer_list<std::string_view> parts, Style style) {
    std::string result;
    for(auto part: parts) {
        result = join(result, part, style);
    }
    return result;
}

std::string normalize(std::string_view p, DotDot dot_dot, Style style) {
    if(p.empty()) {
        return {};
    }
    auto r = root(p, style);
    bool climbs_stop = rooted(r);
    std::vector<std::string_view> kept;
    for(auto name: names_after(p, r, style)) {
        if(name == ".") {
            continue;
        }
        if(name == ".." && dot_dot == DotDot::Fold) {
            if(!kept.empty() && kept.back() != "..") {
                kept.pop_back();
                continue;
            }
            if(climbs_stop) {
                continue;
            }
        }
        kept.push_back(name);
    }
    char sep = separator_of(p, r, style, '/');
    std::string result;
    for(auto c: r.name) {
        result.push_back(!is_verbatim(r.prefix) && is_separator(c, style) ? sep : c);
    }
    if(!r.directory.empty()) {
        result.push_back(sep);
    }
    append_names(result, kept, sep);
    if(result.empty()) {
        return ".";
    }
    return result;
}

std::optional<std::string> relative(std::string_view p, std::string_view base, Style style) {
    auto pr = root(p, style);
    auto br = root(base, style);
    if(!same_root(pr, br, style)) {
        return std::nullopt;
    }
    auto a = names_after(p, pr, style);
    auto b = names_after(base, br, style);
    std::size_t common = 0;
    while(common < a.size() && common < b.size() && a[common] == b[common]) {
        ++common;
    }
    // A `..` the base has past the common part climbs out of a directory
    // whose name the path does not tell.
    std::size_t climbs = 0;
    for(auto i = common; i < b.size(); ++i) {
        if(b[i] == "..") {
            return std::nullopt;
        }
        climbs += b[i] != ".";
    }
    if(climbs == 0 && common == a.size()) {
        return ".";
    }
    char sep = separator_of(p, pr, style, separator_of(base, br, style, '/'));
    std::vector<std::string_view> names(climbs, "..");
    names.insert(names.end(), a.begin() + static_cast<std::ptrdiff_t>(common), a.end());
    std::string result;
    append_names(result, names, sep);
    return result;
}

std::optional<std::string_view> strip_prefix(std::string_view p,
                                             std::string_view prefix,
                                             Style style) {
    auto pr = root(p, style);
    auto ar = root(prefix, style);
    if(!same_root(pr, ar, style)) {
        return std::nullopt;
    }
    auto names = names_after(p, pr, style);
    auto ancestor = names_after(prefix, ar, style);
    if(ancestor.size() > names.size() ||
       !std::equal(ancestor.begin(), ancestor.end(), names.begin())) {
        return std::nullopt;
    }
    if(ancestor.size() == names.size()) {
        return p.substr(p.size());
    }
    auto start = static_cast<std::size_t>(names[ancestor.size()].data() - p.data());
    return p.substr(start);
}

bool is_within(std::string_view p, std::string_view ancestor, Style style) {
    return strip_prefix(p, ancestor, style).has_value();
}

std::string to_slash(std::string_view p, Style style) {
    std::string result(p);
    if(style == Style::Windows && !is_verbatim(root(p, style).prefix)) {
        std::ranges::replace(result, '\\', '/');
    }
    return result;
}

std::string to_backslash(std::string_view p) {
    std::string result(p);
    std::ranges::replace(result, '/', '\\');
    return result;
}

}  // namespace kota::fs::path
