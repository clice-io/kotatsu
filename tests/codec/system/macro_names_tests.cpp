// The macro sheet as a tool that collects the macros a module uses reads it: from the macros it
// exports, the names their replacement lists hold, and theirs in turn, never a name pasting makes.

#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kotatsu/zest/zest.h"

namespace kotatsu::codec {

namespace {

/// The macros `text` defines, each with the identifiers its replacement list and parameters
/// hold.
std::map<std::string, std::set<std::string>> definitions(std::string_view text) {
    std::map<std::string, std::set<std::string>> macros;
    std::string line;
    auto define = [&] {
        constexpr std::string_view directive = "#define ";
        if(!line.starts_with(directive)) {
            return;
        }
        std::set<std::string>* names = nullptr;
        std::string identifier;
        line.push_back(' ');
        for(char c: std::string_view(line).substr(directive.size())) {
            if(std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                identifier.push_back(c);
                continue;
            }
            if(identifier.empty()) {
                continue;
            }
            if(names == nullptr) {
                names = &macros[identifier];
            } else {
                names->insert(identifier);
            }
            identifier.clear();
        }
    };
    for(auto rest = text; !rest.empty();) {
        const auto end = rest.find('\n');
        auto part = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
        // A checkout on Windows ends its lines with CRLF.
        if(part.ends_with('\r')) {
            part.remove_suffix(1);
        }
        if(part.ends_with('\\')) {
            line.append(part.substr(0, part.size() - 1));
            continue;
        }
        line.append(part);
        define();
        line.clear();
    }
    return macros;
}

ZEST_SUITE(codec_macro_names) {

ZEST_CASE(every_macro_an_expansion_reaches_is_named_in_it) {
    std::ifstream file("include/kotatsu/codec/macro.h", std::ios::binary);
    ZASSERT(file.is_open());
    const std::string text(std::istreambuf_iterator<char>(file), {});
    const auto macros = definitions(text);
    ZASSERT(macros.contains("KANNOTATE_TYPE"));

    // The macros a user writes, and those their replacement lists name, in turn.
    std::set<std::string> reached;
    std::vector<std::string> pending;
    for(const auto& macro: macros) {
        if(!macro.first.starts_with("KOTATSU_")) {
            pending.push_back(macro.first);
        }
    }
    while(!pending.empty()) {
        auto name = std::move(pending.back());
        pending.pop_back();
        if(!reached.insert(name).second) {
            continue;
        }
        for(const auto& named: macros.at(name)) {
            if(macros.contains(named)) {
                pending.push_back(named);
            }
        }
    }
    for(const auto& macro: macros) {
        ZEST_CONTEXT("{}", macro.first);
        ZEXPECT(reached.contains(macro.first));
    }
}

};  // ZEST_SUITE(codec_macro_names)

}  // namespace

}  // namespace kotatsu::codec
