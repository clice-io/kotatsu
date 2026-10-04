#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "codec/dyn/harness/backend.h"
#include "codec/harness/fixtures/enums.h"
#include "codec/harness/fixtures/everything.h"
#include "codec/harness/fixtures/scalars.h"
#include "codec/harness/fixtures/structs.h"
#include "kota/zest/zest.h"
#include "kota/codec/dyn/dyn.h"

namespace kota::codec {

namespace {

ZEST_SUITE(codec_dyn_encode) {

ZEST_CASE(kinds_follow_the_type) {
    // A tree tells 1, 1U and 1.0 apart, which its rendering does not: signed
    // integers and enums land on signed_int, unsigned ones on unsigned_int,
    // floats on floating whatever their value, bytes on unsigned elements.
    auto typed = test::Scalars::typical();
    ZEXPECT(dyn::to_dyn(typed) == (dyn::Value{
                                      {"b", typed.b},
                                      {"i8", std::int64_t{typed.i8}},
                                      {"i16", std::int64_t{typed.i16}},
                                      {"i32", std::int64_t{typed.i32}},
                                      {"i64", typed.i64},
                                      {"u8", std::uint64_t{typed.u8}},
                                      {"u16", std::uint64_t{typed.u16}},
                                      {"u32", std::uint64_t{typed.u32}},
                                      {"u64", typed.u64},
                                      {"f32", static_cast<double>(typed.f32)},
                                      {"f64", typed.f64},
                                      {"c", std::string(1, typed.c)},
                                      {"s", typed.s},
    }));
    ZEXPECT(dyn::to_dyn(1.0) == dyn::Value(1.0));
    ZEXPECT(dyn::to_dyn(test::SignedEnum::neg) == dyn::Value(std::int64_t{-42}));
    ZEXPECT(dyn::to_dyn(test::UInt8Enum::c) == dyn::Value(std::uint64_t{255}));
    ZEXPECT(dyn::to_dyn(std::vector<std::byte>{std::byte{0}, std::byte{255}}) ==
            dyn::Value(dyn::Array{std::uint64_t{0}, std::uint64_t{255}}));
}

ZEST_CASE(fields_follow_declaration_order) {
    // Equality ignores the order of an object's entries; the tree keeps it.
    auto tree = dyn::to_dyn(test::Person{
        .name = "ada",
        .age = 36,
        .addr = {.city = "London", .zip = 1}
    });
    ZASSERT(tree);
    ZASSERT(tree->is_object());
    std::string names;
    for(const auto& [name, value]: tree->as_object()) {
        names += name + ' ';
    }
    ZEXPECT(names == "name age addr ");
}

ZEST_CASE(char_writes_its_codepoint) {
    // The char's value, 0-255, is the codepoint, in UTF-8 as json writes it.
    ZEXPECT(dyn::to_dyn('x') == dyn::Value("x"));
    ZEXPECT(dyn::to_dyn(static_cast<char>(0xE9)) == dyn::Value("é"));
    ZEXPECT(dyn::to_dyn(static_cast<char>(0xFF)) == dyn::Value("ÿ"));
}

ZEST_CASE(tree_writes_itself) {
    dyn::Value tree{
        {"k", dyn::Array{std::int64_t{9}, "x"}}
    };
    ZEXPECT(dyn::to_dyn(tree) == tree);
    ZEXPECT(dyn::to_dyn(tree.as_object()) == tree);
    ZEXPECT(dyn::to_dyn(dyn::Array{std::int64_t{1}}) == dyn::Value(dyn::Array{std::int64_t{1}}));
}

ZEST_CASE(tree_inside_a_value_writes_itself) {
    test::Field<dyn::Value> typed{
        .value = {{"name", "alice"}, {"n", std::int64_t{1}}},
    };
    ZEXPECT(dyn::to_dyn(typed) == (dyn::Value{
                                      {"value", typed.value},
    }));
    ZEXPECT(dyn::to_dyn(std::vector<dyn::Value>{typed.value, nullptr}) ==
            dyn::Value(dyn::Array{typed.value, nullptr}));
}

ZEST_CASE(everything_lowering) {
    // How each kind lowers into a tree, in one document.
    auto document = dyn::to_dyn(test::Everything::typical());
    ZASSERT(document);
    ZEXPECT(zest::snapshot(test::Dyn::render(*document)));
}

};  // ZEST_SUITE(codec_dyn_encode)

}  // namespace

}  // namespace kota::codec
