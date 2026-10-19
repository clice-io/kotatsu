#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/meta/attrs.h"
#include "kota/meta/struct.h"

// Zero-copy blobs of reflected structs: a value laid out once in one
// contiguous buffer and read in place as a C++ struct.
//
// A struct holding strings or sequences is declared once, as a template over
// its sequence type:
//
//     template <template <typename> typename Seq>
//     struct IndexOf {
//         std::uint32_t version = 0;
//         Seq<char> name;
//         Seq<std::uint64_t> hashes;
//     };
//     using Index = IndexOf<blob::Owned>;
//
// `Index` owns its contents (std::string, std::vector): a writer fills it and
// to_bytes lays it out. `blob::view_t<Index>`, which is `IndexOf<blob::Vec>`,
// is the same struct read in place, each string or sequence a blob::Vec: two
// u32, the distance from the Vec to its first element and the element count.
// A struct of scalars only is its own view.
//
// A blob is a header (a magic and the fingerprint of the root's view layout),
// then the root's view, then every string's bytes and every sequence's
// elements, out of line, in pre-order: a record's fields in declaration
// order, a sequence's elements in order, each part followed by its own
// out-of-line parts before the next one's. Strings sit unaligned, sequences
// at `alignment`, so a byte sequence holding another blob is read in place.
// A scalar (an arithmetic type other than long double, an enum with a fixed
// underlying type) is its native little-endian image.
//
// The layout is a function of the type alone: there is no Config, and
// neither annotations nor meta::repr apply, since the view is the struct
// itself. It is canonical: to_bytes writes a value as exactly one blob,
// padding zeroed, so the bytes' hash identifies the value. view checks
// untrusted bytes against that layout: every Vec pointing where to_bytes puts
// its part, every bool (or enum over bool) 0 or 1, nothing past the last
// part. Padding is not read, so it is not checked either: bytes that pass may
// differ from what to_bytes writes for the value they hold. Verified bytes
// are then read with no check at all. There is no schema evolution: the
// fingerprint rejects a blob of another layout, and a reader never sees one.

namespace kota::codec::blob {

static_assert(std::endian::native == std::endian::little, "blobs are little-endian images");

/// The alignment a blob's bytes must start at, and every sequence's elements
/// start at inside it.
constexpr inline std::size_t alignment = 8;

/// A string (`Vec<char>`) or a sequence read in place. Its offset counts from
/// its own address, so it is never copied out of the blob: bind a reference to
/// it, or read it as a std::span or a std::string_view.
template <typename T>
class Vec {
public:
    using value_type = T;

    Vec() = default;

    Vec(const Vec&) = delete;

    Vec& operator=(const Vec&) = delete;

    /// Null when empty.
    const T* data() const {
        return length == 0 ? nullptr : first();
    }

    std::uint32_t size() const {
        return length;
    }

    bool empty() const {
        return length == 0;
    }

    const T& operator[](std::size_t i) const {
        return first()[i];
    }

    const T& front() const {
        return first()[0];
    }

    const T& back() const {
        return first()[length - 1];
    }

    const T* begin() const {
        return data();
    }

    const T* end() const {
        return data() + length;
    }

    operator std::string_view() const
        requires std::is_same_v<T, char> {
        return {data(), length};
    }

    std::string str() const
        requires std::is_same_v<T, char> {
        return {data(), length};
    }

private:
    const T* first() const {
        return reinterpret_cast<const T*>(reinterpret_cast<const std::byte*>(this) + offset);
    }

    std::uint32_t offset = 0;

    std::uint32_t length = 0;
};

/// The owning sequence of a struct template's writer form: std::string for
/// characters, std::vector otherwise.
template <typename T>
using Owned = std::conditional_t<std::is_same_v<T, char>, std::string, std::vector<T>>;

namespace detail {

template <typename T>
struct view_of {
    using type = T;
};

template <>
struct view_of<std::string> {
    using type = Vec<char>;
};

template <typename E>
struct view_of<std::vector<E>> {
    using type = Vec<typename view_of<E>::type>;
};

template <template <template <typename> typename> typename Of>
struct view_of<Of<Owned>> {
    using type = Of<Vec>;
};

}  // namespace detail

/// What a T is read in place as: Vec for a string or a sequence, the same
/// struct template over Vec for a record holding them, T itself otherwise.
template <typename T>
using view_t = typename detail::view_of<T>::type;

namespace detail {

constexpr inline std::uint32_t magic = 'B' | ('L' << 8) | ('O' << 16) | ('B' << 24);

constexpr inline std::size_t header_size = 8;

template <typename A>
constexpr inline bool is_vec_v = false;

template <typename E>
constexpr inline bool is_vec_v<Vec<E>> = true;

template <typename A>
constexpr inline bool is_string_v = std::is_same_v<A, Vec<char>>;

template <typename A>
using element_t = typename A::value_type;

template <typename T>
constexpr inline bool is_scalar_v =
    std::is_arithmetic_v<T> ? !std::is_same_v<T, long double>
                            : std::is_enum_v<T> && requires { T{0}; };

/// A scalar's arithmetic type: an enum's underlying type, the scalar itself
/// otherwise.
template <typename T>
using arithmetic_t = typename std::
    conditional_t<std::is_enum_v<T>, std::underlying_type<T>, std::type_identity<T>>::type;

template <typename T>
constexpr inline std::size_t field_count_v = meta::field_count<T>();

template <typename T, std::size_t I>
using field_t = std::remove_cv_t<meta::field_type<T, I>>;

template <typename T, typename Visit>
constexpr void for_each_index(Visit&& visit) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (visit(std::integral_constant<std::size_t, I>{}), ...);
    }(std::make_index_sequence<field_count_v<T>>{});
}

enum class Kind : std::uint8_t {
    Scalar,
    Vec,
    Record,
};

/// The kind of a view type A.
template <typename A>
consteval Kind kind_of() {
    if constexpr(is_scalar_v<A>) {
        static_assert(alignof(A) <= alignment, "a sequence aligns its elements to 8 bytes at most");
        return Kind::Scalar;
    } else if constexpr(is_vec_v<A>) {
        return Kind::Vec;
    } else if constexpr(meta::annotated_type<A>) {
        static_assert(sizeof(A) == 0,
                      "a blob lays a record out as the struct itself: annotations do not apply");
    } else if constexpr(std::is_class_v<A> && std::is_aggregate_v<A>) {
        static_assert(std::is_empty_v<A> || field_count_v<A> > 0,
                      "reflection sees none of this struct's fields");
        static_assert(std::is_standard_layout_v<A>, "a record read in place is standard-layout");
        static_assert(alignof(A) <= alignment, "a blob aligns its records to 8 bytes at most");
        return Kind::Record;
    } else {
        static_assert(sizeof(A) == 0,
                      "a blob holds arithmetic types, enums with a fixed underlying type, Vec "
                      "and aggregates of those; a record holding strings or sequences is a "
                      "struct template over its sequence type");
    }
}

/// Whether A holds no Vec, so a writer copies it as it is.
template <typename A>
consteval bool is_plain() {
    if constexpr(kind_of<A>() == Kind::Record) {
        bool plain = true;
        for_each_index<A>([&](auto index) { plain = plain && is_plain<field_t<A, index()>>(); });
        return plain;
    } else {
        return kind_of<A>() == Kind::Scalar;
    }
}

/// Whether A is plain and its image has no padding, so a sequence of them
/// copies out whole.
template <typename A>
consteval bool is_packed() {
    if constexpr(!is_plain<A>()) {
        return false;
    } else if constexpr(kind_of<A>() == Kind::Scalar) {
        return true;
    } else {
        std::size_t fields = 0;
        bool packed = true;
        for_each_index<A>([&](auto index) {
            using F = field_t<A, index()>;
            fields += sizeof(F);
            packed = packed && is_packed<F>();
        });
        return packed && fields == sizeof(A);
    }
}

/// Whether reading A in place needs a check beyond its bounds: a bool, or an
/// enum over one, admits two bytes of 256, and a Vec points somewhere to
/// place.
template <typename A>
consteval bool needs_check() {
    if constexpr(kind_of<A>() == Kind::Scalar) {
        return std::is_same_v<arithmetic_t<A>, bool>;
    } else if constexpr(kind_of<A>() == Kind::Vec) {
        return true;
    } else {
        bool check = false;
        for_each_index<A>([&](auto index) { check = check || needs_check<field_t<A, index()>>(); });
        return check;
    }
}

consteval std::uint32_t mix(std::uint32_t hash, std::string_view text) {
    for(auto c: text) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }
    return hash;
}

consteval std::uint32_t mix_number(std::uint32_t hash, std::uint64_t number) {
    for(std::uint32_t shift = 0; shift < 64; shift += 8) {
        hash = (hash ^ static_cast<std::uint8_t>(number >> shift)) * 16777619u;
    }
    return hash;
}

/// A hash of A's layout: its kinds, sizes, field names and field offsets.
template <typename A>
consteval std::uint32_t fingerprint(std::uint32_t hash = 2166136261u) {
    if constexpr(kind_of<A>() == Kind::Scalar) {
        using U = arithmetic_t<A>;
        char code[] = {std::is_same_v<U, bool>       ? 'b'
                       : std::is_floating_point_v<U> ? 'f'
                       : std::is_signed_v<U>         ? 'i'
                                                     : 'u',
                       static_cast<char>('0' + sizeof(U))};
        return mix(hash, {code, sizeof(code)});
    } else if constexpr(is_string_v<A>) {
        return mix(hash, "s");
    } else if constexpr(kind_of<A>() == Kind::Vec) {
        return mix(fingerprint<element_t<A>>(mix(hash, "[")), "]");
    } else {
        hash = mix(hash, "{");
        for_each_index<A>([&](auto index) {
            hash = mix(hash, meta::field_name<index(), A>());
            hash = mix_number(hash, meta::field_offset<A>(index()));
            hash = fingerprint<field_t<A, index()>>(mix(hash, ":"));
            hash = mix(hash, ";");
        });
        return mix_number(mix(hash, "}"), sizeof(A));
    }
}

constexpr std::size_t align_up(std::size_t at, std::size_t align) {
    return (at + align - 1) / align * align;
}

}  // namespace detail

}  // namespace kota::codec::blob
