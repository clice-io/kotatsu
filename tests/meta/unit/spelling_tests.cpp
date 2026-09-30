#include <string>
#include <string_view>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/meta/spelling.h"

namespace kota::meta {

namespace {

enum class Color {
    red,
    light_blue,
    GreenLight,
    /// A keyword-like name, generated with a trailing underscore.
    Delete_,
    /// Names that start with a digit, generated with a prefix.
    _2d,
    V3,
    HTTPServer,
};

struct Echo {
    std::string_view operator()(bool, std::string_view value) const {
        return value;
    }
};

struct Constant {
    const char* operator()(bool is_serialize, std::string_view) const {
        return is_serialize ? "out" : "in";
    }
};

ZEST_SUITE(meta_spelling) {

ZEST_CASE(policies_may_return_views) {
    EXPECT(apply_rename_policy<Echo>(true, "asIs") == "asIs");
    EXPECT(apply_rename_policy<Constant>(true, "x") == "out");
    EXPECT(apply_rename_policy<Constant>(false, "x") == "in");
    EXPECT(apply_rename_policy<naming::rename_policy::upper_snake>(true, "fooBar") == "FOO_BAR");
}

ZEST_CASE(enum_to_string_renames_the_enumerator) {
    EXPECT(map_enum_to_string(Color::light_blue) == "lightBlue");
    EXPECT(map_enum_to_string(Color::GreenLight) == "greenLight");
    EXPECT(map_enum_to_string<Color, naming::rename_policy::identity>(Color::light_blue) ==
           "light_blue");
    EXPECT(map_enum_to_string<Color, naming::rename_policy::upper_snake>(Color::red) == "RED");
}

ZEST_CASE(enum_strings_lists_every_enumerator_renamed) {
    const auto& names = enum_strings<Color>();
    EXPECT(names == std::vector<std::string>{"red",
                                             "lightBlue",
                                             "greenLight",
                                             "delete",
                                             "2d",
                                             "v3",
                                             "httpServer"});
    EXPECT(&enum_strings<Color>() == &names);
}

ZEST_CASE(string_to_enum_reads_the_renamed_spelling) {
    EXPECT(map_string_to_enum<Color>("red") == Color::red);
    EXPECT(map_string_to_enum<Color>("lightBlue") == Color::light_blue);
    EXPECT(map_string_to_enum<Color>("light_blue") == Color::light_blue);
    EXPECT(map_string_to_enum<Color>("greenLight") == Color::GreenLight);
}

ZEST_CASE(string_to_enum_finds_generated_names) {
    EXPECT(map_string_to_enum<Color>("delete") == Color::Delete_);
    EXPECT(map_string_to_enum<Color>("2d") == Color::_2d);
    EXPECT(map_string_to_enum<Color>("3") == Color::V3);
}

ZEST_CASE(string_to_enum_reads_back_every_renamed_name) {
    // `httpServer` reads back as `http_server`, which no alias of `HTTPServer` spells.
    EXPECT(map_string_to_enum<Color>("httpServer") == Color::HTTPServer);
    for(const auto value: reflection<Color>::member_values) {
        const auto name = map_enum_to_string(value);
        ZEST_CONTEXT("enumerator spelled `{}`", name);
        EXPECT(map_string_to_enum<Color>(name) == value);
    }
}

ZEST_CASE(string_to_enum_of_an_unknown_name_fails) {
    EXPECT(!map_string_to_enum<Color>("purple").has_value());
    EXPECT(!map_string_to_enum<Color>("").has_value());
}

};  // ZEST_SUITE(meta_spelling)

}  // namespace

}  // namespace kota::meta
