#include "kota/http/detail/util.h"

#include <cctype>
#include <cstdint>
#include <string>

namespace kota::http::detail {

bool iequals(std::string_view lhs, std::string_view rhs) noexcept {
    if(lhs.size() != rhs.size()) {
        return false;
    }

    for(std::size_t i = 0; i < lhs.size(); ++i) {
        if(std::tolower(static_cast<unsigned char>(lhs[i])) !=
           std::tolower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }

    return true;
}

void upsert_header(std::vector<header>& headers, std::string name, std::string value) {
    for(auto& item: headers) {
        if(iequals(item.name, name)) {
            item.name = std::move(name);
            item.value = std::move(value);
            return;
        }
    }

    headers.push_back({std::move(name), std::move(value)});
}

void insert_header(std::vector<header>& headers, std::string name, std::string value) {
    for(const auto& item: headers) {
        if(iequals(item.name, name)) {
            return;
        }
    }

    headers.push_back({std::move(name), std::move(value)});
}

std::string trim_ascii(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while(begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while(end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for(auto& ch: out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string percent_encode(std::string_view text) {
    constexpr char hex[] = "0123456789ABCDEF";

    std::string out;
    out.reserve(text.size() * 3);
    for(unsigned char ch: text) {
        const bool unreserved = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                                (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
                                ch == '~';
        if(unreserved) {
            out.push_back(static_cast<char>(ch));
            continue;
        }

        out.push_back('%');
        out.push_back(hex[(ch >> 4) & 0x0F]);
        out.push_back(hex[ch & 0x0F]);
    }

    return out;
}

std::string encode_pairs(const std::vector<query_param>& pairs) {
    std::string out;
    bool first = true;
    for(const auto& pair: pairs) {
        if(!first) {
            out.push_back('&');
        }
        first = false;
        out += percent_encode(pair.name);
        out.push_back('=');
        out += percent_encode(pair.value);
    }
    return out;
}

std::string base64_encode(std::string_view text) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((text.size() + 2) / 3) * 4);

    std::uint32_t buffer = 0;
    int bits = 0;
    for(unsigned char ch: text) {
        buffer = (buffer << 8) | ch;
        bits += 8;
        while(bits >= 6) {
            bits -= 6;
            out.push_back(alphabet[(buffer >> bits) & 0x3F]);
        }
    }

    if(bits > 0) {
        buffer <<= (6 - bits);
        out.push_back(alphabet[buffer & 0x3F]);
    }

    while(out.size() % 4 != 0) {
        out.push_back('=');
    }

    return out;
}

}  // namespace kota::http::detail
