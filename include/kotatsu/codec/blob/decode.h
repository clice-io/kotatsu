#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kotatsu/support/expected_try.h"
#include "kotatsu/meta/struct.h"
#include "kotatsu/codec/blob/type.h"
#include "kotatsu/codec/context.h"

namespace kotatsu::codec::blob {

namespace detail {

/// Checks bytes against a view's layout, part by part in the order the
/// writer appends them. Positions are 64-bit, so that a length near 2^32
/// cannot wrap past the bounds where std::size_t is 32-bit.
struct Checker {
    std::span<const std::byte> bytes;

    /// Where the next out-of-line part begins, give or take its alignment.
    std::uint64_t cursor;

    rich_error error = {};

    /// Check the view A at `at`, inside bytes already placed, and place its
    /// out-of-line parts.
    template <typename A>
    bool check(std::uint64_t at) {
        if constexpr(!needs_check<A>()) {
            return true;
        } else if constexpr(kind_of<A>() == Kind::Scalar) {
            if(std::to_integer<std::uint8_t>(bytes[at]) > 1) {
                return fail("a bool is neither 0 nor 1");
            }
            return true;
        } else if constexpr(kind_of<A>() == Kind::Record) {
            bool ok = true;
            for_each_index<A>([&](auto index) {
                if(ok && !check<field_t<A, index()>>(at + meta::field_offset<A>(index()))) {
                    error.prepend_field(meta::field_name<index(), A>());
                    ok = false;
                }
            });
            return ok;
        } else {
            using E = element_t<A>;
            auto placed = place(at, sizeof(E), part_alignment_v<A>);
            if(!placed) {
                return false;
            }
            if constexpr(needs_check<E>()) {
                for(std::uint64_t i = 0; i < placed->length; ++i) {
                    if(!check<E>(placed->at + i * sizeof(E))) {
                        error.prepend_index(static_cast<std::size_t>(i));
                        return false;
                    }
                }
            }
            return true;
        }
    }

private:
    struct Placed {
        std::uint64_t at;
        std::uint64_t length;
    };

    bool fail(std::string_view message) {
        error = rich_error(std::string(message));
        return false;
    }

    /// The part the Vec at `at` points to, of elements of `element` bytes:
    /// where the writer puts it, inside the bytes.
    std::optional<Placed> place(std::uint64_t at, std::uint64_t element, std::uint64_t align) {
        std::uint32_t pair[2];
        std::memcpy(pair, bytes.data() + at, sizeof(pair));
        auto [offset, length] = pair;
        if(length == 0) {
            if(offset != 0) {
                fail("an empty part has an offset");
                return std::nullopt;
            }
            return Placed{.at = 0, .length = 0};
        }
        auto expected = align_up(cursor, align);
        if(at + offset != expected) {
            fail("a part is not where the layout puts it");
            return std::nullopt;
        }
        auto end = expected + length * element;
        if(end > bytes.size()) {
            fail("a part runs past the end of the bytes");
            return std::nullopt;
        }
        cursor = end;
        return Placed{.at = expected, .length = length};
    }
};

/// Copy the view `in` out into `out`, its writer form.
template <typename T>
void decode(const view_t<T>& in, T& out) {
    using A = view_t<T>;
    if constexpr(std::is_same_v<A, T>) {
        out = in;
    } else if constexpr(kind_of<A>() == Kind::Record) {
        for_each_index<A>([&](auto index) {
            detail::decode(meta::field_of<index()>(in), meta::field_of<index()>(out));
        });
    } else if constexpr(std::is_same_v<element_t<A>, std::ranges::range_value_t<T>>) {
        out.assign(in.begin(), in.end());
    } else {
        out.clear();
        out.reserve(in.size());
        for(const auto& element: in) {
            detail::decode(element, out.emplace_back());
        }
    }
}

}  // namespace detail

/// The root of bytes view accepted, read in place with no check: for bytes
/// verified once and read many times.
template <typename T>
auto view_verified(std::span<const std::byte> bytes) -> const view_t<T>& {
    assert(reinterpret_cast<std::uintptr_t>(bytes.data()) % alignment == 0);
    assert(bytes.size() >= sizeof(detail::Header) + sizeof(view_t<T>));
    return *reinterpret_cast<const view_t<T>*>(bytes.data() + sizeof(detail::Header));
}

template <typename T>
auto view_verified(std::span<const std::uint8_t> bytes) -> const view_t<T>& {
    return view_verified<T>(std::as_bytes(bytes));
}

/// The root of `bytes` read in place if they hold a blob of T in exactly the
/// layout type.h describes, the reason otherwise. Every read through the root
/// is in bounds, with no further check. The bytes must start at `alignment`,
/// and stay where they are while the root is read.
template <typename T>
auto view(std::span<const std::byte> bytes) -> std::expected<const view_t<T>*, rich_error> {
    using A = view_t<T>;
    static_assert(detail::kind_of<A>() == detail::Kind::Record, "a blob's root is a record");
    if(reinterpret_cast<std::uintptr_t>(bytes.data()) % alignment != 0) {
        return std::unexpected(rich_error("the bytes do not start at an 8-byte boundary"));
    }
    if(bytes.size() < sizeof(detail::Header) + sizeof(A) ||
       bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(rich_error("the bytes cannot hold a blob of this type"));
    }
    detail::Header header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if(header.magic != detail::magic) {
        return std::unexpected(rich_error("not a blob"));
    }
    if(header.fingerprint != detail::fingerprint_v<A>) {
        return std::unexpected(rich_error("a blob of another layout"));
    }
    detail::Checker checker{.bytes = bytes, .cursor = sizeof(detail::Header) + sizeof(A)};
    if(!checker.check<A>(sizeof(detail::Header))) {
        return std::unexpected(std::move(checker.error));
    }
    if(checker.cursor != bytes.size()) {
        return std::unexpected(rich_error("bytes past the blob's last part"));
    }
    return &view_verified<T>(bytes);
}

template <typename T>
auto view(std::span<const std::uint8_t> bytes) -> std::expected<const view_t<T>*, rich_error> {
    return view<T>(std::as_bytes(bytes));
}

/// Copies the value a blob of T holds out of `bytes`, which view checks.
template <typename T>
auto from_bytes(std::span<const std::byte> bytes, T& out) -> std::expected<void, rich_error> {
    KOTATSU_EXPECTED_TRY_V(auto root, view<T>(bytes));
    detail::decode(*root, out);
    return {};
}

template <typename T>
auto from_bytes(std::span<const std::uint8_t> bytes, T& out) -> std::expected<void, rich_error> {
    return from_bytes(std::as_bytes(bytes), out);
}

template <typename T>
    requires std::is_default_constructible_v<T>
auto from_bytes(std::span<const std::byte> bytes) -> std::expected<T, rich_error> {
    auto value = T();
    KOTATSU_EXPECTED_TRY(from_bytes(bytes, value));
    return value;
}

template <typename T>
    requires std::is_default_constructible_v<T>
auto from_bytes(std::span<const std::uint8_t> bytes) -> std::expected<T, rich_error> {
    return from_bytes<T>(std::as_bytes(bytes));
}

}  // namespace kotatsu::codec::blob
