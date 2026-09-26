#if __has_include(<toml++/toml.hpp>)

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "fixtures/schema/common.h"
#include "kota/zest/zest.h"
#include "kota/codec/toml/toml.h"

namespace kota::codec {

namespace {

using toml::from_string;
using toml::from_toml;
using toml::to_string;
using toml::to_toml;

using person = meta::fixtures::PersonWithScores;

struct payload_with_extra {
    int id = 0;
    ::toml::table extra;
};

enum class priority { low, high };

struct string_enum_config {
    [[maybe_unused]] constexpr static auto enum_repr = codec::enum_repr::String;
};

struct task_entry {
    std::string title;
    priority level = priority::low;
    bool operator==(const task_entry&) const = default;
};

/// Reflectable, yet str-like — kind_of classifies it as a string, so the
/// codec serializes it as one.
struct str_like_aggregate {
    std::string value;

    operator std::string_view() const {
        return value;
    }
};

ZEST_SUITE(codec_toml) {

ZEST_CASE(struct_roundtrip_with_dom) {
    const person input{
        .id = 7,
        .name = "alice",
        .scores = {1, 2, 3},
        .active = true,
    };

    auto dom = to_toml(input);
    ASSERT(dom.has_value());
    ASSERT(dom->contains("id"));
    ASSERT(dom->contains("name"));
    ASSERT(dom->contains("scores"));
    ASSERT(dom->contains("active"));

    person output{};
    auto status = from_toml(*dom, output);
    ASSERT(status);
    EXPECT(output == input);
}

ZEST_CASE(parse_and_to_string_roundtrip) {
    constexpr std::string_view input = R"(
id = 9
name = "bob"
scores = [4, 5]
active = true
)";

    auto parsed = from_string<person>(input);
    ASSERT(parsed);
    EXPECT(parsed->id == 9);
    EXPECT(parsed->name == "bob");
    EXPECT(parsed->scores == std::vector<int>({4, 5}));
    EXPECT(parsed->active == true);

    auto encoded = to_string(*parsed);
    ASSERT(encoded.has_value());

    auto reparsed = from_string<person>(*encoded);
    ASSERT(reparsed);
    EXPECT(*reparsed == *parsed);
}

ZEST_CASE(to_string_and_from_string_with_config) {
    const task_entry input{.title = "write docs", .level = priority::high};

    auto text = to_string<string_enum_config>(input);
    ASSERT(text);
    EXPECT(zest::contains(*text, R"(level = 'high')"));

    auto parsed = from_string<task_entry, string_enum_config>(*text);
    ASSERT(parsed);
    EXPECT(*parsed == input);
}

ZEST_CASE(dynamic_dom_field_roundtrip) {
    payload_with_extra input{};
    input.id = 1;
    input.extra.insert_or_assign("city", "shanghai");
    input.extra.insert_or_assign("zip", 200000);

    ::toml::array tags;
    tags.push_back("a");
    tags.push_back("b");
    input.extra.insert_or_assign("tags", std::move(tags));

    auto dom = to_toml(input);
    ASSERT(dom.has_value());

    payload_with_extra output{};
    auto status = from_toml(*dom, output);
    ASSERT(status);

    EXPECT(output.id == 1);
    auto city = output.extra["city"].value<std::string_view>();
    ASSERT(city);
    EXPECT(*city == "shanghai");

    auto zip = output.extra["zip"].value<std::int64_t>();
    ASSERT(zip);
    EXPECT(*zip == 200000);

    auto tags_out = output.extra["tags"].as_array();
    ASSERT(tags_out != nullptr);
    ASSERT(tags_out->size() == 2U);
    EXPECT((*tags_out)[0].value<std::string_view>().value_or("") == "a");
    EXPECT((*tags_out)[1].value<std::string_view>().value_or("") == "b");
}

ZEST_CASE(boxed_root_scalar_and_optional_none) {
    const std::vector<int> values{3, 5, 8};
    auto encoded_values = to_toml(values);
    ASSERT(encoded_values.has_value());
    ASSERT(encoded_values->contains("__value"));

    std::vector<int> decoded_values{};
    auto decode_values_status = from_toml(*encoded_values, decoded_values);
    ASSERT(decode_values_status);
    EXPECT(decoded_values == values);

    const std::optional<int> none = std::nullopt;
    auto encoded_none = to_toml(none);
    ASSERT(encoded_none.has_value());
    EXPECT(encoded_none->empty());

    std::optional<int> decoded_none = 42;
    auto decode_none_status = from_toml(*encoded_none, decoded_none);
    ASSERT(decode_none_status);
    EXPECT(!decoded_none);
}

ZEST_CASE(shared_ptr_root_roundtrip) {
    const auto input = std::make_shared<person>(person{
        .id = 3,
        .name = "carol",
        .scores = {9, 9},
        .active = false,
    });

    auto dom = to_toml(input);
    ASSERT(dom.has_value());

    std::shared_ptr<person> output;
    auto status = from_toml(*dom, output);
    ASSERT(status);
    ASSERT(output != nullptr);
    EXPECT(*output == *input);
}

ZEST_CASE(null_shared_ptr_root) {
    const std::shared_ptr<person> input;
    auto dom = to_toml(input);
    ASSERT(dom.has_value());
    EXPECT(dom->empty());

    auto output = std::make_shared<person>();
    auto status = from_toml(*dom, output);
    ASSERT(status);
    EXPECT(output == nullptr);
}

ZEST_CASE(unique_ptr_root_roundtrip) {
    auto input = std::make_unique<person>(person{
        .id = 8,
        .name = "dave",
        .scores = {1},
        .active = true,
    });

    auto dom = to_toml(input);
    ASSERT(dom.has_value());

    std::unique_ptr<person> output;
    auto status = from_toml(*dom, output);
    ASSERT(status);
    ASSERT(output != nullptr);
    EXPECT(*output == *input);
}

ZEST_CASE(optional_root_present_roundtrip) {
    const std::optional<person> input = person{
        .id = 4,
        .name = "erin",
        .scores = {7, 8},
        .active = true,
    };

    auto dom = to_toml(input);
    ASSERT(dom.has_value());
    EXPECT(!dom->contains("__value"));

    std::optional<person> output;
    auto status = from_toml(*dom, output);
    ASSERT(status);
    ASSERT(output);
    EXPECT(*output == *input);
}

ZEST_CASE(pointer_to_scalar_root_boxes) {
    // A scalar pointee routes through the boxed root key, same as a bare
    // scalar root.
    const auto input = std::make_shared<int>(7);
    auto dom = to_toml(input);
    ASSERT(dom.has_value());
    EXPECT(dom->contains("__value"));

    std::shared_ptr<int> output;
    auto status = from_toml(*dom, output);
    ASSERT(status);
    ASSERT(output != nullptr);
    EXPECT(*output == 7);
}

ZEST_CASE(str_like_reflectable_root_boxes) {
    // str-like wins over reflection in the codec's kind test, so this
    // aggregate encodes as a string and the root boxes it — the decode-side
    // routing must classify by the same kind, not by reflection alone,
    // both for a bare root and through a pointer root.
    const str_like_aggregate input{.value = "abc"};
    auto dom = to_toml(input);
    ASSERT(dom.has_value());
    EXPECT(dom->contains("__value"));

    str_like_aggregate output;
    auto status = from_toml(*dom, output);
    ASSERT(status);
    EXPECT(output.value == "abc");

    const auto boxed = std::make_shared<str_like_aggregate>(input);
    auto ptr_dom = to_toml(boxed);
    ASSERT(ptr_dom.has_value());
    EXPECT(ptr_dom->contains("__value"));

    std::shared_ptr<str_like_aggregate> ptr_output;
    auto ptr_status = from_toml(*ptr_dom, ptr_output);
    ASSERT(ptr_status);
    ASSERT(ptr_output != nullptr);
    EXPECT(ptr_output->value == "abc");
}

ZEST_CASE(nullable_root_engaged_empty_table_rejected) {
    // TOML has no null: a null root is the empty document, so an engaged
    // pointer whose pointee serializes to an empty table has no
    // representation of its own — encoding fails loudly instead of
    // roundtripping back as null.
    using map_t = std::map<std::string, int>;

    const auto empty_map = std::make_shared<map_t>();
    EXPECT(!to_toml(empty_map).has_value());

    const auto filled = std::make_shared<map_t>(map_t{
        {"a", 1}
    });
    auto dom = to_toml(filled);
    ASSERT(dom.has_value());

    std::shared_ptr<map_t> output;
    auto status = from_toml(*dom, output);
    ASSERT(status);
    ASSERT(output != nullptr);
    EXPECT(*output == *filled);

    // The empty document stays reserved for the null pointer.
    output = std::make_shared<map_t>();
    status = from_toml(::toml::table{}, output);
    ASSERT(status);
    EXPECT(output == nullptr);
}

ZEST_CASE(table_root_symmetry) {
    ::toml::table input;
    input.insert_or_assign("city", "shanghai");
    input.insert_or_assign("zip", 200000);

    // A raw table root becomes the document root itself, not a boxed value.
    auto dom = to_toml(input);
    ASSERT(dom.has_value());
    EXPECT(!dom->contains("__value"));
    EXPECT((*dom)["city"].value<std::string_view>().value_or("") == "shanghai");
    EXPECT((*dom)["zip"].value<std::int64_t>().value_or(0) == 200000);

    ::toml::table output;
    auto status = from_toml(*dom, output);
    ASSERT(status);
    // toml::table reads as a map to meta; its own operator== compares it.
    EXPECT((output == input));
}

ZEST_CASE(tuple_length_errors) {
    // Helper: wrap a toml::array in a boxed root table (__value = arr)
    auto boxed = [](::toml::array arr) {
        ::toml::table tbl;
        tbl.insert_or_assign("__value", std::move(arr));
        return tbl;
    };

    // Too many elements for tuple<int,int>
    {
        auto tbl = boxed(::toml::array{1, 2, 3});
        std::tuple<int, int> t{};
        EXPECT(!from_toml(tbl, t).has_value());
    }

    // Too few elements for tuple<int,int>
    {
        auto tbl = boxed(::toml::array{1});
        std::tuple<int, int> t{};
        EXPECT(!from_toml(tbl, t).has_value());
    }

    // Too many elements for pair<int,int>
    {
        auto tbl = boxed(::toml::array{1, 2, 3});
        std::pair<int, int> p{};
        EXPECT(!from_toml(tbl, p).has_value());
    }

    // Too few elements for pair
    {
        auto tbl = boxed(::toml::array{1});
        std::pair<int, int> p{};
        EXPECT(!from_toml(tbl, p).has_value());
    }

    // Empty array into non-empty tuple
    {
        auto tbl = boxed(::toml::array{});
        std::tuple<int> t{};
        EXPECT(!from_toml(tbl, t).has_value());
    }

    // Non-empty array into empty tuple
    {
        auto tbl = boxed(::toml::array{1});
        std::tuple<> t{};
        EXPECT(!from_toml(tbl, t).has_value());
    }

    // Too many elements for array<int,2>
    {
        auto tbl = boxed(::toml::array{1, 2, 3});
        std::array<int, 2> a{};
        EXPECT(!from_toml(tbl, a).has_value());
    }

    // Too few elements for array<int,2>
    {
        auto tbl = boxed(::toml::array{1});
        std::array<int, 2> a{};
        EXPECT(!from_toml(tbl, a).has_value());
    }

    // Exact match still works
    {
        auto tbl = boxed(::toml::array{1, 2});
        std::tuple<int, int> t{};
        ASSERT(from_toml(tbl, t).has_value());
        EXPECT(std::get<0>(t) == 1);
        EXPECT(std::get<1>(t) == 2);
    }

    // Type mismatch within tuple
    {
        auto tbl = boxed(::toml::array{1, "x"});
        std::tuple<int, int> t{};
        EXPECT(!from_toml(tbl, t).has_value());
    }
}

};  // ZEST_SUITE(codec_toml)

}  // namespace

}  // namespace kota::codec

// ============================================================================
// Format-scoped repr: the TOML backend picks repr<T, toml::format>.
// ============================================================================

namespace kota_toml_format_test {

// Generic form is textual; the toml-scoped override is a bare integer.
struct journal {
    int page = 0;

    auto operator<=>(const journal&) const = default;
};

// The toml-scoped repr resolves to a struct: the representation is
// table-shaped, so it must become the document root itself.
struct diary {
    int page = 0;

    auto operator==(const diary&) const -> bool = default;
};

struct diary_record {
    int page = 0;
};

}  // namespace kota_toml_format_test

namespace kota::meta {

template <>
struct repr<kota_toml_format_test::journal> {
    using type = std::string;

    static type to(const kota_toml_format_test::journal& j) {
        return "p" + std::to_string(j.page);
    }

    static kota_toml_format_test::journal from(const std::string& encoded) {
        return {.page = std::stoi(encoded.substr(1))};
    }
};

template <>
struct repr<kota_toml_format_test::journal, codec::toml::format> {
    using type = std::int64_t;

    static type to(const kota_toml_format_test::journal& j) {
        return j.page;
    }

    static kota_toml_format_test::journal from(type v) {
        return {.page = static_cast<int>(v)};
    }
};

template <>
struct repr<kota_toml_format_test::diary, codec::toml::format> {
    using type = kota_toml_format_test::diary_record;

    static type to(const kota_toml_format_test::diary& d) {
        return {.page = d.page};
    }

    static kota_toml_format_test::diary from(const type& r) {
        return {.page = r.page};
    }
};

}  // namespace kota::meta

namespace kota::codec {

namespace {

using kota_toml_format_test::diary;
using kota_toml_format_test::journal;

struct journal_entry {
    journal j;

    auto operator==(const journal_entry&) const -> bool = default;
};

ZEST_SUITE(codec_toml_format_scoped) {

ZEST_CASE(format_scoped_repr_selected_by_toml) {
    const journal_entry input{.j = {.page = 41}};

    auto text = toml::to_string(input);
    ASSERT(text);
    EXPECT(zest::contains(*text, "j = 41"));

    auto output = toml::from_string<journal_entry>(*text);
    ASSERT(output);
    EXPECT(*output == input);
}

ZEST_CASE(map_keys_follow_toml_scoped_repr) {
    const std::map<journal, int> input{
        {journal{.page = 7},  1},
        {journal{.page = 19}, 2},
    };

    auto text = toml::to_string(input);
    ASSERT(text);
    // Keys travel through the toml-scoped integer repr, not the generic
    // textual one.
    EXPECT(!zest::contains(*text, "p7"));

    auto output = toml::from_string<std::map<journal, int>>(*text);
    ASSERT(output);
    EXPECT(*output == input);
}

ZEST_CASE(top_level_scalar_repr_boxed_under_root_key) {
    // journal's toml repr is a scalar, so the root routing must box it under
    // the root key instead of dumping raw struct fields into the table.
    const journal input{.page = 12};

    auto text = toml::to_string(input);
    ASSERT(text);
    EXPECT(!zest::contains(*text, "page"));

    auto output = toml::from_string<journal>(*text);
    ASSERT(output);
    EXPECT(*output == input);
}

ZEST_CASE(top_level_table_shaped_repr_becomes_root) {
    // diary's toml repr resolves to a struct: the represented fields form the
    // root table directly.
    const diary input{.page = 3};

    auto text = toml::to_string(input);
    ASSERT(text);
    EXPECT(zest::contains(*text, "page = 3"));
    EXPECT(!zest::contains(*text, std::string(toml::detail::boxed_root_key)));

    auto output = toml::from_string<diary>(*text);
    ASSERT(output);
    EXPECT(*output == input);
}

};  // ZEST_SUITE(codec_toml_format_scoped)

}  // namespace

}  // namespace kota::codec

#endif
