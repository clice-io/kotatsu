#include <cstdint>
#include <type_traits>

#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/format.h"
#include "kotatsu/codec/repr.h"

namespace kotatsu::codec {

namespace {

/// A backend's format, and formats extending it.
struct backend_format : extend<format> {
    constexpr static bool human_readable = false;
};

struct user_format : extend<backend_format> {};

struct leaf_format : extend<user_format> {
    using rename_all = naming::rename_policy::lower_camel;
};

/// Types with reprs for some of the formats.
struct Everywhere {};

struct ForBackend {};

struct ForUser {};

/// A visitor that carries a format.
template <typename F>
struct Visitor {
    using format = F;
};

}  // namespace

template <>
struct repr<Everywhere> {
    using type = int;
};

template <>
struct repr<ForBackend, backend_format> {
    using type = int;
};

template <>
struct repr<ForBackend> {
    using type = long;
};

template <>
struct repr<ForUser, user_format> {
    using type = int;
};

namespace {

ZEST_SUITE(codec_format) {

ZEST_CASE(a_format_inherits_what_it_does_not_declare) {
    ZSTATIC_EXPECT(!user_format::human_readable);
    ZSTATIC_EXPECT(!leaf_format::presence_all.has_value());
    ZSTATIC_EXPECT(std::is_same_v<user_format::rename_all, naming::rename_policy::identity>);
    ZSTATIC_EXPECT(std::is_same_v<leaf_format::rename_all, naming::rename_policy::lower_camel>);
    ZSTATIC_EXPECT(std::is_same_v<leaf_format::base, user_format>);
}

ZEST_CASE(a_repr_applies_along_the_format_chain) {
    ZSTATIC_EXPECT(std::is_same_v<repr_for<Everywhere, leaf_format>, repr<Everywhere>>);
    // The nearest format's repr wins over the one for every format.
    ZSTATIC_EXPECT(
        std::is_same_v<repr_for<ForBackend, leaf_format>, repr<ForBackend, backend_format>>);
    ZSTATIC_EXPECT(std::is_same_v<repr_for<ForBackend, format>, repr<ForBackend>>);
    // A format's repr applies under the formats extending it, not above it.
    ZSTATIC_EXPECT(std::is_same_v<repr_for<ForUser, leaf_format>, repr<ForUser, user_format>>);
    ZSTATIC_EXPECT(!has_repr<ForUser, backend_format>);
}

ZEST_CASE(a_visitor_carries_its_format) {
    ZSTATIC_EXPECT(std::is_same_v<format_of_t<Visitor<leaf_format>>, leaf_format>);
    ZSTATIC_EXPECT(std::is_void_v<format_of_t<int>>);
    // A format carries no format of its own.
    ZSTATIC_EXPECT(std::is_void_v<format_of_t<leaf_format>>);
}

};  // ZEST_SUITE(codec_format)

}  // namespace

}  // namespace kotatsu::codec
