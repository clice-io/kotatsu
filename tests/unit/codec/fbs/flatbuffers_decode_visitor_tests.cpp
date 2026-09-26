#if __has_include(<flatbuffers/flatbuffers.h>)

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "../standard_case_suite.h"
#include "fixtures/schema/common.h"
#include "kota/zest/zest.h"
#include "kota/meta/attrs.h"
#include "kota/codec/fbs/decode.h"
#include "kota/codec/fbs/encode.h"

namespace kota::codec {

using namespace meta;

namespace {

using fbs::to_bytes;
using fbs::from_bytes;

using point = meta::fixtures::Point2i;
using address = meta::fixtures::Address;

struct ext_tag_annotation {
    constexpr static auto spec =
        make_struct_spec(dsl::tagged = true, dsl::tag_names = {"integer", "text", "basic"});
};

struct person {
    std::int32_t id;
    std::string name;
    point pos;
    std::vector<std::int32_t> scores;
    address addr;
};

template <typename T>
auto roundtrip(const T& input) -> std::expected<T, rich_error> {
    auto encoded = to_bytes(input);
    if(!encoded) {
        return std::unexpected(rich_error("encode failed"));
    }
    if(encoded->empty()) {
        return std::unexpected(rich_error("empty buffer"));
    }
    return from_bytes<T>(*encoded);
}

ZEST_SUITE(fbs_decode_visitor) {

ZEST_CASE(scalar_int32_roundtrip) {
    std::int32_t input = 42;
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(*result == 42);
}

ZEST_CASE(scalar_string_roundtrip) {
    std::string input = "hello world";
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(*result == "hello world");
}

ZEST_CASE(scalar_bool_roundtrip) {
    bool input = true;
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(*result == true);
}

ZEST_CASE(scalar_double_roundtrip) {
    double input = 3.14;
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(*result == 3.14);
}

ZEST_CASE(simple_struct_roundtrip) {
    address input{.city = "tokyo", .zip = 100};
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(result->city == "tokyo");
    EXPECT(result->zip == 100);
}

ZEST_CASE(inline_struct_roundtrip) {
    struct with_point {
        point pos;
    };

    with_point input{
        .pos = {.x = 10, .y = 20}
    };
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(result->pos.x == 10);
    EXPECT(result->pos.y == 20);
}

ZEST_CASE(bare_inline_struct_roundtrip) {
    point input{.x = 42, .y = 99};
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(result->x == 42);
    EXPECT(result->y == 99);
}

ZEST_CASE(struct_with_nested_struct_and_vector_roundtrip) {
    struct simple_person {
        std::int32_t id;
        std::string name;
        std::vector<std::int32_t> scores;
        address addr;
    };

    simple_person input{
        .id = 7,
        .name = "alice",
        .scores = {1, 2, 3},
        .addr = {.city = "sh", .zip = 200000},
    };
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(result->id == 7);
    EXPECT(result->name == "alice");
    ASSERT(result->scores.size() == 3U);
    EXPECT(result->scores[0] == 1);
    EXPECT(result->scores[1] == 2);
    EXPECT(result->scores[2] == 3);
    EXPECT(result->addr.city == "sh");
    EXPECT(result->addr.zip == 200000);
}

ZEST_CASE(struct_with_vector_roundtrip) {
    person input{
        .id = 7,
        .name = "alice",
        .pos = {.x = 10, .y = 20},
        .scores = {1, 2, 3},
        .addr = {.city = "sh", .zip = 200000},
    };
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(result->id == 7);
    EXPECT(result->name == "alice");
    EXPECT(result->pos.x == 10);
    EXPECT(result->pos.y == 20);
    ASSERT(result->scores.size() == 3U);
    EXPECT(result->scores[0] == 1);
    EXPECT(result->scores[1] == 2);
    EXPECT(result->scores[2] == 3);
    EXPECT(result->addr.city == "sh");
    EXPECT(result->addr.zip == 200000);
}

ZEST_CASE(vector_of_strings_roundtrip) {
    std::vector<std::string> input{"hello", "world", "foo"};
    auto result = roundtrip(input);
    ASSERT(result);
    ASSERT(result->size() == 3U);
    EXPECT((*result)[0] == "hello");
    EXPECT((*result)[1] == "world");
    EXPECT((*result)[2] == "foo");
}

ZEST_CASE(vector_of_ints_roundtrip) {
    std::vector<std::int32_t> input{10, 20, 30};
    auto result = roundtrip(input);
    ASSERT(result);
    ASSERT(result->size() == 3U);
    EXPECT((*result)[0] == 10);
    EXPECT((*result)[1] == 20);
    EXPECT((*result)[2] == 30);
}

ZEST_CASE(optional_present_roundtrip) {
    struct with_opt {
        std::optional<std::int32_t> value;
    };

    with_opt input{.value = 42};
    auto result = roundtrip(input);
    ASSERT(result);
    ASSERT(result->value);
    EXPECT(*result->value == 42);
}

ZEST_CASE(optional_absent_roundtrip) {
    struct with_opt {
        std::optional<std::int32_t> value;
    };

    with_opt input{.value = std::nullopt};
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(!result->value);
}

ZEST_CASE(pair_roundtrip) {
    std::pair<std::int32_t, std::string> input{42, "hello"};
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(result->first == 42);
    EXPECT(result->second == "hello");
}

ZEST_CASE(tuple_roundtrip) {
    std::tuple<std::int32_t, std::string, double> input{42, "hello", 3.14};
    auto result = roundtrip(input);
    ASSERT(result);
    EXPECT(std::get<0>(*result) == 42);
    EXPECT(std::get<1>(*result) == "hello");
    EXPECT(std::get<2>(*result) == 3.14);
}

ZEST_CASE(variant_int_roundtrip) {
    struct with_var {
        std::variant<std::int32_t, std::string> value;
    };

    with_var input{.value = 42};
    auto result = roundtrip(input);
    ASSERT(result);
    ASSERT(std::holds_alternative<std::int32_t>(result->value));
    EXPECT(std::get<std::int32_t>(result->value) == 42);
}

ZEST_CASE(variant_string_roundtrip) {
    struct with_var {
        std::variant<std::int32_t, std::string> value;
    };

    with_var input{.value = std::string("hello")};
    auto result = roundtrip(input);
    ASSERT(result);
    ASSERT(std::holds_alternative<std::string>(result->value));
    EXPECT(std::get<std::string>(result->value) == "hello");
}

ZEST_CASE(map_roundtrip) {
    std::map<std::string, std::int32_t> input{
        {"a", 1},
        {"b", 2},
        {"c", 3}
    };
    auto result = roundtrip(input);
    ASSERT(result);
    ASSERT(result->size() == 3U);
    EXPECT(result->at("a") == 1);
    EXPECT(result->at("b") == 2);
    EXPECT(result->at("c") == 3);
}

ZEST_CASE(vector_of_structs_roundtrip) {
    std::vector<address> input{
        {.city = "tokyo", .zip = 100},
        {.city = "osaka", .zip = 200},
    };
    auto result = roundtrip(input);
    ASSERT(result);
    ASSERT(result->size() == 2U);
    EXPECT((*result)[0].city == "tokyo");
    EXPECT((*result)[0].zip == 100);
    EXPECT((*result)[1].city == "osaka");
    EXPECT((*result)[1].zip == 200);
}

};  // ZEST_SUITE(fbs_decode_visitor)

// ---------------------------------------------------------------------------
// Diagnostic tests: step-by-step encode → raw read → decode for failing cases
// ---------------------------------------------------------------------------

struct Basic {
    bool is_valid{};
    std::int32_t i32{};
    double f64{};
    std::string text;
    auto operator==(const Basic&) const -> bool = default;
};

using scalar_tuple_t = std::tuple<bool,
                                  char,
                                  std::int8_t,
                                  std::uint8_t,
                                  std::int16_t,
                                  std::uint16_t,
                                  std::int32_t,
                                  std::uint32_t,
                                  std::int64_t,
                                  std::uint64_t,
                                  float,
                                  double,
                                  std::string>;

ZEST_SUITE(fbs_decode_diagnostic) {

ZEST_CASE(diag_pair_scalar_tuple) {
    using pair_t = std::pair<scalar_tuple_t, scalar_tuple_t>;
    const scalar_tuple_t a{true,
                           'q',
                           std::int8_t(-12),
                           std::uint8_t(210),
                           std::int16_t(-1024),
                           std::uint16_t(4096),
                           std::int32_t(-123456),
                           std::uint32_t(123456),
                           std::int64_t(-9876543210LL),
                           std::uint64_t(9876543210ULL),
                           1.5F,
                           -9.25,
                           std::string("scalar-a")};
    const scalar_tuple_t b{false,
                           'z',
                           std::int8_t(-8),
                           std::uint8_t(8),
                           std::int16_t(-16),
                           std::uint16_t(16),
                           std::int32_t(-32),
                           std::uint32_t(32),
                           std::int64_t(-64),
                           std::uint64_t(64),
                           -0.75F,
                           -12.125,
                           std::string("scalar-b")};

    pair_t input{a, b};
    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    // Step 1: Read the root table
    const auto* data = encoded->data();
    const auto* root = ::flatbuffers::GetRoot<fbs::Table>(data);
    ASSERT(root != nullptr);

    // Step 2: Check that root has 2 pointer fields (pair elements)
    bool has_field0 = root->GetOptionalFieldOffset(fbs::detail::first_field) != 0;
    bool has_field1 =
        root->GetOptionalFieldOffset(fbs::detail::first_field + fbs::detail::field_step) != 0;
    EXPECT(has_field0);
    EXPECT(has_field1);

    // Step 3: Follow pointer for first element
    const auto* child0 = root->GetPointer<const fbs::Table*>(fbs::detail::first_field);
    ASSERT(child0 != nullptr);

    // Check first element's bool field (index 0)
    auto bool_val = child0->GetField<std::uint8_t>(fbs::detail::first_field, 0);
    EXPECT(bool_val == 1);  // true

    // Check first element's int32 field (index 6)
    auto i32_val =
        child0->GetField<std::int32_t>(fbs::detail::first_field + fbs::detail::field_step * 6, 0);
    EXPECT(i32_val == -123456);

    // Check first element's string field (index 12)
    const auto* str_ptr = child0->GetPointer<const fbs::String*>(fbs::detail::first_field +
                                                                 fbs::detail::field_step * 12);
    ASSERT(str_ptr != nullptr);
    EXPECT(std::string_view(str_ptr->data(), str_ptr->size()) == "scalar-a");

    // Step 4: Decode roundtrip
    pair_t output{};
    auto decode_result = fbs::from_bytes(*encoded, output);
    ASSERT(decode_result);
    EXPECT(std::get<0>(output.first) == true);          // bool
    EXPECT(std::get<1>(output.first) == 'q');           // char
    EXPECT(std::get<6>(output.first) == -123456);       // int32
    EXPECT(std::get<12>(output.first) == "scalar-a");   // string
    EXPECT(std::get<0>(output.second) == false);        // bool
    EXPECT(std::get<12>(output.second) == "scalar-b");  // string
    EXPECT(input == output);
}

ZEST_CASE(diag_variant_vector_int) {
    using var_t = std::variant<std::tuple<int, std::string>, std::vector<int>, Basic>;
    var_t input{
        std::vector<int>{1, 2, 3}
    };

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    // Step 1: Read root table (variant table)
    const auto* data = encoded->data();
    const auto* root = ::flatbuffers::GetRoot<fbs::Table>(data);
    ASSERT(root != nullptr);

    // Step 2: Read variant tag at first_field
    auto tag = root->GetField<std::uint32_t>(fbs::detail::first_field, 0);
    EXPECT(tag == 1U);  // vector<int> is at index 1

    // Step 3: Compute payload slot
    auto payload_slot =
        static_cast<fbs::voffset_t>(fbs::detail::first_field + fbs::detail::field_step * (tag + 1));

    // Step 4: Check what's at the payload slot
    bool has_payload = root->GetOptionalFieldOffset(payload_slot) != 0;
    EXPECT(has_payload);

    // Step 5: Try reading as vector<int32_t> directly
    const auto* vec = root->GetPointer<const fbs::Vector<std::int32_t>*>(payload_slot);
    if(vec != nullptr) {
        EXPECT(vec->size() == 3U);
        if(vec->size() >= 3) {
            EXPECT(vec->Get(0) == 1);
            EXPECT(vec->Get(1) == 2);
            EXPECT(vec->Get(2) == 3);
        }
    } else {
        // Maybe it's wrapped in a table?
        const auto* wrapper = root->GetPointer<const fbs::Table*>(payload_slot);
        if(wrapper != nullptr) {
            const auto* inner_vec =
                wrapper->GetPointer<const fbs::Vector<std::int32_t>*>(fbs::detail::first_field);
            ASSERT(inner_vec != nullptr);
            EXPECT(inner_vec->size() == 3U);
        }
    }

    // Step 6: Decode roundtrip
    var_t output{};
    auto decode_result = fbs::from_bytes(*encoded, output);
    ASSERT(decode_result);
    ASSERT(std::holds_alternative<std::vector<int>>(output));
    auto& decoded_vec = std::get<std::vector<int>>(output);
    ASSERT(decoded_vec.size() == 3U);
    EXPECT(decoded_vec[0] == 1);
    EXPECT(decoded_vec[1] == 2);
    EXPECT(decoded_vec[2] == 3);
}

ZEST_CASE(diag_variant_monostate) {
    using var_t = std::variant<std::monostate, int, double, std::string, Basic>;
    var_t input{std::in_place_index<0>};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    const auto* data = encoded->data();
    const auto* root = ::flatbuffers::GetRoot<fbs::Table>(data);
    ASSERT(root != nullptr);

    auto tag = root->GetField<std::uint32_t>(fbs::detail::first_field, 0);
    EXPECT(tag == 0U);

    var_t output{42};  // start with different value
    auto decode_result = fbs::from_bytes(*encoded, output);
    ASSERT(decode_result);
    EXPECT(output.index() == 0U);
}

ZEST_CASE(diag_variant_basic) {
    using var_t = std::variant<std::monostate, int, double, std::string, Basic>;
    var_t input{
        Basic{.is_valid = true, .i32 = 64, .f64 = 2.5, .text = "variant-basic"}
    };

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    const auto* data = encoded->data();
    const auto* root = ::flatbuffers::GetRoot<fbs::Table>(data);
    ASSERT(root != nullptr);

    auto tag = root->GetField<std::uint32_t>(fbs::detail::first_field, 0);
    EXPECT(tag == 4U);  // Basic is at index 4

    var_t output{};
    auto decode_result = fbs::from_bytes(*encoded, output);
    ASSERT(decode_result);
    ASSERT(std::holds_alternative<Basic>(output));
    EXPECT(std::get<Basic>(output).is_valid == true);
    EXPECT(std::get<Basic>(output).i32 == 64);
    EXPECT(std::get<Basic>(output).text == "variant-basic");
}

ZEST_CASE(diag_tagged_ext_variant_int) {
    using ext_t = annotate<ext_tag_annotation>::type<std::variant<int, std::string, Basic>>;
    ext_t input{42};

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    const auto* data = encoded->data();
    const auto* root = ::flatbuffers::GetRoot<fbs::Table>(data);
    ASSERT(root != nullptr);

    // For non-human-readable, tagged variants use the same encoded format as untagged
    auto tag = root->GetField<std::uint32_t>(fbs::detail::first_field, 0);
    EXPECT(tag == 0U);  // int is at index 0

    auto payload_slot =
        static_cast<fbs::voffset_t>(fbs::detail::first_field + fbs::detail::field_step * 1);
    auto int_val = root->GetField<std::int32_t>(payload_slot, 0);
    EXPECT(int_val == 42);

    ext_t output{};
    auto decode_result = fbs::from_bytes(*encoded, output);
    ASSERT(decode_result);
    ASSERT(std::holds_alternative<int>(meta::annotated_value(output)));
    EXPECT(std::get<int>(meta::annotated_value(output)) == 42);
}

ZEST_CASE(diag_tuple_01_pair_int_string) {
    auto r = roundtrip(std::pair<int, std::string>{9, "pair"});
    ASSERT(r);
    EXPECT(r->first == 9);
    EXPECT(r->second == "pair");
}

ZEST_CASE(diag_tuple_02_pair_u64_bool) {
    auto r = roundtrip(std::pair<std::uint64_t, bool>{42ULL, true});
    ASSERT(r);
    EXPECT(r->first == 42ULL);
    EXPECT(r->second == true);
}

ZEST_CASE(diag_tuple_03_pair_basic_basic) {
    auto r = roundtrip(std::pair<Basic, Basic>{
        Basic{true,  11,  1.5,  "lhs"},
        Basic{false, -22, -2.5, "rhs"}
    });
    ASSERT(r);
    EXPECT(r->first.i32 == 11);
    EXPECT(r->second.i32 == -22);
}

ZEST_CASE(diag_tuple_04_tuple_int_bool_string) {
    auto r = roundtrip(std::tuple<int, bool, std::string>{7, true, "tuple"});
    ASSERT(r);
    EXPECT(std::get<0>(*r) == 7);
    EXPECT(std::get<1>(*r) == true);
    EXPECT(std::get<2>(*r) == "tuple");
}

ZEST_CASE(diag_tuple_05_scalar_tuple) {
    scalar_tuple_t input{true,
                         'q',
                         std::int8_t(-12),
                         std::uint8_t(210),
                         std::int16_t(-1024),
                         std::uint16_t(4096),
                         std::int32_t(-123456),
                         std::uint32_t(123456),
                         std::int64_t(-9876543210LL),
                         std::uint64_t(9876543210ULL),
                         1.5F,
                         -9.25,
                         std::string("all-scalars-a")};
    auto r = roundtrip(input);
    ASSERT(r);
    EXPECT(input == *r);
}

ZEST_CASE(diag_tuple_06_tuple_basic_array_pair) {
    using T = std::tuple<Basic, std::array<int, 3>, std::pair<std::uint32_t, float>>;
    T input{
        Basic{true, 33, 3.75, "tuple-struct"},
        std::array<int, 3>{1, 3, 5},
        std::pair<std::uint32_t, float>{99U, 6.25F}
    };

    auto encoded = fbs::to_bytes(input);
    ASSERT(encoded);

    const auto* root = ::flatbuffers::GetRoot<fbs::Table>(encoded->data());
    ASSERT(root != nullptr);

    // Root is a tuple table with 3 pointer fields
    // Field 0 (Basic): table pointer at slot 4
    // Field 1 (array<int,3>): table pointer at slot 6
    // Field 2 (pair<uint32_t,float>): table pointer at slot 8
    const auto* child1 =
        root->GetPointer<const fbs::Table*>(fbs::detail::first_field + fbs::detail::field_step * 1);
    ASSERT(child1 != nullptr);

    // child1 should be a sub-table with 3 int fields at slots 4, 6, 8
    auto v0 = child1->GetField<std::int32_t>(fbs::detail::first_field, 0);
    auto v1 = child1->GetField<std::int32_t>(fbs::detail::first_field + fbs::detail::field_step, 0);
    auto v2 =
        child1->GetField<std::int32_t>(fbs::detail::first_field + fbs::detail::field_step * 2, 0);
    EXPECT(v0 == 1);
    EXPECT(v1 == 3);
    EXPECT(v2 == 5);

    // Manual decode step-by-step
    T output{};
    auto decode_result = fbs::from_bytes(*encoded, output);
    ASSERT(decode_result);

    // Check each tuple element
    EXPECT(std::get<0>(output).is_valid == true);
    EXPECT(std::get<0>(output).i32 == 33);
    EXPECT(std::get<0>(output).text == "tuple-struct");
    EXPECT(std::get<1>(output)[0] == 1);
    EXPECT(std::get<1>(output)[1] == 3);
    EXPECT(std::get<1>(output)[2] == 5);
    EXPECT(std::get<2>(output).first == 99U);
    EXPECT(std::get<2>(output).second == 6.25F);

    // Manual decode of element 1 via FieldReader directly
    auto verifier = fbs::detail::make_verifier(encoded->data(), encoded->size());
    ASSERT(verifier.VerifyOffset(0) != 0U);
    ASSERT(root->VerifyTableStart(verifier));
    fbs::decode_detail::FieldReader vr{
        .tbl = root,
        .slot = static_cast<fbs::voffset_t>(fbs::detail::first_field + fbs::detail::field_step),
        .verifier = &verifier};
    // This should follow the pointer at slot 6 to get the sub-table
    std::array<int, 3> manual_arr{};
    bool manual_ok = vr.visit_tuple(manual_arr, [&](auto& sv) -> bool {
        for(std::size_t i = 0; i < 3; ++i) {
            bool ok =
                sv.visit_element([&](auto& ev) -> bool { return ev.visit_int(manual_arr[i]); });
            if(!ok)
                return false;
        }
        return true;
    });
    ASSERT(manual_ok);
    EXPECT(manual_arr[0] == 1);
    EXPECT(manual_arr[1] == 3);
    EXPECT(manual_arr[2] == 5);
}

ZEST_CASE(diag_tuple_07_array_int3) {
    auto r = roundtrip(std::array<int, 3>{4, 5, 6});
    ASSERT(r);
    EXPECT((*r)[0] == 4);
    EXPECT((*r)[1] == 5);
    EXPECT((*r)[2] == 6);
}

ZEST_CASE(diag_tuple_08_array_basic2) {
    auto r = roundtrip(std::array<Basic, 2>{
        Basic{true,  101,  10.1,  "arr-a"},
        Basic{false, -202, -20.2, "arr-b"}
    });
    ASSERT(r);
    EXPECT((*r)[0].i32 == 101);
    EXPECT((*r)[1].i32 == -202);
}

ZEST_CASE(diag_tuple_09_pair_scalar_tuple_scalar_tuple) {
    // Already tested - just confirm it still passes
    using pair_t = std::pair<scalar_tuple_t, scalar_tuple_t>;
    const scalar_tuple_t a{true,
                           'q',
                           std::int8_t(-12),
                           std::uint8_t(210),
                           std::int16_t(-1024),
                           std::uint16_t(4096),
                           std::int32_t(-123456),
                           std::uint32_t(123456),
                           std::int64_t(-9876543210LL),
                           std::uint64_t(9876543210ULL),
                           1.5F,
                           -9.25,
                           std::string("all-scalars-a")};
    const scalar_tuple_t b{false,
                           'z',
                           std::int8_t(-8),
                           std::uint8_t(8),
                           std::int16_t(-16),
                           std::uint16_t(16),
                           std::int32_t(-32),
                           std::uint32_t(32),
                           std::int64_t(-64),
                           std::uint64_t(64),
                           -0.75F,
                           -12.125,
                           std::string("all-scalars-b")};
    auto r = roundtrip(pair_t{a, b});
    ASSERT(r);
    EXPECT(r->first == a);
    EXPECT(r->second == b);
}

ZEST_CASE(diag_tuple_10_array_scalar_tuple2) {
    const scalar_tuple_t a{true,
                           'q',
                           std::int8_t(-12),
                           std::uint8_t(210),
                           std::int16_t(-1024),
                           std::uint16_t(4096),
                           std::int32_t(-123456),
                           std::uint32_t(123456),
                           std::int64_t(-9876543210LL),
                           std::uint64_t(9876543210ULL),
                           1.5F,
                           -9.25,
                           std::string("all-scalars-a")};
    const scalar_tuple_t b{false,
                           'z',
                           std::int8_t(-8),
                           std::uint8_t(8),
                           std::int16_t(-16),
                           std::uint16_t(16),
                           std::int32_t(-32),
                           std::uint32_t(32),
                           std::int64_t(-64),
                           std::uint64_t(64),
                           -0.75F,
                           -12.125,
                           std::string("all-scalars-b")};
    auto r = roundtrip(std::array<scalar_tuple_t, 2>{a, b});
    ASSERT(r);
    EXPECT((*r)[0] == a);
    EXPECT((*r)[1] == b);
}

ZEST_CASE(diag_struct_with_array_field) {
    struct WithArray {
        std::int32_t x;
        std::array<float, 3> arr;
        std::string s;
    };

    WithArray input{
        .x = 42,
        .arr = {1.5F, -2.25F, 0.0F},
        .s = "test"
    };
    auto r = roundtrip(input);
    ASSERT(r);
    EXPECT(r->x == 42);
    EXPECT(r->arr[0] == 1.5F);
    EXPECT(r->arr[1] == -2.25F);
    EXPECT(r->arr[2] == 0.0F);
    EXPECT(r->s == "test");
}

ZEST_CASE(diag_complex_01_scalars) {
    auto r = roundtrip(standard_case::make_scalars());
    ASSERT(r);
    EXPECT(r->b == true);
    EXPECT(r->s == "hello scalars");
}

ZEST_CASE(diag_complex_02_nested_containers) {
    auto input = standard_case::make_nested_containers();
    auto r = roundtrip(input);
    ASSERT(r);
    EXPECT(input == *r);
}

ZEST_CASE(diag_complex_03_empty_containers) {
    auto input = standard_case::make_empty_containers();
    auto r = roundtrip(input);
    ASSERT(r);
    EXPECT(input == *r);
}

ZEST_CASE(diag_complex_04_ultimate) {
    auto input = standard_case::make_ultimate();
    auto r = roundtrip(input);
    ASSERT(r);
    EXPECT(input.basic == r->basic);
    EXPECT(input.compound.string_list == r->compound.string_list);
    EXPECT(input.compound.fixed_array == r->compound.fixed_array);
    EXPECT(input.compound.heterogeneous_tuple == r->compound.heterogeneous_tuple);
    EXPECT(input.compound == r->compound);
    EXPECT(input.hard_map == r->hard_map);
    EXPECT(input == *r);
}

ZEST_CASE(diag_complex_05_ultimate_monostate) {
    auto input = standard_case::make_ultimate();
    input.adts.multi_variant = std::monostate{};
    input.nullables.opt_value.reset();
    input.nullables.heap_allocated.reset();
    auto r = roundtrip(input);
    ASSERT(r);
    EXPECT(input == *r);
}

ZEST_CASE(diag_complex_06_ultimate_int_variant) {
    auto input = standard_case::make_ultimate();
    input.adts.multi_variant = 123;
    auto r = roundtrip(input);
    ASSERT(r);
    EXPECT(input == *r);
}

ZEST_CASE(diag_complex_07_ultimate_string_variant) {
    auto input = standard_case::make_ultimate();
    input.adts.multi_variant = std::string("variant-text");
    auto r = roundtrip(input);
    ASSERT(r);
    EXPECT(input == *r);
}

};  // ZEST_SUITE(fbs_decode_diagnostic)

}  // namespace

}  // namespace kota::codec

#endif
