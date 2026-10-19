#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/meta/struct.h"
#include "kota/codec/blob/type.h"
#include "kota/codec/visit/context.h"

namespace kota::codec::blob {

namespace detail {

/// Lays a value out at the end of `bytes`, which grow zeroed, so that padding
/// is zero.
struct Writer {
    std::vector<std::byte> bytes;

    /// Room for `size` zeroed bytes at the next multiple of `align`.
    std::size_t reserve(std::size_t size, std::size_t align) {
        auto at = align_up(bytes.size(), align);
        bytes.resize(at + size);
        return at;
    }

    void write(std::size_t at, const void* data, std::size_t size) {
        std::memcpy(bytes.data() + at, data, size);
    }

    /// Write `value` as the view A at `at`, inside room already reserved, and
    /// append its out-of-line parts.
    template <typename A, typename T>
    void store(std::size_t at, const T& value) {
        if constexpr(kind_of<A>() == Kind::Scalar) {
            static_assert(std::is_same_v<A, T>, "a scalar is laid out as itself");
            write(at, &value, sizeof(A));
        } else if constexpr(kind_of<A>() == Kind::Record) {
            static_assert(field_count_v<A> == field_count_v<T>,
                          "a record is laid out field by field");
            for_each_index<A>([&](auto index) {
                store<field_t<A, index()>>(at + meta::field_offset<A>(index()),
                                           meta::field_of<index()>(value));
            });
        } else if constexpr(is_string_v<A>) {
            std::string_view text(value);
            if(text.empty()) {
                return;
            }
            auto data = reserve(text.size(), 1);
            write(data, text.data(), text.size());
            write_vec(at, data, text.size());
        } else {
            using E = element_t<A>;
            auto count = std::ranges::size(value);
            if(count == 0) {
                return;
            }
            auto data = reserve(count * sizeof(E), alignment);
            write_vec(at, data, count);
            // std::vector<bool> is no array of bools, so it goes element by
            // element.
            if constexpr(is_packed<E>() && std::ranges::contiguous_range<const T>) {
                static_assert(std::is_same_v<std::ranges::range_value_t<const T>, E>,
                              "a plain element is laid out as itself");
                write(data, std::ranges::data(value), count * sizeof(E));
            } else {
                for(const auto& element: value) {
                    store<E>(data, element);
                    data += sizeof(E);
                }
            }
        }
    }

private:
    /// The Vec at `at`, of `length` elements at `data`: wrong in bytes of
    /// 4 GiB or more, which to_bytes rejects.
    void write_vec(std::size_t at, std::size_t data, std::size_t length) {
        std::uint32_t pair[] = {static_cast<std::uint32_t>(data - at),
                                static_cast<std::uint32_t>(length)};
        write(at, pair, sizeof(pair));
    }
};

}  // namespace detail

/// Lays `value`, a record, out as a blob. std::vector allocates its bytes at
/// `alignment`, so they read in place as they are. A blob holds less than
/// 4 GiB: a value that needs more fails.
template <typename T>
auto to_bytes(const T& value) -> std::expected<std::vector<std::byte>, rich_error> {
    using A = view_t<T>;
    static_assert(detail::kind_of<A>() == detail::Kind::Record, "a blob's root is a record");
    detail::Writer writer;
    writer.reserve(detail::header_size, alignment);
    constexpr std::uint32_t header[] = {detail::magic, detail::fingerprint<A>()};
    writer.write(0, header, sizeof(header));
    writer.store<A>(writer.reserve(sizeof(A), alignof(A)), value);
    if(writer.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(rich_error("a blob holds less than 4 GiB"));
    }
    return std::move(writer.bytes);
}

}  // namespace kota::codec::blob
