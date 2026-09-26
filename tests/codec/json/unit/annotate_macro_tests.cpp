#include <cstdio>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

// Downstream regression fixtures: outside namespace kota, `rename` must not be
// ambiguous with ::rename from <cstdio>, and the macro must work inside class
// templates.
namespace kotatsu_annotate_downstream {

struct config {
    KOTATSU_ANNOTATE(rename = "compileCommands")
    <int> compile_commands = 0;
};

template <typename T>
struct box {
    KOTATSU_ANNOTATE(alias = {"v"})
    <T> value;
};

}  // namespace kotatsu_annotate_downstream

namespace kota::codec {

namespace {

using json::from_string;
using json::to_string;

struct negative_pred {
    constexpr bool operator()(const int& value, bool is_serialize) const {
        return is_serialize && value < 0;
    }
};

/// Adapter: encode int as its decimal string representation.
struct int_string_adapter {
    using type = std::string;

    static auto to(int value) -> std::string {
        return std::to_string(value);
    }

    static auto from(std::string encoded) -> int {
        return encoded.empty() ? 0 : std::stoi(encoded);
    }
};

/// Strong id encoded as its underlying string via `as`.
struct user_id {
    std::string raw;

    user_id() = default;

    user_id(std::string s) : raw(std::move(s)) {}

    operator std::string() const {
        return raw;
    }
};

struct profile_info {
    std::string first;
    int age = 0;
};

struct defaulted_payload {
    int id = 0;

    KOTATSU_ANNOTATE(defaulted = true)
    <int> retries = 3;
};

struct custom_skip_payload {
    KOTATSU_ANNOTATE(skip_if = type<negative_pred>)
    <int> score = 0;
};

struct builtin_skip_payload {
    KOTATSU_ANNOTATE(skip_if = skip_when::empty)
    <std::vector<int>> tags;

    KOTATSU_ANNOTATE(skip_if = skip_when::default_value)
    <int> generation = 0;
};

struct adapted_payload {
    KOTATSU_ANNOTATE(with = type<int_string_adapter>)
    <int> encoded = 0;

    KOTATSU_ANNOTATE(as = type<std::string>)
    <user_id> owner;
};

struct flattened_payload {
    KOTATSU_ANNOTATE(flatten = true)
    <profile_info> profile;
};

struct circle {
    double radius = 0;
};

struct rect {
    double width = 0;
};

KOTATSU_ANNOTATION(shape_annotation, tag = "kind", tag_names = {"circle", "rect"});
using shape = meta::annotate<shape_annotation>::type<std::variant<circle, rect>>;

KOTATSU_ANNOTATION(camel_annotation, rename_all = casing::lower_camel, deny_unknown_fields = true);

struct wide_payload {
    std::string user_name;
    int user_age = 0;
};

using camel_payload = meta::annotate<camel_annotation>::type<wide_payload>;

KOTATSU_ANNOTATION(camel_choice_annotation, rename_all = casing::lower_camel);
using camel_choice = meta::annotate<camel_choice_annotation>::type<std::variant<wide_payload, int>>;

struct shape_holder {
    KOTATSU_ANNOTATE(tag = "kind", tag_names = {"circle", "rect"})
    <std::variant<circle, rect>> shape;
};

struct nested_holder {
    KOTATSU_ANNOTATE(rename_all = casing::upper_snake, deny_unknown_fields = true)
    <wide_payload> inner;
};

ZEST_SUITE(codec_json_annotate_macro) {

ZEST_CASE(defaulted_field_may_be_absent) {
    defaulted_payload parsed{};
    auto status = from_string(R"({"id":1})", parsed);
    ASSERT(status);
    EXPECT(parsed.id == 1);
    EXPECT(parsed.retries == 3);

    defaulted_payload strict{};
    auto missing_required = from_string(R"({"retries":9})", strict);
    EXPECT(!missing_required);
}

ZEST_CASE(custom_skip_predicate_applies) {
    custom_skip_payload input{};
    input.score = -5;
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({})");

    input.score = 5;
    encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"score":5})");
}

ZEST_CASE(builtin_skip_conditions_apply) {
    builtin_skip_payload input{};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({})");

    input.tags = std::vector{1, 2};
    input.generation = 5;
    encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"tags":[1,2],"generation":5})");

    builtin_skip_payload parsed{};
    parsed.generation = 7;
    auto absent_ok = from_string(R"({})", parsed);
    ASSERT(absent_ok);
    EXPECT(parsed.tags.empty());
    EXPECT(parsed.generation == 7);

    auto status = from_string(R"({"tags":[3],"generation":9})", parsed);
    ASSERT(status);
    EXPECT(parsed.tags == std::vector{3});
    EXPECT(parsed.generation == 9);
}

ZEST_CASE(with_adapter_and_as_target_roundtrip) {
    adapted_payload input{};
    input.encoded = 42;
    input.owner = user_id{"alice"};

    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"encoded":"42","owner":"alice"})");

    adapted_payload parsed{};
    auto status = from_string(R"({"encoded":"17","owner":"bob"})", parsed);
    ASSERT(status);
    EXPECT(parsed.encoded == 17);
    EXPECT(parsed.owner.raw == "bob");
}

ZEST_CASE(macro_works_outside_kota_namespace) {
    kotatsu_annotate_downstream::config input{};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"compileCommands":0})");

    kotatsu_annotate_downstream::box<int> boxed{};
    auto status = from_string(R"({"v":7})", boxed);
    ASSERT(status);
    EXPECT(boxed.value == 7);
}

ZEST_CASE(named_annotation_tags_variant) {
    shape input{rect{2.5}};
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"kind":"rect","width":2.5})");

    shape parsed;
    auto status = from_string(R"({"kind":"circle","radius":1.5})", parsed);
    ASSERT(status);
    ASSERT(parsed.index() == 0u);
    EXPECT(std::get<circle>(parsed).radius == 1.5);
}

ZEST_CASE(named_annotation_renames_and_denies_unknown) {
    camel_payload input;
    input.user_name = "alice";
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"userName":"alice","userAge":0})");

    camel_payload parsed;
    auto status = from_string(R"({"userName":"bob","userAge":3})", parsed);
    ASSERT(status);
    EXPECT(parsed.user_name == "bob");
    EXPECT(parsed.user_age == 3);

    auto unknown = from_string(R"({"userName":"bob","extra":1})", parsed);
    EXPECT(!unknown);
}

ZEST_CASE(rename_all_on_untagged_variant_is_inert) {
    // The codec merges rename_all into the config only when crossing a
    // reflectable annotated node; an untagged variant is not one, so its
    // alternatives encode with their declared names — and type_info must
    // describe exactly that.
    camel_choice input{
        wide_payload{.user_name = "alice", .user_age = 1}
    };
    auto encoded = to_string(input);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"user_name":"alice","user_age":1})");

    const auto& info =
        static_cast<const meta::variant_type_info&>(meta::type_info_of<camel_choice>());
    const auto& alt = static_cast<const meta::struct_type_info&>(info.alternatives[0]());
    EXPECT(alt.fields[0].name == "user_name");
}

ZEST_CASE(field_annotation_accepts_struct_entries) {
    shape_holder holder;
    holder.shape = circle{4.0};
    auto encoded = to_string(holder);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"shape":{"kind":"circle","radius":4.0}})");

    shape_holder parsed;
    auto status = from_string(R"({"shape":{"kind":"rect","width":6.0}})", parsed);
    ASSERT(status);
    ASSERT(parsed.shape.index() == 1u);
    EXPECT(std::get<rect>(parsed.shape).width == 6.0);
}

ZEST_CASE(field_annotation_merges_struct_config) {
    nested_holder holder;
    holder.inner.user_name = "alice";
    auto encoded = to_string(holder);
    ASSERT(encoded);
    EXPECT(*encoded == R"({"inner":{"USER_NAME":"alice","USER_AGE":0}})");

    nested_holder parsed;
    auto status = from_string(R"({"inner":{"USER_NAME":"bob","USER_AGE":2}})", parsed);
    ASSERT(status);
    EXPECT(parsed.inner.user_name == "bob");
    EXPECT(parsed.inner.user_age == 2);

    auto unknown = from_string(R"({"inner":{"USER_NAME":"bob","EXTRA":1}})", parsed);
    EXPECT(!unknown);
}

ZEST_CASE(annotated_and_bare_use_share_type_info) {
    // The spec attr is a field-local attr: it must not fork the type_info
    // instance of the underlying type.
    using annotated = decltype(flattened_payload{}.profile);
    EXPECT(&meta::type_info_of<annotated>() == &meta::type_info_of<profile_info>());
}

};  // ZEST_SUITE(codec_json_annotate_macro)

}  // namespace

}  // namespace kota::codec
