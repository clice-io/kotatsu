#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kota::test {

/// An argv of `words`, e.g. `test::args("-o", "out")`. A parse keeps views into it, so a test
/// holds it for as long as it reads the result.
template <typename... Words>
std::vector<std::string> args(Words&&... words) {
    std::vector<std::string> argv;
    argv.reserve(sizeof...(words));
    (argv.emplace_back(std::forward<Words>(words)), ...);
    return argv;
}

/// The parts of `line` between each `separator`: the argv it writes when that is a space,
/// "-o out" as {"-o", "out"}.
inline std::vector<std::string> split(std::string_view line, char separator = ' ') {
    std::vector<std::string> parts;
    while(true) {
        const auto at = line.find(separator);
        parts.emplace_back(line.substr(0, at));
        if(at == std::string_view::npos) {
            return parts;
        }
        line.remove_prefix(at + 1);
    }
}

}  // namespace kota::test
