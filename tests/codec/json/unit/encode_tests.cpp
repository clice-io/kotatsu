#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/everything.h"
#include "codec/harness/fixtures/scalars.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/json/harness/backend.h"
#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

struct Spliced {
    int id = 0;
    RawValue payload;
};

/// Travels as "f<page>", and in json as the page itself: its json-scoped
/// repr names json's own format tag.
struct Folio {
    int page = 0;
};

/// A format tag no backend declares.
struct OtherFormat {};

}  // namespace

}  // namespace kota::codec

namespace kota::meta {

template <>
struct repr<codec::Folio> {
    using type = std::string;

    static type to(const codec::Folio& folio) {
        return "f" + std::to_string(folio.page);
    }

    static codec::Folio from(const std::string& text) {
        return {.page = std::stoi(text.substr(1))};
    }
};

template <>
struct repr<codec::Folio, codec::json::format> {
    using type = std::int64_t;

    static type to(const codec::Folio& folio) {
        return folio.page;
    }

    static codec::Folio from(type page) {
        return {.page = static_cast<int>(page)};
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

ZEST_SUITE(codec_json_encode) {

ZEST_CASE(fields_follow_declaration_order) {
    test::PersonWithScores person{
        .id = 7,
        .name = "alice",
        .scores = {10, 20},
        .active = true
    };
    EXPECT(json::to_string(person) == R"({"id":7,"name":"alice","scores":[10,20],"active":true})");
}

ZEST_CASE(tree_text_not_utf8_fails) {
    // A tree's strings and keys go through the same check as a value's.
    auto text = json::to_string(dyn::Value{
        {"name", "caf\xE9"},
    });
    ASSERT(!text);
    EXPECT(text.error().message == "invalid UTF-8 in a string");
    auto key = json::to_string(dyn::Value{
        {"caf\xE9", 1},
    });
    ASSERT(!key);
    EXPECT(key.error().message == "invalid UTF-8 in a string");
}

ZEST_CASE(strings_escape_quotes_and_backslashes) {
    EXPECT(json::to_string(std::string(R"(a"b\c/)")) == R"("a\"b\\c/")");
}

ZEST_CASE(strings_escape_control_characters) {
    EXPECT(json::to_string(std::string("\n\t\r\b\f")) == R"("\n\t\r\b\f")");
    EXPECT(json::to_string(std::string("\x01\x1f")) == R"("\u0001\u001f")");
}

ZEST_CASE(map_keys_are_escaped) {
    std::map<std::string, int> keyed{
        {R"(key "quoted")", 1}
    };
    EXPECT(json::to_string(keyed) == R"({"key \"quoted\"":1})");
}

ZEST_CASE(char_writes_its_codepoint) {
    // The char's value, 0-255, is the codepoint: an octet above 0x7F becomes
    // two bytes of UTF-8, not a sign-extended codepoint.
    EXPECT(json::to_string('x') == R"("x")");
    EXPECT(json::to_string(static_cast<char>(0xE9)) == R"("é")");
    EXPECT(json::to_string(static_cast<char>(0xFF)) == R"("ÿ")");
}

ZEST_CASE(char_backed_enum_writes_its_integer) {
    EXPECT(json::to_string(test::Letter::a) == "65");
}

ZEST_CASE(zero_writes_as_a_float) {
    EXPECT(json::to_string(0.0) == "0.0");
}

ZEST_CASE(non_finite_writes_null) {
    // JSON has no literal for them, so even nan_repr::Passthrough writes null.
    EXPECT(json::to_string(test::NonFinite::typical()) ==
           R"({"nan":null,"inf":null,"neg_inf":null})");
}

ZEST_CASE(byte_span_writes_numbers) {
    std::array<std::byte, 3> bytes{std::byte{0}, std::byte{127}, std::byte{255}};
    EXPECT(json::to_string(std::span<const std::byte>(bytes)) == "[0,127,255]");
}

ZEST_CASE(raw_value_splices_its_text) {
    Spliced spliced{.id = 1, .payload = {R"({"any": [1, "thing"]})"}};
    EXPECT(json::to_string(spliced) == R"({"id":1,"payload":{"any": [1, "thing"]}})");
    EXPECT(json::to_string(Spliced{.id = 1, .payload = {}}) == R"({"id":1,"payload":null})");
}

ZEST_CASE(initial_capacity_does_not_change_output) {
    std::vector<int> values{7, 9};
    EXPECT(json::to_string(values, 1) == "[7,9]");
}

ZEST_CASE(prettify_indents) {
    auto pretty = json::prettify(R"({"a":[1,2],"b":{}})");
    ASSERT(pretty);
    EXPECT(*pretty == R"({
    "a": [
        1,
        2
    ],
    "b": {}
}
)");
}

ZEST_CASE(prettify_invalid_text_fails) {
    auto pretty = json::prettify(R"({"a":)");
    ASSERT(!pretty);
    EXPECT(zest::starts_with(pretty.error().message, "TAPE_ERROR"));
}

ZEST_CASE(dyn_value_encodes_as_typed) {
    // A dyn::Value tree is written leaf by leaf exactly as the typed value it
    // mirrors.
    test::Scalars typed = test::Scalars::lowest();
    typed.u64 = std::numeric_limits<std::uint64_t>::max();
    dyn::Value tree{
        {"b", false},
        {"i8", std::int64_t{typed.i8}},
        {"i16", std::int64_t{typed.i16}},
        {"i32", std::int64_t{typed.i32}},
        {"i64", typed.i64},
        {"u8", std::uint64_t{0}},
        {"u16", std::uint64_t{0}},
        {"u32", std::uint64_t{0}},
        {"u64", typed.u64},
        {"f32", static_cast<double>(typed.f32)},
        {"f64", typed.f64},
        {"c", std::string(1, typed.c)},
        {"s", std::string(typed.s)},
    };
    auto typed_text = json::to_string(typed);
    ASSERT(typed_text);
    EXPECT(json::to_string(tree) == *typed_text);

    dyn::Value nested{
        {"list", dyn::Array{nullptr, true, std::int64_t{-1}, 2.5, std::string("x")}},
        {"none", dyn::Object{}                                                     },
    };
    EXPECT(json::to_string(nested) == R"({"list":[null,true,-1,2.5,"x"],"none":{}})");

    // An Array or Object on its own writes the same way.
    EXPECT(json::to_string(dyn::Array{std::int64_t{10}, std::int64_t{20}}) == "[10,20]");
    EXPECT(json::to_string(dyn::Object{
               {"x", std::int64_t{10}}
    }) == R"({"x":10})");
}

ZEST_CASE(json_scoped_repr_applies_to_json_only) {
    STATIC_EXPECT(std::is_same_v<meta::resolved_repr_t<Folio, json::format>, std::int64_t>);
    STATIC_EXPECT(std::is_same_v<meta::resolved_repr_t<Folio, OtherFormat>, std::string>);
    auto text = json::to_string(test::Field<Folio>{{.page = 41}});
    ASSERT(text);
    EXPECT(*text == R"({"value":41})");
    auto read = json::from_string<test::Field<Folio>>(*text);
    ASSERT(read);
    EXPECT(read->value.page == 41);
}

ZEST_CASE(everything_lowering) {
    // How each kind lowers into JSON text, in one document.
    auto document = json::to_string(test::Everything::typical());
    ASSERT(document);
    EXPECT_SNAPSHOT(test::Json::render(*document));
}

};  // ZEST_SUITE(codec_json_encode)

}  // namespace

}  // namespace kota::codec
