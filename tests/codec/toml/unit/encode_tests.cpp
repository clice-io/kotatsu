#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/everything.h"
#include "codec/harness/fixtures/scalars.h"
#include "codec/harness/fixtures/structs.h"
#include "codec/harness/fixtures/tagged.h"
#include "codec/toml/harness/backend.h"
#include "fixtures/repr.h"
#include "fixtures/structs.h"
#include "kota/zest/zest.h"
#include "kota/codec/toml/toml.h"

namespace kota::codec {

namespace {

/// Its toml-scoped repr is a table, its format-agnostic one text: the root
/// follows the one the backend's format selects.
struct Page {
    int number = 0;

    auto operator==(const Page&) const -> bool = default;
};

struct PageRecord {
    int number;
};

/// Reflectable, yet string-like: kind_of takes it for a string, so it
/// encodes as one.
struct Label {
    std::string text;

    operator std::string_view() const {
        return text;
    }
};

}  // namespace

}  // namespace kota::codec

namespace kota::meta {

template <>
struct repr<codec::Page> {
    using type = std::string;

    static type to(const codec::Page& page) {
        return "p" + std::to_string(page.number);
    }

    static codec::Page from(const std::string& text) {
        return {.number = std::stoi(text.substr(1))};
    }
};

template <>
struct repr<codec::Page, codec::toml::format> {
    using type = codec::PageRecord;

    static type to(const codec::Page& page) {
        return {.number = page.number};
    }

    static codec::Page from(const type& record) {
        return {.number = record.number};
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

ZEST_SUITE(codec_toml_encode) {

ZEST_CASE(struct_root_is_the_document) {
    // A table-shaped root is the document itself; toml++ keeps a table's keys
    // sorted, whatever order the fields are declared in.
    test::PersonWithScores person{
        .id = 7,
        .name = "alice",
        .scores = {10, 20},
        .active = true
    };
    ZEXPECT(toml::to_string(person) == R"(active = true
id = 7
name = 'alice'
scores = [ 10, 20 ])");
    std::map<std::string, int> keyed{
        {"b", 2},
        {"a", 1}
    };
    ZEXPECT(toml::to_string(keyed) == "a = 1\nb = 2");
}

ZEST_CASE(non_table_root_is_boxed) {
    // Anything that is not table-shaped by its declared type lands under the
    // `__value` key, and is read back from there.
    ZEXPECT(toml::to_string(7) == "__value = 7");
    auto list = toml::to_string(std::vector<int>{3, 5, 8});
    ZASSERT(list);
    ZEXPECT(*list == "__value = [ 3, 5, 8 ]");
    auto read = toml::from_string<std::vector<int>>(*list);
    ZASSERT(read);
    ZEXPECT(*read == std::vector<int>{3, 5, 8});
}

ZEST_CASE(variant_root_is_boxed) {
    // A variant is boxed although a tagged one always writes a table: the
    // route is chosen from the declared type, not from what it holds.
    ZEXPECT(toml::to_string(test::InternalShape(test::Rect{.width = 2, .height = 3})) ==
            R"([__value]
height = 3.0
kind = 'rect'
width = 2.0)");
    ZEXPECT(toml::to_string(std::variant<int, test::Point>(test::Point{.x = 1, .y = 2})) ==
            "[__value]\nx = 1\ny = 2");
}

ZEST_CASE(string_like_struct_root_is_boxed) {
    // Being string-like wins over reflection, so the struct is boxed as a
    // string, bare and behind a pointer, and read back from the same place.
    auto text = toml::to_string(Label{.text = "abc"});
    ZASSERT(text);
    ZEXPECT(*text == "__value = 'abc'");
    auto read = toml::from_string<Label>(*text);
    ZASSERT(read);
    ZEXPECT(read->text == "abc");

    auto pointed = toml::to_string(std::make_shared<Label>(Label{.text = "abc"}));
    ZASSERT(pointed);
    ZEXPECT(*pointed == "__value = 'abc'");
    auto read_pointed = toml::from_string<std::shared_ptr<Label>>(*pointed);
    ZASSERT(read_pointed);
    ZASSERT(*read_pointed != nullptr);
    ZEXPECT((*read_pointed)->text == "abc");
}

ZEST_CASE(scalar_repr_root_is_boxed) {
    // Journal is a struct whose toml-scoped repr is an integer.
    auto text = toml::to_string(test::Journal{.page = 12});
    ZASSERT(text);
    ZEXPECT(*text == "__value = 12");
    auto read = toml::from_string<test::Journal>(*text);
    ZASSERT(read);
    ZEXPECT(read->page == 12);
}

ZEST_CASE(table_repr_root_is_the_document) {
    auto text = toml::to_string(Page{.number = 3});
    ZASSERT(text);
    ZEXPECT(*text == "number = 3");
    auto read = toml::from_string<Page>(*text);
    ZASSERT(read);
    ZEXPECT(read->number == 3);
}

ZEST_CASE(table_root_is_the_document) {
    toml::Table table{
        {"city", "shanghai"},
        {"zip",  200000    }
    };
    ZEXPECT(toml::to_string(table) == "city = 'shanghai'\nzip = 200000");
}

ZEST_CASE(empty_nullable_root_is_the_empty_document) {
    ZEXPECT(toml::to_string(std::optional<int>{}) == "");
    ZEXPECT(toml::to_string(std::shared_ptr<test::Point>{}) == "");

    std::optional<int> number = 42;
    ZASSERT(toml::from_string("", number));
    ZEXPECT(!number);
    auto point = std::make_shared<test::Point>();
    ZASSERT(toml::from_string("", point));
    ZEXPECT(point == nullptr);
}

ZEST_CASE(engaged_nullable_root_is_its_value) {
    // The wrapper is transparent: the value routes as it would on its own.
    test::Point point{.x = 1, .y = 2};
    ZEXPECT(toml::to_string(std::optional<test::Point>(point)) == "x = 1\ny = 2");
    ZEXPECT(toml::to_string(std::make_unique<test::Point>(point)) == "x = 1\ny = 2");
    ZEXPECT(toml::to_string(std::make_shared<int>(7)) == "__value = 7");

    auto read = toml::from_string<std::unique_ptr<test::Point>>("x = 1\ny = 2");
    ZASSERT(read);
    ZASSERT(*read != nullptr);
    ZEXPECT(**read == point);
    auto optional = toml::from_string<std::optional<test::Point>>("x = 1\ny = 2");
    ZASSERT(optional);
    ZASSERT(optional->has_value());
    ZEXPECT(**optional == point);
    auto number = toml::from_string<std::shared_ptr<int>>("__value = 7");
    ZASSERT(number);
    ZASSERT(*number != nullptr);
    ZEXPECT(**number == 7);
}

ZEST_CASE(engaged_nullable_root_writing_nothing_fails) {
    // The empty document is the null root, so a value that writes an empty
    // table cannot stand for an engaged one.
    using Ints = std::map<std::string, int>;
    auto empty = toml::to_string(std::make_shared<Ints>());
    ZASSERT(!empty);
    ZEXPECT(empty.error().message ==
            "engaged nullable root serializes to an empty TOML document, "
            "indistinguishable from null");
    ZEXPECT(toml::to_string(std::make_shared<Ints>(Ints{
                {"a", 1}
    })) == "a = 1");
}

ZEST_CASE(uint64_beyond_int64_fails) {
    auto status = toml::to_string(test::Field<std::uint64_t>{
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1});
    ZASSERT(!status);
    ZEXPECT(status.error().message == "uint64 exceeds int64 range");
    ZEXPECT(status.error().format_path() == "value");
}

ZEST_CASE(null_element_fails) {
    auto status = toml::to_string(test::Field<std::vector<std::optional<int>>>{
        {1, std::nullopt}
    });
    ZASSERT(!status);
    ZEXPECT(status.error().message == "TOML array does not support null");
    ZEXPECT(status.error().format_path() == "value[1]");
}

ZEST_CASE(null_field_is_omitted) {
    // TOML has no null: a null field writes no key at all.
    ZEXPECT(toml::to_string(test::Field<std::optional<int>>{}) == "");
    ZEXPECT(toml::to_string(test::Field<std::variant<std::monostate, int>>{}) == "");
    ZEXPECT(toml::to_string(test::Field<test::ExternalShape>{}) == "[value]");
    ZEXPECT(toml::to_string(test::Field<test::AdjacentShape>{}) == "[value]\nt = 'none'");
}

ZEST_CASE(null_map_value_is_dropped) {
    // Known gap: a null map value is omitted like a null field, so its entry
    // is lost without an error, where a null array element fails. The case
    // pins today's behaviour.
    std::map<std::string, std::optional<int>> entries{
        {"a", 1           },
        {"b", std::nullopt}
    };
    ZEXPECT(toml::to_string(entries) == "a = 1");
}

ZEST_CASE(char_writes_its_codepoint) {
    // The char's value, 0-255, is the codepoint: an octet above 0x7F becomes
    // two bytes of UTF-8 instead of text toml++ cannot write.
    ZEXPECT(toml::to_string('x') == "__value = 'x'");
    ZEXPECT(toml::to_string(static_cast<char>(0xE9)) == "__value = 'é'");
    ZEXPECT(toml::to_string(static_cast<char>(0xFF)) == "__value = 'ÿ'");
}

ZEST_CASE(non_finite_writes_literals) {
    ZEXPECT(toml::to_string(test::NonFinite::typical()) == "inf = inf\nnan = nan\nneg_inf = -inf");
}

ZEST_CASE(table_and_array_fields_pass_through) {
    test::Field<toml::Table> table{
        toml::Table{{"city", "shanghai"}, {"zip", 200000}}
    };
    ZEXPECT(toml::to_string(table) == "[value]\ncity = 'shanghai'\nzip = 200000");
    test::Field<toml::Array> array{
        toml::Array{"a", "b"}
    };
    ZEXPECT(toml::to_string(array) == "value = [ 'a', 'b' ]");
}

ZEST_CASE(to_toml_builds_the_table) {
    test::Point point{.x = 1, .y = 2};
    // The debug codec cannot print a toml::Table, so these check has_value().
    auto table = toml::to_toml(point);
    ZASSERT(table.has_value());
    ZEXPECT(table->size() == 2U);
    ZEXPECT((*table)["x"].value<std::int64_t>() == 1);
    ZEXPECT((*table)["y"].value<std::int64_t>() == 2);
    auto boxed = toml::to_toml(7);
    ZASSERT(boxed.has_value());
    ZEXPECT((*boxed)["__value"].value<std::int64_t>() == 7);
}

ZEST_CASE(everything_lowering) {
    // How each kind lowers into TOML text, in one document. toml++ prints a
    // float in its shortest form with one standard library and to 17 digits
    // with another; binary fractions print alike under both.
    auto value = test::Everything::typical();
    value.scalars.f32 = 3.25F;
    value.scalars.f64 = 2.5;
    auto document = toml::to_string(value);
    ZASSERT(document);
    ZEXPECT(zest::snapshot(test::Toml::render(*document)));
}

};  // ZEST_SUITE(codec_toml_encode)

}  // namespace

}  // namespace kota::codec
