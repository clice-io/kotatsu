#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

#ifndef EVENTIDE_ENABLE_EXCEPTIONS
#if defined(__cpp_exceptions)
#define EVENTIDE_ENABLE_EXCEPTIONS 1
#else
#define EVENTIDE_ENABLE_EXCEPTIONS 0
#endif
#endif

#if EVENTIDE_ENABLE_EXCEPTIONS
#define EVENTIDE_THROW(exception_expr) throw exception_expr
#define EVENTIDE_TRY try
#define EVENTIDE_CATCH_ALL() catch(...)
#define EVENTIDE_RETHROW() throw
#else
#define EVENTIDE_THROW(exception_expr)                                                             \
    do {                                                                                           \
        static_cast<void>(sizeof(exception_expr));                                                 \
        std::abort();                                                                              \
    } while(false)
#define EVENTIDE_TRY if(true)
#define EVENTIDE_CATCH_ALL() else
#define EVENTIDE_RETHROW() std::abort()
#endif

namespace eventide::mem {

template <typename T>
union uninitialized {
    T value;
    std::byte bytes[sizeof(T)];

    constexpr uninitialized() noexcept : bytes{} {}

    constexpr ~uninitialized() {}
};

template <typename T, unsigned int N>
struct uninitialized_array {
    uninitialized<T> buffer[N];
};

template <typename T>
struct uninitialized_array<T, 0> {};

template <typename T>
constexpr inline bool is_complete_type_v = requires { sizeof(T); };

template <typename U, typename = void>
struct underlying_if_enum {
    using type = U;
};

template <typename U>
struct underlying_if_enum<U, std::enable_if_t<std::is_enum_v<U>>> : std::underlying_type<U> {};

template <typename U>
using underlying_if_enum_t = typename underlying_if_enum<U>::type;

template <typename From, typename To>
constexpr inline bool is_memcpyable_integral_v = [] {
    if constexpr(!is_complete_type_v<From>) {
        return false;
    } else {
        using from = underlying_if_enum_t<From>;
        using to = underlying_if_enum_t<To>;

        return (sizeof(from) == sizeof(to)) &&
               (std::is_same_v<bool, from> == std::is_same_v<bool, to>) &&
               std::is_integral_v<from> && std::is_integral_v<to>;
    }
}();

template <typename From, typename To>
constexpr inline bool is_convertible_pointer_v =
    std::is_pointer_v<From> && std::is_pointer_v<To> && std::is_convertible_v<From, To>;

template <typename QualifiedFrom, typename QualifiedTo = QualifiedFrom>
constexpr inline bool is_memcpyable_v = [] {
    static_assert(!std::is_reference_v<QualifiedTo>, "QualifiedTo must not be a reference.");

    if constexpr(!is_complete_type_v<QualifiedFrom>) {
        return false;
    } else {
        using from = std::remove_cvref_t<QualifiedFrom>;
        using to = std::remove_cv_t<QualifiedTo>;

        return std::is_trivially_assignable_v<QualifiedTo&, QualifiedFrom> &&
               std::is_trivially_copyable_v<to> &&
               (std::is_same_v<from, to> || is_memcpyable_integral_v<from, to> ||
                is_convertible_pointer_v<from, to>);
    }
}();

template <typename To, typename... Args>
constexpr inline bool is_uninitialized_memcpyable_v = [] {
    static_assert(!std::is_reference_v<To>, "To must not be a reference.");

    if constexpr(sizeof...(Args) != 1) {
        return false;
    } else {
        return []<typename From>() {
            using from = std::remove_cvref_t<From>;
            using to = std::remove_cv_t<To>;

            if constexpr(is_complete_type_v<from>) {
                return std::is_trivially_constructible_v<To, From> &&
                       std::is_trivially_copyable_v<to> &&
                       (std::is_same_v<from, to> || is_memcpyable_integral_v<from, to> ||
                        is_convertible_pointer_v<from, to>);
            } else {
                return false;
            }
        }.template operator()<Args...>();
    }
}();

template <typename InputIt>
constexpr inline bool is_contiguous_iterator_v =
    std::is_pointer_v<InputIt> || std::contiguous_iterator<InputIt>;

template <typename ValueT, typename InputIt>
constexpr inline bool is_memcpyable_iterator_v =
    is_memcpyable_v<decltype(*std::declval<InputIt>()), ValueT> &&
    is_contiguous_iterator_v<InputIt>;

template <typename ValueT, typename InputIt>
constexpr inline bool is_memcpyable_iterator_v<ValueT, std::move_iterator<InputIt>> =
    is_memcpyable_iterator_v<ValueT, InputIt>;

template <typename ValueT, typename InputIt>
constexpr inline bool is_uninitialized_memcpyable_iterator_v =
    is_uninitialized_memcpyable_v<ValueT, decltype(*std::declval<InputIt>())> &&
    is_contiguous_iterator_v<InputIt>;

#ifndef NDEBUG
[[noreturn]]
inline void throw_range_length_error() {
    EVENTIDE_THROW(std::length_error("The specified range is too long."));
}
#endif

template <typename Integer>
[[nodiscard]]
consteval std::size_t numeric_max() noexcept {
    static_assert(0 <= (std::numeric_limits<Integer>::max)(), "Integer is nonpositive.");
    return static_cast<std::size_t>((std::numeric_limits<Integer>::max)());
}

template <typename ItDiffT>
constexpr void check_range_length_overflow([[maybe_unused]] ItDiffT len) {
#ifndef NDEBUG
    if(static_cast<std::make_unsigned_t<ItDiffT>>(len) > numeric_max<std::size_t>()) {
        throw_range_length_error();
    }
#endif
}

template <typename T>
constexpr inline bool is_trivially_relocatable_v = std::is_trivially_copyable_v<T>;

template <typename T, typename... Args>
constexpr auto construct_at_impl(T* p,
                                 Args&&... args) noexcept(noexcept(::new (std::declval<void*>())
                                                                       T(std::declval<Args>()...)))
    -> decltype(::new (std::declval<void*>()) T(std::declval<Args>()...)) {
    if(std::is_constant_evaluated()) {
        return std::construct_at(p, std::forward<Args>(args)...);
    }
    void* vp = const_cast<void*>(static_cast<const volatile void*>(p));
    return ::new (vp) T(std::forward<Args>(args)...);
}

template <typename T>
constexpr auto default_construct_at_impl(T* p) noexcept(noexcept(::new (std::declval<void*>()) T))
    -> decltype(::new (std::declval<void*>()) T) {
    if(std::is_constant_evaluated()) {
        return std::construct_at(p);
    }
    void* vp = const_cast<void*>(static_cast<const volatile void*>(p));
    return ::new (vp) T;
}

template <typename T, typename U>
constexpr T* construct(T* p, U&& val) noexcept(is_uninitialized_memcpyable_v<T, U&&> ||
                                               std::is_nothrow_constructible_v<T, U&&>) {
    if constexpr(is_uninitialized_memcpyable_v<T, U&&>) {
        if(std::is_constant_evaluated()) {
            return construct_at_impl(p, std::forward<U>(val));
        }
        std::memcpy(p, std::addressof(val), sizeof(T));
        return std::launder(p);
    } else {
        return construct_at_impl(p, std::forward<U>(val));
    }
}

template <typename T, typename... Args>
    requires (sizeof...(Args) != 1)
constexpr auto construct(T* p, Args&&... args) noexcept(noexcept(::new (std::declval<void*>())
                                                                     T(std::declval<Args>()...)))
    -> decltype(::new (std::declval<void*>()) T(std::declval<Args>()...)) {
    return construct_at_impl(p, std::forward<Args>(args)...);
}

template <typename T>
constexpr void destroy(T* p) noexcept {
    if constexpr(!std::is_trivially_destructible_v<T>) {
        std::destroy_at(p);
    }
}

template <typename T>
constexpr T* default_construct(T* p) noexcept(noexcept(::new (std::declval<void*>()) T)) {
    return default_construct_at_impl(p);
}

template <std::ranges::input_range Range>
constexpr void destroy_range(Range&& range) noexcept {
    using value_type = std::ranges::range_value_t<Range>;
    if constexpr(!std::is_trivially_destructible_v<value_type>) {
        std::ranges::destroy(range);
    }
}

template <typename T, typename Alloc = std::allocator<T>>
[[nodiscard]] constexpr T* allocate(std::size_t count) {
    if(count == 0) {
        return nullptr;
    }

    Alloc alloc;
    return std::allocator_traits<Alloc>::allocate(alloc, count);
}

template <typename T, typename Alloc = std::allocator<T>>
constexpr void deallocate(T* data, std::size_t count) noexcept {
    if(data == nullptr) {
        return;
    }

    Alloc alloc;
    std::allocator_traits<Alloc>::deallocate(alloc, data, count);
}

template <typename T>
[[nodiscard]]
constexpr bool pointer_in_range(const T* ptr, std::ranges::contiguous_range auto&& range) noexcept {
    const auto* first = std::ranges::data(range);
    const auto count = std::ranges::size(range);
    if(count == 0) {
        return false;
    }
    const auto* last = first + static_cast<std::ptrdiff_t>(count);
    std::less<const T*> less;
    return !less(ptr, first) && less(ptr, last);
}

template <typename T>
class stack_temporary {
public:
    using value_type = T;

    stack_temporary() = delete;
    stack_temporary(const stack_temporary&) = delete;
    stack_temporary(stack_temporary&&) noexcept = delete;
    stack_temporary& operator=(const stack_temporary&) = delete;
    stack_temporary& operator=(stack_temporary&&) noexcept = delete;

    template <typename... Args>
    constexpr explicit stack_temporary(Args&&... args) {
        construct(storage_pointer(), std::forward<Args>(args)...);
    }

    constexpr ~stack_temporary() {
        destroy(get_pointer());
    }

    [[nodiscard]] constexpr const value_type& get() const noexcept {
        return *get_pointer();
    }

    [[nodiscard]] constexpr value_type&& release() noexcept {
        return std::move(*get_pointer());
    }

private:
    [[nodiscard]] constexpr const value_type* storage_pointer() const noexcept {
        return std::addressof(m_storage.value);
    }

    [[nodiscard]] constexpr value_type* storage_pointer() noexcept {
        return std::addressof(m_storage.value);
    }

    [[nodiscard]] constexpr const value_type* get_pointer() const noexcept {
        return std::launder(storage_pointer());
    }

    [[nodiscard]] constexpr value_type* get_pointer() noexcept {
        return std::launder(storage_pointer());
    }

    uninitialized<value_type> m_storage;
};

template <typename T>
class heap_temporary {
public:
    using value_type = T;

    heap_temporary() = delete;
    heap_temporary(const heap_temporary&) = delete;
    heap_temporary(heap_temporary&&) noexcept = delete;
    heap_temporary& operator=(const heap_temporary&) = delete;
    heap_temporary& operator=(heap_temporary&&) noexcept = delete;

    template <typename... Args>
    constexpr explicit heap_temporary(Args&&... args) : m_data(allocate_storage()) {
        EVENTIDE_TRY {
            construct(m_data, std::forward<Args>(args)...);
        }
        EVENTIDE_CATCH_ALL() {
            deallocate(m_data, 1);
            EVENTIDE_RETHROW();
        }
    }

    constexpr ~heap_temporary() {
        destroy(m_data);
        deallocate(m_data, 1);
    }

    [[nodiscard]] constexpr const value_type& get() const noexcept {
        return *m_data;
    }

    [[nodiscard]] constexpr value_type&& release() noexcept {
        return std::move(*m_data);
    }

private:
    [[nodiscard]] constexpr static auto allocate_storage() {
        return allocate<value_type>(1);
    }

    value_type* m_data;
};

template <typename T, typename Alloc = std::allocator<T>>
class allocation_guard {
public:
    using value_type = T;
    using allocator_type = Alloc;
    using pointer = value_type*;
    using size_type = std::size_t;

    constexpr explicit allocation_guard(size_type capacity) :
        m_data(allocate<value_type, allocator_type>(capacity)), m_capacity(capacity),
        m_constructed(m_data) {}

    allocation_guard(const allocation_guard&) = delete;
    allocation_guard& operator=(const allocation_guard&) = delete;
    allocation_guard(allocation_guard&&) noexcept = delete;
    allocation_guard& operator=(allocation_guard&&) noexcept = delete;

    constexpr ~allocation_guard() {
        if(m_data == nullptr) {
            return;
        }

        destroy_range(std::ranges::subrange(m_data, m_constructed));
        deallocate<value_type, allocator_type>(m_data, m_capacity);
    }

    constexpr void mark(pointer p) noexcept {
        m_constructed = p;
    }

    [[nodiscard]] constexpr pointer data() const noexcept {
        return m_data;
    }

    [[nodiscard]] constexpr pointer release() noexcept {
        pointer result = m_data;
        m_data = nullptr;
        return result;
    }

private:
    pointer m_data;
    size_type m_capacity;
    pointer m_constructed;
};

template <std::ranges::forward_range Range>
[[nodiscard]]
constexpr std::size_t range_length(Range&& range) {
    using ItDiffT = std::ranges::range_difference_t<Range>;
    auto first = std::ranges::begin(range);
    auto last = std::ranges::end(range);
    if(first == last) {
        return 0;
    }

    if constexpr(numeric_max<std::size_t>() < numeric_max<ItDiffT>()) {
        if constexpr(std::ranges::random_access_range<Range>) {
            const auto len = last - first;
            assert(0 <= len && "Invalid range.");
            check_range_length_overflow(len);
            return static_cast<std::size_t>(len);
        } else {
            if(std::is_constant_evaluated()) {
                ItDiffT len = 0;
                for(; !(first == last); ++first) {
                    ++len;
                }
                check_range_length_overflow(len);
                return static_cast<std::size_t>(len);
            }
            const auto len = std::ranges::distance(range);
            check_range_length_overflow(len);
            return static_cast<std::size_t>(len);
        }
    } else {
        if(std::is_constant_evaluated()) {
            std::size_t len = 0;
            for(; !(first == last); ++first) {
                ++len;
            }
            return len;
        }
        return static_cast<std::size_t>(std::ranges::distance(range));
    }
}

[[nodiscard]]
constexpr auto iterator_address(std::contiguous_iterator auto it) noexcept {
    return std::to_address(it);
}

template <typename Iterator>
[[nodiscard]]
constexpr auto move_range(Iterator first, Iterator last) noexcept {
    return std::ranges::subrange(std::make_move_iterator(first), std::make_move_iterator(last));
}

template <typename T, std::ranges::input_range Range>
constexpr T* default_uninitialized_copy(Range&& range, T* d_first) {
    T* d_last = d_first;
    EVENTIDE_TRY {
        for(auto&& value: range) {
            construct(d_last, std::forward<decltype(value)>(value));
            ++d_last;
        }
        return d_last;
    }
    EVENTIDE_CATCH_ALL() {
        destroy_range(std::ranges::subrange(d_first, d_last));
        EVENTIDE_RETHROW();
    }
}

template <typename T, std::ranges::input_range Range>
constexpr T* uninitialized_copy(Range&& range, T* dest) {
    using iterator = std::ranges::iterator_t<Range>;

    if constexpr(std::ranges::forward_range<Range> &&
                 is_uninitialized_memcpyable_iterator_v<T, iterator>) {
        static_assert(std::is_constructible_v<T, std::ranges::range_reference_t<Range>>,
                      "`value_type` must be copy constructible.");
        if(std::is_constant_evaluated()) {
            return default_uninitialized_copy<T>(std::forward<Range>(range), dest);
        }

        const std::size_t num_copy = range_length(range);
        if(num_copy != 0) {
            std::memcpy(dest, iterator_address(std::ranges::begin(range)), num_copy * sizeof(T));
        }
        return dest + static_cast<std::ptrdiff_t>(num_copy);
    } else {
        return default_uninitialized_copy<T>(std::forward<Range>(range), dest);
    }
}

template <typename T, std::ranges::forward_range Range>
constexpr T* uninitialized_relocate(Range&& range, T* dest) {
    if constexpr(is_trivially_relocatable_v<T> && std::ranges::contiguous_range<Range> &&
                 std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, T>) {
        const std::size_t count = range_length(range);
        if(count != 0 && !std::is_constant_evaluated()) {
            std::memcpy(dest, std::ranges::data(range), count * sizeof(T));
        } else if(count != 0) {
            uninitialized_copy<T>(std::forward<Range>(range), dest);
        }
        return dest + static_cast<std::ptrdiff_t>(count);
    } else {
        return uninitialized_copy<T>(move_range(std::ranges::begin(range), std::ranges::end(range)),
                                     dest);
    }
}

constexpr auto uninitialized_value_construct(std::ranges::contiguous_range auto&& range) {
    auto* first = std::ranges::data(range);
    auto* last = std::ranges::data(range) + static_cast<std::ptrdiff_t>(std::ranges::size(range));
    using T = std::remove_cv_t<std::ranges::range_value_t<decltype(range)>>;
    if constexpr(std::is_trivially_constructible_v<T>) {
        if(!std::is_constant_evaluated()) {
            std::fill(first, last, T());
            return last;
        }
    }
    T* curr = first;
    EVENTIDE_TRY {
        for(; !(curr == last); ++curr) {
            construct(curr);
        }
        return curr;
    }
    EVENTIDE_CATCH_ALL() {
        destroy_range(std::ranges::subrange(first, curr));
        EVENTIDE_RETHROW();
    }
}

constexpr auto uninitialized_default_construct(std::ranges::contiguous_range auto&& range) {
    auto* first = std::ranges::data(range);
    auto* last = std::ranges::data(range) + static_cast<std::ptrdiff_t>(std::ranges::size(range));
    using T = std::remove_cv_t<std::ranges::range_value_t<decltype(range)>>;
    if constexpr(std::is_trivially_default_constructible_v<T>) {
        if(!std::is_constant_evaluated()) {
            return last;
        }
    }

    T* curr = first;
    EVENTIDE_TRY {
        for(; !(curr == last); ++curr) {
            default_construct(curr);
        }
        return curr;
    }
    EVENTIDE_CATCH_ALL() {
        destroy_range(std::ranges::subrange(first, curr));
        EVENTIDE_RETHROW();
    }
}

template <std::ranges::contiguous_range Range, typename T>
constexpr auto uninitialized_fill(Range&& range, const T& val) {
    auto* first = std::ranges::data(range);
    auto* last = std::ranges::data(range) + static_cast<std::ptrdiff_t>(std::ranges::size(range));
    T* curr = first;
    EVENTIDE_TRY {
        for(; !(curr == last); ++curr) {
            construct(curr, val);
        }
        return curr;
    }
    EVENTIDE_CATCH_ALL() {
        destroy_range(std::ranges::subrange(first, curr));
        EVENTIDE_RETHROW();
    }
}

}  // namespace eventide::mem
