/// Tests that try_read correctly saves and restores all json_iterator state
/// on failure. Uses Source::json_iter() to verify internal simdjson state.

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

using json::from_string;
using json::Reader;
using json::Source;

struct SimpleA {
    int x = 0;
    int y = 0;
    std::string name;
};

struct SimpleB {
    std::string value;
};

using SimpleVariant = std::variant<SimpleA, SimpleB>;

struct HasRequired {
    int id = 0;
    std::string label;
    std::string data;
};

struct HasOptional {
    std::optional<int> id;
    std::string data;
};

struct Nested {
    std::string a;
    std::string b;
    std::string c;
};

struct Shallow {
    std::string a;
};

using NestedVariant = std::variant<Nested, Shallow>;

struct Wrapper {
    std::vector<SimpleVariant> items;
};

ZEST_SUITE(codec_json_simdjson_checkpoint) {

ZEST_CASE(string_buffer_reclaimed) {
    std::string big(16384, 'X');
    std::string input = R"({"value":")" + big + R"("})";

    json::padded_string padded{std::string_view{input}};
    json::ondemand::Parser parser;
    json::ondemand::Document doc;
    ASSERT(parser.iterate(padded).get(doc) == json::success);
    auto r = Reader{doc, padded.data(), padded.size()};
    auto& ji = r.src.json_iter();
    auto* buf_before = ji.string_buf_loc();

    bool ok = r.try_read([&](Reader& sub) -> bool {
        SimpleA a;
        return decode_value<default_config<>>(sub, a);
    });
    EXPECT(!ok);
    EXPECT(ji.string_buf_loc() == buf_before);
}

ZEST_CASE(depth_restored) {
    auto input = R"({"value":"test"})";
    json::padded_string padded{std::string_view{input}};
    json::ondemand::Parser parser;
    json::ondemand::Document doc;
    ASSERT(parser.iterate(padded).get(doc) == json::success);
    auto r = Reader{doc, padded.data(), padded.size()};
    auto& ji = r.src.json_iter();
    auto depth_before = ji.depth();

    bool ok = r.try_read([&](Reader& sub) -> bool {
        SimpleA a;
        return decode_value<default_config<>>(sub, a);
    });
    EXPECT(!ok);
    EXPECT(ji.depth() == depth_before);
}

ZEST_CASE(token_position_restored) {
    auto input = R"({"value":"test"})";
    json::padded_string padded{std::string_view{input}};
    json::ondemand::Parser parser;
    json::ondemand::Document doc;
    ASSERT(parser.iterate(padded).get(doc) == json::success);
    auto r = Reader{doc, padded.data(), padded.size()};
    auto& ji = r.src.json_iter();
    auto pos_before = ji.position();

    bool ok = r.try_read([&](Reader& sub) -> bool {
        SimpleA a;
        return decode_value<default_config<>>(sub, a);
    });
    EXPECT(!ok);
    EXPECT(ji.position() == pos_before);
}

ZEST_CASE(all_state_with_large_string) {
    std::string big(32768, 'Z');
    std::string input = R"({"data":")" + big + R"("})";

    json::padded_string padded{std::string_view{input}};
    json::ondemand::Parser parser;
    json::ondemand::Document doc;
    ASSERT(parser.iterate(padded).get(doc) == json::success);
    auto r = Reader{doc, padded.data(), padded.size()};
    auto& ji = r.src.json_iter();

    auto pos_before = ji.position();
    auto* buf_before = ji.string_buf_loc();
    auto depth_before = ji.depth();

    bool ok = r.try_read([&](Reader& sub) -> bool {
        HasRequired h;
        return decode_value<default_config<>>(sub, h);
    });
    EXPECT(!ok);

    EXPECT(ji.position() == pos_before);
    EXPECT(ji.string_buf_loc() == buf_before);
    EXPECT(ji.depth() == depth_before);
}

ZEST_CASE(success_advances_state) {
    auto input = R"({"value":"hello"})";
    json::padded_string padded{std::string_view{input}};
    json::ondemand::Parser parser;
    json::ondemand::Document doc;
    ASSERT(parser.iterate(padded).get(doc) == json::success);
    auto r = Reader{doc, padded.data(), padded.size()};
    auto& ji = r.src.json_iter();

    auto pos_before = ji.position();
    auto* buf_before = ji.string_buf_loc();

    bool ok = r.try_read([&](Reader& sub) -> bool {
        SimpleB b;
        return decode_value<default_config<>>(sub, b);
    });
    EXPECT(ok);
    EXPECT(ji.position() != pos_before);
    EXPECT(ji.string_buf_loc() != buf_before);
}

ZEST_CASE(multiple_failures_no_drift) {
    auto input = R"({"value":"hello"})";
    json::padded_string padded{std::string_view{input}};
    json::ondemand::Parser parser;
    json::ondemand::Document doc;
    ASSERT(parser.iterate(padded).get(doc) == json::success);
    auto r = Reader{doc, padded.data(), padded.size()};
    auto& ji = r.src.json_iter();

    auto pos_original = ji.position();
    auto* buf_original = ji.string_buf_loc();
    auto depth_original = ji.depth();

    for(int i = 0; i < 5; ++i) {
        bool ok = r.try_read([&](Reader& sub) -> bool {
            SimpleA a;
            return decode_value<default_config<>>(sub, a);
        });
        EXPECT(!ok);
    }

    EXPECT(ji.position() == pos_original);
    EXPECT(ji.string_buf_loc() == buf_original);
    EXPECT(ji.depth() == depth_original);
}

ZEST_CASE(variant_fallback_reclaims) {
    // variant<Nested, Shallow>: Nested fails (missing b, c), Shallow succeeds.
    // String buffer consumed during Nested attempt must be reclaimed.
    std::string big(8192, 'D');
    std::string input = R"({"a":")" + big + R"("})";

    NestedVariant out;
    auto result = from_string<>(input, out);
    ASSERT(result);
    ASSERT(out.index() == 1U);
    EXPECT(std::get<Shallow>(out).a.size() == 8192U);
}

ZEST_CASE(variant_fallback_reclaims_256KB) {
    std::string big(256 * 1024, 'E');
    std::string input = R"({"a":")" + big + R"("})";

    NestedVariant out;
    auto result = from_string<>(input, out);
    ASSERT(result);
    ASSERT(out.index() == 1U);
    EXPECT(std::get<Shallow>(out).a.size() == 256U * 1024U);
}

ZEST_CASE(variant_first_alternative_succeeds) {
    // Sanity: when the first alternative matches, no checkpoint restore needed.
    std::string s1(4096, 'A');
    std::string s2(4096, 'B');
    std::string s3(4096, 'C');
    std::string input = R"({"a":")" + s1 + R"(","b":")" + s2 + R"(","c":")" + s3 + R"("})";

    NestedVariant out;
    auto result = from_string<>(input, out);
    ASSERT(result);
    ASSERT(out.index() == 0U);
    auto& n = std::get<Nested>(out);
    EXPECT(n.a.size() == 4096U);
    EXPECT(n.b.size() == 4096U);
    EXPECT(n.c.size() == 4096U);
}

ZEST_CASE(value_level_variant) {
    // Variant inside an array: try_read operates on a Value, not Document.
    auto input = R"({"items":[{"value":"world"}]})";
    Wrapper w;
    auto result = from_string<>(input, w);
    ASSERT(result);
    ASSERT(w.items.size() == 1U);
    ASSERT(w.items[0].index() == 1U);
    EXPECT(std::get<SimpleB>(w.items[0]).value == "world");
}

};  // ZEST_SUITE(codec_json_simdjson_checkpoint)

}  // namespace

}  // namespace kota::codec
