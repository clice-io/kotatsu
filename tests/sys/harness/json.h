#pragma once

#include <format>
#include <string>
#include <string_view>

// json, what sys's drivers print a string in their JSON lines by.

namespace kotatsu::test {

/// `text` as a JSON string, its control characters escaped.
inline std::string json(std::string_view text) {
    std::string quoted = "\"";
    for(char c: text) {
        if(c == '"' || c == '\\') {
            quoted += '\\';
            quoted += c;
        } else if(static_cast<unsigned char>(c) < 0x20) {
            quoted += std::format("\\u{:04x}", static_cast<unsigned char>(c));
        } else {
            quoted += c;
        }
    }
    quoted += '"';
    return quoted;
}

}  // namespace kotatsu::test
