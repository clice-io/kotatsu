#include <format>
#include <string>

#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"

namespace kota::meta {

namespace {

/// A type std::format knows nothing about.
struct Opaque {
    int value = 0;
};

// An annotation formats as the value it annotates, under that value's format spec.

ZEST_SUITE(meta_annotation_format) {

ZEST_CASE(string_formats_as_its_text) {
    annotation<std::string> name = std::string("kota");
    EXPECT(std::format("{}", name) == "kota");
    EXPECT(std::format("[{:>6}]", name) == "[  kota]");
    EXPECT(std::format("[{:*<6.2}]", name) == "[ko****]");
}

ZEST_CASE(bool_formats_as_its_value) {
    annotation<bool> flag = true;
    EXPECT(std::format("{}", flag) == "true");
    EXPECT(std::format("[{:>6}]", flag) == "[  true]");
    EXPECT(std::format("{:d}", flag) == "1");
}

ZEST_CASE(int_formats_as_its_value) {
    annotation<int> count = 42;
    EXPECT(std::format("{}", count) == "42");
    EXPECT(std::format("[{:>5}]", count) == "[   42]");
    EXPECT(std::format("{:#06x}", count) == "0x002a");
}

ZEST_CASE(attributes_leave_the_format_alone) {
    annotation<std::string, behavior::skip_if<pred::empty>> name = std::string("kota");
    EXPECT(std::format("[{:^8}]", name) == "[  kota  ]");
}

ZEST_CASE(annotation_of_an_unformattable_type_does_not_format) {
    STATIC_EXPECT(!std::formattable<annotation<Opaque>, char>);
    STATIC_EXPECT(std::formattable<annotation<std::string>, char>);
}

};  // ZEST_SUITE(meta_annotation_format)

}  // namespace

}  // namespace kota::meta
