#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/codec/json/string_builder.h"

namespace kota::codec::json {

namespace {

/// What JSON writes for byte c inside a string, spelled out.
std::string escaped(char c) {
    switch(c) {
        case '"': return R"(\")";
        case '\\': return R"(\\)";
        case '\b': return R"(\b)";
        case '\f': return R"(\f)";
        case '\n': return R"(\n)";
        case '\r': return R"(\r)";
        case '\t': return R"(\t)";
        default: break;
    }
    if(static_cast<unsigned char>(c) < 0x20) {
        return std::format(R"(\u{:04x})", static_cast<unsigned char>(c));
    }
    return std::string(1, c);
}

/// size bytes, each unlike its neighbours, so that a byte copied to the
/// wrong place shows.
std::string distinct(std::size_t size) {
    std::string text;
    for(std::size_t i = 0; i < size; ++i) {
        text.push_back(static_cast<char>('A' + i));
    }
    return text;
}

ZEST_SUITE(codec_json_string_builder) {

ZEST_CASE(escaped_size_counts_what_escape_to_writes) {
    ZSTATIC_EXPECT(detail::escaped_size("plain") == 5);
    ZSTATIC_EXPECT(detail::escaped_size(R"(a"b\c)") == 7);
    ZSTATIC_EXPECT(detail::escaped_size("\n\x01") == 8);
}

ZEST_CASE(strings_escape_each_byte_below_0x80_as_json_says) {
    // The builder starts with no room, so each string grows it to exactly
    // the size escaped_size counts.
    for(int byte = 0; byte < 0x80; ++byte) {
        ZEST_CONTEXT("byte {}", byte);
        const auto c = static_cast<char>(byte);
        StringBuilder builder(0);
        builder.append_string(std::string_view(&c, 1));
        ZEXPECT(std::move(builder).take() == "\"" + escaped(c) + "\"");
    }
}

ZEST_CASE(strings_and_raw_text_copy_every_byte_whatever_the_size) {
    for(std::size_t size = 0; size <= 20; ++size) {
        ZEST_CONTEXT("size {}", size);
        const auto text = distinct(size);
        StringBuilder strings(0);
        strings.append_string(text);
        ZEXPECT(std::move(strings).take() == "\"" + text + "\"");
        StringBuilder raw(0);
        raw.append_raw(text);
        ZEXPECT(std::move(raw).take() == text);
    }
}

ZEST_CASE(growth_keeps_what_was_written) {
    const std::string expected =
        R"({"key":[true,false,null,-9223372036854775808,18446744073709551615]})";
    for(std::size_t capacity = 0; capacity <= expected.size() + 1; ++capacity) {
        ZEST_CONTEXT("capacity {}", capacity);
        StringBuilder builder(capacity);
        builder.put('{');
        builder.append_string("key");
        builder.put(':');
        builder.put('[');
        builder.append_bool(true);
        builder.put(',');
        builder.append_bool(false);
        builder.put(',');
        builder.append_null();
        builder.put(',');
        builder.append_number(std::numeric_limits<std::int64_t>::min());
        builder.put(',');
        builder.append_number(std::numeric_limits<std::uint64_t>::max());
        builder.put(']');
        builder.put('}');
        ZEXPECT(std::move(builder).take() == expected);
    }
}

};  // ZEST_SUITE(codec_json_string_builder)

}  // namespace

}  // namespace kota::codec::json
