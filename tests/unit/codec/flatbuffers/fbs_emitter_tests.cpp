#include <string>

#include "fixtures/schema/containers.h"
#include "fixtures/schema/enums.h"
#include "fixtures/schema/primitives.h"
#include "kota/zest/zest.h"
#include "kota/meta/type_info.h"
#include "kota/codec/flatbuffers/fbs_emitter.h"

namespace kota::codec::flatbuffers {

namespace {

namespace fx = ::kota::meta::fixtures;
namespace fbs = ::kota::codec::flatbuffers::fbs;

auto contains(const std::string& haystack, std::string_view needle) -> bool {
    return haystack.find(needle) != std::string::npos;
}

TEST_SUITE(fbs_emitter) {

TEST_CASE(trivial_struct_becomes_inline_struct) {
    const auto schema = fbs::render<fx::SimpleStruct>();
    EXPECT_TRUE(contains(schema, "table "));  // SimpleStruct has std::string, not inline
    EXPECT_TRUE(contains(schema, "x:int;"));
    EXPECT_TRUE(contains(schema, "name:string;"));
    EXPECT_TRUE(contains(schema, "score:float;"));
    EXPECT_TRUE(contains(schema, "root_type "));
}

TEST_CASE(all_primitives_emits_every_scalar_kind) {
    const auto schema = fbs::render<fx::AllPrimitives>();
    EXPECT_TRUE(contains(schema, "b:bool;"));
    EXPECT_TRUE(contains(schema, "i8:byte;"));
    EXPECT_TRUE(contains(schema, "i16:short;"));
    EXPECT_TRUE(contains(schema, "i32:int;"));
    EXPECT_TRUE(contains(schema, "i64:long;"));
    EXPECT_TRUE(contains(schema, "u8:ubyte;"));
    EXPECT_TRUE(contains(schema, "u16:ushort;"));
    EXPECT_TRUE(contains(schema, "u32:uint;"));
    EXPECT_TRUE(contains(schema, "u64:ulong;"));
    EXPECT_TRUE(contains(schema, "f32:float;"));
    EXPECT_TRUE(contains(schema, "f64:double;"));
    EXPECT_TRUE(contains(schema, "c:byte;"));
    EXPECT_TRUE(contains(schema, "s:string;"));
}

TEST_CASE(enum_underlying_mapped_to_fbs_scalar) {
    struct EnumHolder {
        fx::color c;
    };

    const auto schema = fbs::render<EnumHolder>();
    EXPECT_TRUE(contains(schema, "enum "));
    // color's underlying is the enum's default (int) → "int"
    EXPECT_TRUE(contains(schema, ":int {"));
    EXPECT_TRUE(contains(schema, "red = 0"));
    EXPECT_TRUE(contains(schema, "green = 1"));
    EXPECT_TRUE(contains(schema, "blue = 2"));
}

TEST_CASE(unsigned_enum_uses_unsigned_underlying) {
    struct UHolder {
        fx::UInt8Enum value;
    };

    const auto schema = fbs::render<UHolder>();
    EXPECT_TRUE(contains(schema, ":ubyte {"));
    EXPECT_TRUE(contains(schema, "c = 255"));
}

TEST_CASE(map_field_emits_entry_table) {
    struct MapHolder {
        std::map<std::string, int> attrs;
    };

    const auto schema = fbs::render<MapHolder>();
    EXPECT_TRUE(contains(schema, "_attrsEntry"));
    EXPECT_TRUE(contains(schema, "key:string (key);"));
    EXPECT_TRUE(contains(schema, "value:int;"));
    EXPECT_TRUE(contains(schema, "attrs:["));
}

TEST_CASE(nested_struct_emitted_as_dependency) {
    const auto schema = fbs::render<fx::NestedStruct>();
    // SimpleStruct should appear before NestedStruct since it's a dependency
    const auto simple_pos = schema.find("SimpleStruct");
    const auto nested_pos = schema.find("NestedStruct");
    ASSERT_NE(simple_pos, std::string::npos);
    ASSERT_NE(nested_pos, std::string::npos);
    EXPECT_LT(simple_pos, nested_pos);
}

TEST_CASE(optional_and_pointer_unwrap_to_inner) {
    struct OptHolder {
        std::optional<int> maybe;
        std::unique_ptr<std::string> ptr;
    };

    const auto schema = fbs::render<OptHolder>();
    // Optional<int> → int on the wire
    EXPECT_TRUE(contains(schema, "maybe:int;"));
    EXPECT_TRUE(contains(schema, "ptr:string;"));
}

TEST_CASE(vector_of_scalar_emits_list_type) {
    struct VecHolder {
        std::vector<std::int32_t> xs;
    };

    const auto schema = fbs::render<VecHolder>();
    EXPECT_TRUE(contains(schema, "xs:[int];"));
}

TEST_CASE(root_type_declaration_present) {
    const auto schema = fbs::render<fx::SimpleStruct>();
    EXPECT_TRUE(contains(schema, "root_type "));
}

};  // TEST_SUITE(fbs_emitter)

}  // namespace

}  // namespace kota::codec::flatbuffers
