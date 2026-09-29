#pragma once

#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <new>
#include <ranges>
#include <type_traits>
#include <utility>

#include "kota/support/config.h"

/// The memory primitives small_vector is built from: construction into uninitialized memory
/// that undoes itself when a constructor throws, and byte copies where they are the same thing.
namespace kota::mem {

/// Whether constructing a `To` from a `From` copies its bytes unchanged, so that a range of
/// them can be copied at once: the same trivially copyable type, or integers of one size, a
/// bool only from a bool. A pointer converted to point to a base class can change its
/// address, so no conversion between pointers counts.
template <typename To, typename From>
constexpr inline bool is_bitwise_constructible_v = [] {
    using from = std::remove_cvref_t<From>;
    using to = std::remove_cv_t<To>;
    if constexpr(!std::is_trivially_constructible_v<To, From> ||
                 !std::is_trivially_copyable_v<to>) {
        return false;
    } else if constexpr(std::is_same_v<from, to>) {
        return true;
    } else {
        return std::is_integral_v<from> && std::is_integral_v<to> && sizeof(from) == sizeof(to) &&
               std::is_same_v<from, bool> == std::is_same_v<to, bool>;
    }
}();

/// Default-initializes a T at `p`, which leaves a trivial type indeterminate. Constant
/// evaluation has no placement new, and std::construct_at, which it has, value-initializes.
template <typename T>
constexpr T* default_construct(T* p) {
    if consteval {
        return std::construct_at(p);
    } else {
        return ::new (static_cast<void*>(p)) T;
    }
}

/// Room for `count` T, or null for none.
template <typename T>
[[nodiscard]] constexpr T* allocate(std::size_t count) {
    return count == 0 ? nullptr : std::allocator<T>{}.allocate(count);
}

/// Frees what allocate<T>(count) returned.
template <typename T>
constexpr void deallocate(T* data, std::size_t count) noexcept {
    if(data != nullptr) {
        std::allocator<T>{}.deallocate(data, count);
    }
}

/// Whether `ptr` points into [first, last).
template <typename T>
[[nodiscard]] constexpr bool pointer_in_range(const T* ptr,
                                              const T* first,
                                              const T* last) noexcept {
    std::less<const T*> less;
    return !less(ptr, first) && less(ptr, last);
}

/// Owns an allocation, and the elements constructed at its start, until released: an
/// exception on the way to filling it destroys them and frees it.
template <typename T>
class AllocationGuard {
public:
    /// Takes `buffer`, allocated with allocate<T>(capacity), holding no elements yet.
    constexpr AllocationGuard(T* buffer, std::size_t capacity) noexcept :
        buffer(buffer), capacity(capacity), constructed_begin(buffer), constructed_end(buffer) {}

    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;

    constexpr ~AllocationGuard() {
        if(buffer != nullptr) {
            std::ranges::destroy(constructed_begin, constructed_end);
            deallocate(buffer, capacity);
        }
    }

    /// Records that the elements up to `end` are constructed.
    constexpr void mark(T* end) noexcept {
        mark(buffer, end);
    }

    /// Records that the elements of [first, last) are constructed, and no others.
    constexpr void mark(T* first, T* last) noexcept {
        constructed_begin = first;
        constructed_end = last;
    }

    [[nodiscard]] constexpr T* data() const noexcept {
        return buffer;
    }

    /// Hands the allocation and its elements over to the caller.
    [[nodiscard]] constexpr T* release() noexcept {
        return std::exchange(buffer, nullptr);
    }

private:
    T* buffer;
    std::size_t capacity;
    T* constructed_begin;
    T* constructed_end;
};

/// The elements a series of constructions has built from `first` on, destroyed when it goes
/// out of scope unless released: what is left to undo when one of the constructions throws.
template <typename T>
class ConstructedRange {
public:
    constexpr explicit ConstructedRange(T* first) noexcept : first(first), last(first) {}

    ConstructedRange(const ConstructedRange&) = delete;
    ConstructedRange& operator=(const ConstructedRange&) = delete;

    constexpr ~ConstructedRange() {
        std::ranges::destroy(first, last);
    }

    /// Where the next element goes.
    [[nodiscard]] constexpr T* end() const noexcept {
        return last;
    }

    /// Records that the element at end() is constructed.
    constexpr void extend() noexcept {
        ++last;
    }

    /// Keeps the elements, and returns their end.
    constexpr T* release() noexcept {
        first = last;
        return last;
    }

private:
    T* first;
    T* last;
};

template <std::ranges::forward_range Range>
[[nodiscard]] constexpr std::size_t range_length(Range&& range) {
    return static_cast<std::size_t>(std::ranges::distance(range));
}

/// [first, last) as rvalues.
template <typename Iterator>
[[nodiscard]] constexpr auto move_range(Iterator first, Iterator last) noexcept {
    return std::ranges::subrange(std::make_move_iterator(first), std::make_move_iterator(last));
}

namespace detail {

/// Copies the bytes of `count` T from `source` to `dest`, which do not overlap, and returns
/// the end of the copies.
template <typename T>
T* copy_bytes(const void* source, std::size_t count, T* dest) noexcept {
    if(count != 0) {
        std::memcpy(static_cast<void*>(dest), source, count * sizeof(T));
    }
    return dest + count;
}

/// Constructs a T from each element of `range` from `dest` on, as uninitialized_copy() does.
template <typename T, typename Range>
constexpr T* construct_from_each(Range&& range, T* dest) {
    ConstructedRange<T> built(dest);
    for(auto&& value: range) {
        std::construct_at(built.end(), std::forward<decltype(value)>(value));
        built.extend();
    }
    return built.release();
}

}  // namespace detail

// Each function below spells out the constant-evaluation branch rather than falling through
// to it: MSVC reports the code after an `if !consteval` that returns as unreachable.

/// Constructs a T from each element of `range` into uninitialized memory from `dest` on and
/// returns the end of what it constructed. If a constructor throws, the elements constructed
/// so far are destroyed.
template <typename T, std::ranges::input_range Range>
constexpr T* uninitialized_copy(Range&& range, T* dest) {
    if constexpr(std::ranges::contiguous_range<Range> && std::ranges::sized_range<Range> &&
                 is_bitwise_constructible_v<T, std::ranges::range_reference_t<Range>>) {
        if consteval {
            return detail::construct_from_each(range, dest);
        } else {
            return detail::copy_bytes(std::ranges::data(range),
                                      static_cast<std::size_t>(std::ranges::size(range)),
                                      dest);
        }
    } else {
        return detail::construct_from_each(std::forward<Range>(range), dest);
    }
}

/// Move-constructs [first, last) into uninitialized memory from `dest` on, copying the bytes
/// of a trivially copyable T, and returns the end of the copies. The sources stay alive,
/// moved from.
template <typename T>
constexpr T* uninitialized_move(T* first, T* last, T* dest) {
    if constexpr(std::is_trivially_copyable_v<T>) {
        if consteval {
            return detail::construct_from_each(move_range(first, last), dest);
        } else {
            return detail::copy_bytes(first, static_cast<std::size_t>(last - first), dest);
        }
    } else {
        return detail::construct_from_each(move_range(first, last), dest);
    }
}

/// Constructs each of [first, last) with `construct_one(p)`. If one throws, those constructed
/// so far are destroyed.
template <typename T, typename ConstructOne>
constexpr T* construct_each(T* first, T* last, ConstructOne construct_one) {
    ConstructedRange<T> built(first);
    for(; built.end() != last; built.extend()) {
        construct_one(built.end());
    }
    return built.release();
}

template <typename T>
constexpr T* uninitialized_value_construct(T* first, T* last) {
    return construct_each(first, last, [](T* p) { std::construct_at(p); });
}

template <typename T>
constexpr T* uninitialized_default_construct(T* first, T* last) {
    return construct_each(first, last, [](T* p) { default_construct(p); });
}

template <typename T>
constexpr T* uninitialized_fill(T* first, T* last, const T& value) {
    return construct_each(first, last, [&value](T* p) { std::construct_at(p, value); });
}

}  // namespace kota::mem
