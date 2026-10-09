#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/everything.h"
#include "codec/harness/fixtures/scalars.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/json/harness/backend.h"
#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"
#include "kota/codec/json/json.h"
#include "kota/codec/macro.h"

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

/// Keys JSON escapes, which encode spells out as it compiles.
struct OddlyNamed {
    KOTATSU_ANNOTATE(rename = R"(say "hi")")
    <int> quoted = 1;
    KOTATSU_ANNOTATE(rename = R"(back\slash)")
    <int> backslashed = 2;
    KOTATSU_ANNOTATE(rename = "tab\tand\x01")
    <int> controlled = 3;
    KOTATSU_ANNOTATE(rename = "\b\f\n\r\x1f")
    <int> shortened = 4;
    KOTATSU_ANNOTATE(rename = "del\x7f/caf\xC3\xA9")
    <int> kept = 5;
};

/// Its first field is left out when empty, and the next one's key then
/// opens the object.
struct NoteFirst {
    KOTATSU_ANNOTATE(skip_if = skip_when::empty)
    <std::string> note;
    int id = 1;
};

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
    ZEXPECT(json::to_string(person) == R"({"id":7,"name":"alice","scores":[10,20],"active":true})");
}

ZEST_CASE(tree_text_not_utf8_fails) {
    // A tree's strings and keys go through the same check as a value's.
    auto text = json::to_string(dyn::Value{
        {"name", "caf\xE9"},
    });
    ZASSERT(!text);
    ZEXPECT(text.error().message == "invalid UTF-8 in a string");
    auto key = json::to_string(dyn::Value{
        {"caf\xE9", 1},
    });
    ZASSERT(!key);
    ZEXPECT(key.error().message == "invalid UTF-8 in a string");
}

ZEST_CASE(strings_escape_quotes_and_backslashes) {
    ZEXPECT(json::to_string(std::string(R"(a"b\c/)")) == R"("a\"b\\c/")");
}

ZEST_CASE(strings_escape_control_characters) {
    ZEXPECT(json::to_string(std::string("\n\t\r\b\f")) == R"("\n\t\r\b\f")");
    ZEXPECT(json::to_string(std::string("\x01\x1f")) == R"("\u0001\u001f")");
}

ZEST_CASE(strings_escape_the_byte_wherever_it_stands) {
    // Strings are scanned eight bytes at a time; the byte to escape is found
    // in every place of a word, and of a text shorter than one.
    const std::pair<char, std::string_view> escapes[] = {
        {'"',    R"(\")"    },
        {'\\',   R"(\\)"    },
        {'\n',   R"(\n)"    },
        {'\x01', R"(\u0001)"},
    };
    for(std::size_t size = 1; size <= 20; ++size) {
        for(std::size_t at = 0; at < size; ++at) {
            for(auto [byte, escaped]: escapes) {
                ZEST_CONTEXT("size {}, byte {} at {}", size, +byte, at);
                std::string text(size, 'a');
                text[at] = byte;
                auto expected = std::format(R"("{}{}{}")",
                                            std::string(at, 'a'),
                                            escaped,
                                            std::string(size - at - 1, 'a'));
                ZEXPECT(json::to_string(text) == expected);
            }
        }
    }
}

ZEST_CASE(strings_keep_text_past_ascii) {
    ZEXPECT(json::to_string(std::string("caf\xC3\xA9 \xE2\x82\xAC")) ==
            "\"caf\xC3\xA9 \xE2\x82\xAC\"");
}

ZEST_CASE(field_names_escape_as_strings_do) {
    ZEXPECT(json::to_string(OddlyNamed{}) ==
            R"({"say \"hi\"":1,"back\\slash":2,"tab\tand\u0001":3,"\b\f\n\r\u001f":4,)"
            "\"del\x7f/caf\xC3\xA9\":5}");
}

ZEST_CASE(first_field_written_opens_the_object) {
    NoteFirst value;
    ZEXPECT(json::to_string(value) == R"({"id":1})");
    value.note.assign("x");
    ZEXPECT(json::to_string(value) == R"({"note":"x","id":1})");
}

ZEST_CASE(map_keys_are_escaped) {
    std::map<std::string, int> keyed{
        {R"(key "quoted")", 1}
    };
    ZEXPECT(json::to_string(keyed) == R"({"key \"quoted\"":1})");
}

ZEST_CASE(char_writes_its_codepoint) {
    // The char's value, 0-255, is the codepoint: an octet above 0x7F becomes
    // two bytes of UTF-8, not a sign-extended codepoint.
    ZEXPECT(json::to_string('x') == R"("x")");
    ZEXPECT(json::to_string(static_cast<char>(0xE9)) == R"("é")");
    ZEXPECT(json::to_string(static_cast<char>(0xFF)) == R"("ÿ")");
}

ZEST_CASE(char_backed_enum_writes_its_integer) {
    ZEXPECT(json::to_string(test::Letter::a) == "65");
}

ZEST_CASE(zero_writes_as_a_float) {
    ZEXPECT(json::to_string(0.0) == "0.0");
}

ZEST_CASE(floats_write_text_that_reads_back) {
    // Digits alone get a fraction, so that they read back as a float, and an
    // exponent starts past 1e15 and below 1e-4.
    const std::pair<double, std::string_view> texts[] = {
        {1.0,                      "1.0"                     },
        {-0.0,                     "-0.0"                    },
        {-3.0,                     "-3.0"                    },
        {100000.0,                 "100000.0"                },
        {1e14,                     "100000000000000.0"       },
        {1e15,                     "1e+15"                   },
        {0.0001,                   "0.0001"                  },
        {1e-5,                     "1e-05"                   },
        {-1e21,                    "-1e+21"                  },
        {0.1,                      "0.1"                     },
        {5e-324,                   "5e-324"                  },
        {-2.2250738585072014e-308, "-2.2250738585072014e-308"},
    };
    for(auto [value, text]: texts) {
        ZEST_CONTEXT("value {}", value);
        auto written = json::to_string(value);
        ZASSERT(written);
        ZEXPECT(*written == text);
        auto back = json::from_string<double>(*written);
        ZASSERT(back);
        ZEXPECT(*back == value);
    }
    ZEXPECT(json::to_string(0.1f) == "0.10000000149011612");
    auto tree = json::to_string(dyn::Value(100000.0));
    ZASSERT(tree);
    auto read = json::from_string<dyn::Value>(*tree);
    ZASSERT(read);
    ZEXPECT(read->kind() == dyn::ValueKind::floating);
}

ZEST_CASE(long_documents_grow_their_buffer) {
    std::vector<std::string> items;
    std::string expected = "[";
    for(int i = 0; i < 50'000; ++i) {
        items.push_back(std::format("item {}", i));
        expected += std::format(R"({}"item {}")", i == 0 ? "" : ",", i);
    }
    expected += "]";
    ZEXPECT(json::to_string(items) == expected);
    ZEXPECT(json::to_string(items, 0) == expected);
}

ZEST_CASE(non_finite_writes_null) {
    // JSON has no literal for them, so even nan_repr::Passthrough writes null.
    ZEXPECT(json::to_string(test::NonFinite::typical()) ==
            R"({"nan":null,"inf":null,"neg_inf":null})");
}

ZEST_CASE(byte_span_writes_numbers) {
    std::array<std::byte, 3> bytes{std::byte{0}, std::byte{127}, std::byte{255}};
    ZEXPECT(json::to_string(std::span<const std::byte>(bytes)) == "[0,127,255]");
}

ZEST_CASE(raw_value_splices_its_text) {
    Spliced spliced{.id = 1, .payload = {R"({"any": [1, "thing"]})"}};
    ZEXPECT(json::to_string(spliced) == R"({"id":1,"payload":{"any": [1, "thing"]}})");
    ZEXPECT(json::to_string(Spliced{.id = 1, .payload = {}}) == R"({"id":1,"payload":null})");
}

ZEST_CASE(initial_capacity_does_not_change_output) {
    std::vector<int> values{7, 9};
    ZEXPECT(json::to_string(values, 1) == "[7,9]");
}

ZEST_CASE(prettify_indents) {
    auto pretty = json::prettify(R"({"a":[1,2],"b":{}})");
    ZASSERT(pretty);
    ZEXPECT(*pretty == R"({
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
    ZASSERT(!pretty);
    ZEXPECT(zest::starts_with(pretty.error().message, "TAPE_ERROR"));
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
    ZASSERT(typed_text);
    ZEXPECT(json::to_string(tree) == *typed_text);

    dyn::Value nested{
        {"list", dyn::Array{nullptr, true, std::int64_t{-1}, 2.5, std::string("x")}},
        {"none", dyn::Object{}                                                     },
    };
    ZEXPECT(json::to_string(nested) == R"({"list":[null,true,-1,2.5,"x"],"none":{}})");

    // An Array or Object on its own writes the same way.
    ZEXPECT(json::to_string(dyn::Array{std::int64_t{10}, std::int64_t{20}}) == "[10,20]");
    ZEXPECT(json::to_string(dyn::Object{
                {"x", std::int64_t{10}}
    }) == R"({"x":10})");
}

ZEST_CASE(json_scoped_repr_applies_to_json_only) {
    ZSTATIC_EXPECT(std::is_same_v<meta::resolved_repr_t<Folio, json::format>, std::int64_t>);
    ZSTATIC_EXPECT(std::is_same_v<meta::resolved_repr_t<Folio, OtherFormat>, std::string>);
    auto text = json::to_string(test::Field<Folio>{{.page = 41}});
    ZASSERT(text);
    ZEXPECT(*text == R"({"value":41})");
    auto read = json::from_string<test::Field<Folio>>(*text);
    ZASSERT(read);
    ZEXPECT(read->value.page == 41);
}

ZEST_CASE(everything_lowering) {
    // How each kind lowers into JSON text, in one document: prettified to
    // read, and as written.
    auto document = json::to_string(test::Everything::typical());
    ZASSERT(document);
    ZEXPECT(zest::snapshot(test::Json::render(*document)));
    ZEXPECT(zest::snapshot(*document, "compact"));
}

};  // ZEST_SUITE(codec_json_encode)

}  // namespace

}  // namespace kota::codec
