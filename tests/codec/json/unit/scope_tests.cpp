#include <string>
#include <tuple>
#include <type_traits>
#include <variant>

#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/json/json.h"
#include "kotatsu/codec/json/schema.h"
#include "kotatsu/codec/macro.h"

// Where a node's attributes apply: a field's over its type's, a flattened
// struct's to its own fields, an enum's names, a named variant's repr, a
// tagged variant's deny_unknown_fields, and a scope that restates the format.

namespace kotatsu::codec {

namespace {

KANNOTATE_TYPE(struct Camel, rename_all = Casing::LowerCamel);

struct Camel {
    int item_count = 1;
};

/// A field's rename_all stands over its type's.
struct HoldsCamel {
    KANNOTATE(&inner, rename_all = Casing::UpperSnake)
    Camel inner;
};

/// Its fields shout, the flattened struct's keep their own spelling.
KANNOTATE_TYPE(struct Paged, rename_all = Casing::UpperSnake);

struct Paged {
    int max_count = 2;
    KANNOTATE(&paging, flatten = true)
    Camel paging;
};

enum class Level { low_water, high_water };

struct HoldsLevel {
    KANNOTATE(&level, with = type<enum_names<>>, rename_all = Casing::UpperSnake)
    Level level = Level::high_water;
};

/// A class derived from a std::variant whose repr writes it as text.
struct Token : std::variant<int, std::string> {
    using variant::variant;
};

struct HoldsToken {
    Token token;
};

KANNOTATE_TYPE(struct Strict, tag = "t", content = "c", deny_unknown_fields = true);

struct Strict : std::variant<int, std::string> {
    using variant::variant;
};

struct Circle {
    double radius = 0;
};

KANNOTATE_TYPE(struct StrictShape, tag = "kind", deny_unknown_fields = true);

struct StrictShape : std::variant<Circle> {
    using variant::variant;
};

/// What two types travel as, declared by one repr and written by hand by the
/// other.
struct Counted {
    int item_count = 3;
};

struct Declared {
    int count = 3;
};

struct ByHand {
    int count = 3;
};

/// A field's struct attributes scope what its type travels as, whichever
/// form the repr takes.
struct HoldsReprs {
    KANNOTATE(&declared, rename_all = Casing::UpperSnake)
    Declared declared;
    KANNOTATE(&by_hand, rename_all = Casing::UpperSnake, deny_unknown_fields = true)
    ByHand by_hand;
};

KANNOTATE_TYPE(struct Restated, rename_all = Casing::Identity);

struct Restated {
    int item_count = 0;
};

}  // namespace

template <>
struct repr<Token> {
    using type = std::string;

    static type to(const Token& token) {
        return token.index() == 0 ? std::to_string(std::get<0>(token)) : std::get<1>(token);
    }

    static Token from(const type& text) {
        return Token(text);
    }
};

template <>
struct repr<Declared> {
    using type = Counted;

    static type to(const Declared& value) {
        return {.item_count = value.count};
    }

    static Declared from(const type& counted) {
        return {.count = counted.item_count};
    }
};

template <>
struct repr<ByHand> {
    using type = Counted;

    template <typename F>
    static bool encode(auto& vis, const ByHand& value) {
        return encode_value<F>(vis, Counted{.item_count = value.count});
    }

    template <typename F>
    static bool decode(auto& vis, ByHand& out) {
        Counted counted;
        if(!decode_value<F>(vis, counted)) {
            return false;
        }
        out.count = counted.item_count;
        return true;
    }
};

namespace {

ZEST_SUITE(codec_json_scope) {

ZEST_CASE(field_attributes_stand_over_the_type_s) {
    const auto text = json::to_string(HoldsCamel{});
    ZEXPECT(text == R"({"inner":{"ITEM_COUNT":1}})");
    HoldsCamel back;
    back.inner.item_count = 0;
    ZASSERT(json::from_string(*text, back));
    ZEXPECT(back.inner.item_count == 1);
    // The schema names the field as the document does.
    const auto schema = json::schema_string<HoldsCamel>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("ITEM_COUNT")"));
    ZEXPECT(!zest::contains(*schema, R"("itemCount")"));
}

ZEST_CASE(flattened_fields_keep_their_own_spelling) {
    ZEXPECT(json::to_string(Paged{}) == R"({"MAX_COUNT":2,"itemCount":1})");
}

ZEST_CASE(field_rename_all_spells_its_enum) {
    const auto text = json::to_string(HoldsLevel{});
    ZEXPECT(text == R"({"level":"HIGH_WATER"})");
    HoldsLevel back{.level = Level::low_water};
    ZASSERT(json::from_string(*text, back));
    ZEXPECT(back.level == Level::high_water);
}

ZEST_CASE(named_variant_field_takes_its_repr) {
    // As a field, as a root: the repr writes it.
    ZEXPECT(json::to_string(Token(7)) == R"("7")");
    ZEXPECT(json::to_string(HoldsToken{Token(7)}) == R"({"token":"7"})");
    const auto schema = json::schema_string<HoldsToken>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("token":{"type":"string"})"));
}

ZEST_CASE(tagged_variant_denies_unknown_keys) {
    Strict adjacent;
    auto status = json::from_string(R"({"t":"int","c":1,"x":2})", adjacent);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "unknown field 'x'");

    StrictShape internal;
    status = json::from_string(R"({"kind":"Circle","radius":1,"x":2})", internal);
    ZASSERT(!status);
    ZEXPECT(status.error().message == "unknown field 'x'");

    const auto schema = json::schema_string<StrictShape>();
    ZASSERT(schema);
    ZEXPECT(zest::contains(*schema, R"("additionalProperties":false)"));
}

ZEST_CASE(reprs_take_the_field_s_scope) {
    const auto text = json::to_string(HoldsReprs{});
    ZEXPECT(text == R"({"declared":{"ITEM_COUNT":3},"by_hand":{"ITEM_COUNT":3}})");
    HoldsReprs back{.declared = {.count = 0}, .by_hand = {.count = 0}};
    ZASSERT(json::from_string(*text, back));
    ZEXPECT(back.declared.count == 3);
    ZEXPECT(back.by_hand.count == 3);
    ZEXPECT(
        !json::from_string(R"({"declared":{"ITEM_COUNT":3},"by_hand":{"ITEM_COUNT":3,"extra":1}})",
                           back));
}

ZEST_CASE(a_scope_that_restates_the_format_is_the_format) {
    // One type_info, so one $def, wherever the struct stands.
    ZEXPECT(zest::type_eq<node_format_t<format, Restated, meta::attrs_of_t<Restated>>, format>());
    ZEXPECT(&type_info::of<Restated>() == &detail::struct_info<Restated, format>());
}

};  // ZEST_SUITE(codec_json_scope)

}  // namespace

}  // namespace kotatsu::codec
