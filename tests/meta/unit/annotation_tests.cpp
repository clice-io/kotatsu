#include <format>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "kota/zest/zest.h"
#include "kota/meta/annotation.h"

namespace kota::meta {

namespace {

/// Has no std::formatter.
struct Unformattable {
    int value;
};

ZEST_SUITE(meta_annotation) {

ZEST_CASE(class_wrapper_takes_the_wrapped_value) {
    std::optional<std::string> text = "a";
    annotation<std::optional<std::string>> copied = text;
    ASSERT(copied);
    EXPECT(*copied == "a");

    annotation<std::unique_ptr<int>> owned = std::make_unique<int>(1);
    ASSERT(owned != nullptr);
    EXPECT(*owned == 1);

    static_assert(
        !std::is_constructible_v<annotation<std::unique_ptr<int>>, const std::unique_ptr<int>&>);
}

ZEST_CASE(formats_as_the_annotated_value) {
    // A wrapped scalar, and a class it inherits from, with the value's spec.
    annotation<int> count = 42;
    EXPECT(std::format("{:>4}", count) == "  42");
    annotation<bool> flag = true;
    EXPECT(std::format("{}", flag) == "true");
    annotation<std::string> name = std::string("clice");
    EXPECT(std::format("{}|{:*^9}", name, name) == "clice|**clice**");
    STATIC_EXPECT(!std::formattable<annotation<Unformattable>, char>);
}

};  // ZEST_SUITE(meta_annotation)

}  // namespace

}  // namespace kota::meta
