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
        buffer(buffer), capacity(capacity), constructed_end(buffer) {}

    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;

    constexpr ~AllocationGuard() {
        if(buffer != nullptr) {
            std::ranges::destroy(buffer, constructed_end);
            deallocate(buffer, capacity);
        }
    }

    /// Records that the elements up to `end` are constructed.
    constexpr void mark(T* end) noexcept {
        constructed_end = end;
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
    T* constructed_end;
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

/// Constructs a T from each element of `range` into uninitialized memory from `dest` on and
/// returns the end of what it constructed. If a constructor throws, the elements constructed
/// so far are destroyed.
template <typename T, std::ranges::input_range Range>
constexpr T* uninitialized_copy(Range&& range, T* dest) {
    if constexpr(std::ranges::contiguous_range<Range> && std::ranges::sized_range<Range> &&
                 is_bitwise_constructible_v<T, std::ranges::range_reference_t<Range>>) {
        if !consteval {
            const auto count = static_cast<std::size_t>(std::ranges::size(range));
            if(count != 0) {
                std::memcpy(static_cast<void*>(dest), std::ranges::data(range), count * sizeof(T));
            }
            return dest + count;
        }
    }
    T* out = dest;
    KOTA_TRY {
        for(auto&& value: range) {
            std::construct_at(out, std::forward<decltype(value)>(value));
            ++out;
        }
        return out;
    }
    KOTA_CATCH_ALL() {
        std::ranges::destroy(dest, out);
        KOTA_RETHROW();
    }
}

/// Move-constructs [first, last) into uninitialized memory from `dest` on, copying the bytes
/// of a trivially copyable T, and returns the end of the copies. The sources stay alive,
/// moved from.
template <typename T>
constexpr T* uninitialized_move(T* first, T* last, T* dest) {
    if constexpr(std::is_trivially_copyable_v<T>) {
        if !consteval {
            const auto count = static_cast<std::size_t>(last - first);
            if(count != 0) {
                std::memcpy(static_cast<void*>(dest), first, count * sizeof(T));
            }
            return dest + count;
        }
    }
    return uninitialized_copy(move_range(first, last), dest);
}

/// Constructs [first, last) anew from `dest` on, for elements moving to a new buffer: by
/// moving, unless the move can throw and T can be copied, which leaves the sources as they
/// were when a construction throws (std::move_if_noexcept).
template <typename T>
constexpr T* uninitialized_relocate(T* first, T* last, T* dest) {
    if constexpr(!std::is_nothrow_move_constructible_v<T> && std::is_copy_constructible_v<T>) {
        return uninitialized_copy(std::ranges::subrange(first, last), dest);
    } else {
        return uninitialized_move(first, last, dest);
    }
}

/// Constructs each of [first, last) with `construct_one(p)`. If one throws, those constructed
/// so far are destroyed.
template <typename T, typename ConstructOne>
constexpr T* construct_each(T* first, T* last, ConstructOne construct_one) {
    T* current = first;
    KOTA_TRY {
        for(; current != last; ++current) {
            construct_one(current);
        }
        return current;
    }
    KOTA_CATCH_ALL() {
        std::ranges::destroy(first, current);
        KOTA_RETHROW();
    }
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
