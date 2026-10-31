#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include "kotatsu/meta/struct.h"
#include "kotatsu/codec/blob/type.h"
#include "kotatsu/codec/visit/context.h"

namespace kotatsu::codec::blob {

namespace detail {

/// Lays a value out at the end of `bytes`; append grows them zeroed, so
/// padding stays zero.
struct Writer {
    std::vector<std::byte> bytes;

    /// Whether a part would have ended past 4 GiB, where a Vec cannot point;
    /// such a part is left out, and to_bytes fails.
    bool oversized = false;

    /// Where `size` zeroed bytes appended at the next multiple of `align`
    /// start; none past 4 GiB.
    std::optional<std::size_t> append(std::uint64_t size, std::uint64_t align) {
        auto at = align_up(bytes.size(), align);
        if(at + size > std::numeric_limits<std::uint32_t>::max()) {
            oversized = true;
            return std::nullopt;
        }
        bytes.resize(static_cast<std::size_t>(at + size));
        return static_cast<std::size_t>(at);
    }

    void write(std::size_t at, const void* data, std::size_t size) {
        std::memcpy(bytes.data() + at, data, size);
    }

    /// Write `value` as the view A at `at`, inside bytes already appended, and
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
        } else {
            using E = element_t<A>;
            using Element = std::ranges::range_value_t<const T>;
            auto count = std::ranges::size(value);
            if(count == 0) {
                return;
            }
            auto data = append(std::uint64_t(count) * sizeof(E), part_alignment_v<A>);
            if(!data) {
                return;
            }
            std::uint32_t pair[] = {static_cast<std::uint32_t>(*data - at),
                                    static_cast<std::uint32_t>(count)};
            write(at, pair, sizeof(pair));
            // A plain struct template's writer form is not its view, and
            // std::vector<bool> holds no bools, so those go element by
            // element; each of libc++'s bit references converts to a bool.
            if constexpr(is_packed<E>() && std::is_same_v<Element, E> &&
                         std::ranges::contiguous_range<const T>) {
                write(*data, std::ranges::data(value), count * sizeof(E));
            } else {
                auto next = *data;
                for(const Element& element: value) {
                    store<E>(next, element);
                    next += sizeof(E);
                }
            }
        }
    }
};

}  // namespace detail

/// Lays `value`, a record, out as a blob. std::vector allocates its bytes
/// through operator new, which every supported platform aligns to 16, so they
/// read in place as they are. A blob holds less than 4 GiB: a value that
/// needs more fails.
template <typename T>
auto to_bytes(const T& value) -> std::expected<std::vector<std::byte>, rich_error> {
    using A = view_t<T>;
    static_assert(detail::kind_of<A>() == detail::Kind::Record, "a blob's root is a record");
    detail::Writer writer;
    writer.append(sizeof(detail::Header) + sizeof(A), alignment);
    const detail::Header header{.magic = detail::magic, .fingerprint = detail::fingerprint_v<A>};
    writer.write(0, &header, sizeof(header));
    writer.store<A>(sizeof(detail::Header), value);
    if(writer.oversized) {
        return std::unexpected(rich_error("a blob holds less than 4 GiB"));
    }
    return std::move(writer.bytes);
}

}  // namespace kotatsu::codec::blob
