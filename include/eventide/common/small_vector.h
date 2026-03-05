#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace eventide {

namespace concepts {

template <typename T>
concept Complete = requires { sizeof(T); };

// Note: this mirrors the named requirements, not the standard concepts, so we don't require
// the destructor to be noexcept for Destructible.
template <typename T>
concept Destructible = std::is_destructible<T>::value;

template <typename T>
concept TriviallyDestructible = std::is_trivially_destructible<T>::value;

template <typename T>
concept NoThrowDestructible = std::is_nothrow_destructible<T>::value;

// Note: this mirrors the named requirements, not the standard library concepts,
// so we don't require Destructible here.

template <typename T, typename... Args>
concept ConstructibleFrom = std::is_constructible<T, Args...>::value;

template <typename T, typename... Args>
concept NoThrowConstructibleFrom = std::is_nothrow_constructible<T, Args...>::value;

template <typename From, typename To>
concept ConvertibleTo =
    std::is_convertible<From, To>::value &&
    requires(typename std::add_rvalue_reference<From>::type (&f)(void)) { static_cast<To>(f()); };

template <typename From, typename To>
concept NoThrowConvertibleTo =
    std::is_nothrow_convertible<From, To>::value &&
    requires(typename std::add_rvalue_reference<From>::type (&f)(void) noexcept) {
        { static_cast<To>(f()) } noexcept;
    };

// Note: std::default_initializable requires std::destructible.
template <typename T>
concept DefaultConstructible = ConstructibleFrom<T> && requires { T{}; } &&
                               requires { ::new (static_cast<void*>(nullptr)) T; };

template <typename T>
concept MoveAssignable = std::assignable_from<T&, T>;

template <typename T>
concept CopyAssignable = MoveAssignable<T> && std::assignable_from<T&, T&> &&
                         std::assignable_from<T&, const T&> && std::assignable_from<T&, const T>;

template <typename T>
concept MoveConstructible = ConstructibleFrom<T, T> && ConvertibleTo<T, T>;

template <typename T>
concept NoThrowMoveConstructible = NoThrowConstructibleFrom<T, T> && NoThrowConvertibleTo<T, T>;

template <typename T>
concept CopyConstructible =
    MoveConstructible<T> && ConstructibleFrom<T, T&> && ConvertibleTo<T&, T> &&
    ConstructibleFrom<T, const T&> && ConvertibleTo<const T&, T> && ConstructibleFrom<T, const T> &&
    ConvertibleTo<const T, T>;

template <typename T>
concept NoThrowCopyConstructible =
    NoThrowMoveConstructible<T> && NoThrowConstructibleFrom<T, T&> && NoThrowConvertibleTo<T&, T> &&
    NoThrowConstructibleFrom<T, const T&> && NoThrowConvertibleTo<const T&, T> &&
    NoThrowConstructibleFrom<T, const T> && NoThrowConvertibleTo<const T, T>;

template <typename T>
concept Swappable = std::swappable<T>;

template <typename T>
concept EqualityComparable = std::equality_comparable<T>;

namespace small_vector {

// Basically, these shut off the concepts if we have an incomplete type.
// This namespace is only needed because of issues on Clang
// preventing us from short-circuiting for incomplete types.

template <typename T>
concept Destructible = !concepts::Complete<T> || concepts::Destructible<T>;

template <typename T>
concept MoveAssignable = !concepts::Complete<T> || concepts::MoveAssignable<T>;

template <typename T>
concept CopyAssignable = !concepts::Complete<T> || concepts::CopyAssignable<T>;

template <typename T>
concept MoveConstructible = !concepts::Complete<T> || concepts::MoveConstructible<T>;

template <typename T>
concept CopyConstructible = !concepts::Complete<T> || concepts::CopyConstructible<T>;

template <typename T>
concept Swappable = !concepts::Complete<T> || concepts::Swappable<T>;

// Simplified (no allocator):
template <typename T>
concept DefaultInsertable = !concepts::Complete<T> || std::is_default_constructible_v<T>;

template <typename T>
concept MoveInsertable = !concepts::Complete<T> || std::is_move_constructible_v<T>;

template <typename T>
concept CopyInsertable =
    !concepts::Complete<T> ||
    (MoveInsertable<T> && std::is_constructible_v<T, T&> && std::is_constructible_v<T, const T&>);

template <typename T>
concept Erasable = !concepts::Complete<T> || std::is_destructible_v<T>;

template <typename T, typename... Args>
concept EmplaceConstructible = !concepts::Complete<T> || std::is_constructible_v<T, Args...>;

}  // namespace small_vector

}  // namespace concepts

template <typename T>
struct default_buffer_size;

template <typename T, unsigned InlineCapacity = default_buffer_size<T>::value>
class small_vector;

template <typename T>
struct default_buffer_size {
private:
    template <typename, typename Enable = void>
    struct is_complete : std::false_type {};

    template <typename U>
    struct is_complete<U, decltype(static_cast<void>(sizeof(U)))> : std::true_type {};

public:
    using value_type = T;
    using empty_small_vector = small_vector<value_type, 0>;

    static_assert(is_complete<value_type>::value,
                  "Calculation of a default number of elements requires that `T` be complete.");

    constexpr static unsigned buffer_max = 256;

    constexpr static unsigned ideal_total = 64;

    // FIXME: Some compilers will not emit the error from this static_assert
    //        while instantiating a small_vector, and attribute the mistake
    //        to some random other function.
    // static_assert (sizeof (value_type) <= buffer_max, "`sizeof (T)` too large");

    constexpr static unsigned ideal_buffer = ideal_total - sizeof(empty_small_vector);

    static_assert(sizeof(empty_small_vector) != 0, "Empty `small_vector` should not have size 0.");

    static_assert(ideal_buffer < ideal_total, "Empty `small_vector` is larger than ideal_total.");

    constexpr static unsigned value =
        (sizeof(value_type) <= ideal_buffer) ? (ideal_buffer / sizeof(value_type)) : 1;
};

template <typename T>
constexpr inline unsigned default_buffer_size_v = default_buffer_size<T>::value;

namespace detail {

template <typename T, unsigned InlineCapacity>
class inline_storage {
public:
    using value_ty = T;

    inline_storage(void) = default;
    inline_storage(const inline_storage&) = delete;
    inline_storage(inline_storage&&) noexcept = delete;
    inline_storage& operator=(const inline_storage&) = delete;
    inline_storage& operator=(inline_storage&&) noexcept = delete;
    ~inline_storage(void) = default;

    [[nodiscard]] constexpr value_ty* get_inline_ptr(void) noexcept {
        return static_cast<value_ty*>(static_cast<void*>(std::addressof(*m_data)));
    }

private:
    union alignas(alignof(value_ty)) {
        unsigned char _[sizeof(value_ty)];
    } m_data[InlineCapacity];
};

// =====================================================================
// Free type traits (extracted from allocation_helper)
// =====================================================================

template <typename T, typename = void>
struct is_complete_type : std::false_type {};

template <typename T>
struct is_complete_type<T, decltype(static_cast<void>(sizeof(T)))> : std::true_type {};

template <typename U, typename = void>
struct underlying_if_enum {
    using type = U;
};

template <typename U>
struct underlying_if_enum<U, std::enable_if_t<std::is_enum_v<U>>> : std::underlying_type<U> {};

template <typename U>
using underlying_if_enum_t = typename underlying_if_enum<U>::type;

template <typename From, typename To, typename = void>
struct is_memcpyable_integral : std::false_type {};

template <typename From, typename To>
struct is_memcpyable_integral<From, To, std::enable_if_t<is_complete_type<From>::value>> {
    using from = underlying_if_enum_t<From>;
    using to = underlying_if_enum_t<To>;

    constexpr static bool value = (sizeof(from) == sizeof(to)) &&
                                  (std::is_same_v<bool, from> == std::is_same_v<bool, to>) &&
                                  std::is_integral_v<from> && std::is_integral_v<to>;
};

template <typename From, typename To>
struct is_convertible_pointer :
    std::bool_constant<std::is_pointer_v<From> && std::is_pointer_v<To> &&
                       std::is_convertible_v<From, To>> {};

// Memcpyable assignment: can we assign QualifiedTo from QualifiedFrom via memcpy?
template <typename QualifiedFrom, typename QualifiedTo, typename = void>
struct is_memcpyable : std::false_type {};

template <typename QualifiedFrom, typename QualifiedTo>
struct is_memcpyable<QualifiedFrom,
                     QualifiedTo,
                     std::enable_if_t<is_complete_type<QualifiedFrom>::value>> {
    static_assert(!std::is_reference_v<QualifiedTo>, "QualifiedTo must not be a reference.");

    using from = std::remove_cv_t<std::remove_reference_t<std::remove_cv_t<QualifiedFrom>>>;
    using to = std::remove_cv_t<QualifiedTo>;

    constexpr static bool value =
        std::is_trivially_assignable_v<QualifiedTo&, QualifiedFrom> &&
        std::is_trivially_copyable_v<to> &&
        (std::is_same_v<std::remove_cv_t<from>, to> || is_memcpyable_integral<from, to>::value ||
         is_convertible_pointer<from, to>::value);
};

// Memcpyable construction: can we construct QualifiedTo from QualifiedFrom via memcpy?
template <typename QualifiedTo, typename QualifiedFrom, typename = void>
struct is_uninitialized_memcpyable_impl : std::false_type {};

template <typename QualifiedTo, typename QualifiedFrom>
struct is_uninitialized_memcpyable_impl<
    QualifiedTo,
    QualifiedFrom,
    std::enable_if_t<
        is_complete_type<std::remove_cv_t<std::remove_reference_t<QualifiedFrom>>>::value>> {
    static_assert(!std::is_reference_v<QualifiedTo>, "QualifiedTo must not be a reference.");

    using from = std::remove_cv_t<std::remove_reference_t<std::remove_cv_t<QualifiedFrom>>>;
    using to = std::remove_cv_t<QualifiedTo>;

    constexpr static bool value =
        std::is_trivially_constructible_v<QualifiedTo, QualifiedFrom> &&
        std::is_trivially_copyable_v<to> &&
        (std::is_same_v<std::remove_cv_t<from>, to> || is_memcpyable_integral<from, to>::value ||
         is_convertible_pointer<from, to>::value);
};

template <typename To, typename... Args>
struct is_uninitialized_memcpyable : std::false_type {};

template <typename To, typename From>
struct is_uninitialized_memcpyable<To, From> : is_uninitialized_memcpyable_impl<To, From> {};

template <typename InputIt>
struct is_contiguous_iterator :
    std::bool_constant<std::is_pointer_v<InputIt> || std::contiguous_iterator<InputIt>> {};

template <typename ValueT, typename InputIt>
struct is_memcpyable_iterator :
    std::bool_constant<is_memcpyable<decltype(*std::declval<InputIt>()), ValueT>::value &&
                       is_contiguous_iterator<InputIt>::value> {};

// Unwrap `move_iterator`s.
template <typename ValueT, typename InputIt>
struct is_memcpyable_iterator<ValueT, std::move_iterator<InputIt>> :
    is_memcpyable_iterator<ValueT, InputIt> {};

template <typename ValueT, typename InputIt>
struct is_uninitialized_memcpyable_iterator :
    std::bool_constant<
        is_uninitialized_memcpyable<ValueT, decltype(*std::declval<InputIt>())>::value &&
        is_contiguous_iterator<InputIt>::value> {};

// =====================================================================
// Free utility functions (extracted from allocation_helper)
// =====================================================================

#ifndef NDEBUG
[[noreturn]]
inline void throw_range_length_error(void) {
    throw std::length_error("The specified range is too long.");
}
#endif

template <typename Integer>
[[nodiscard]]
consteval std::size_t numeric_max(void) noexcept {
    static_assert(0 <= (std::numeric_limits<Integer>::max)(), "Integer is nonpositive.");
    return static_cast<std::size_t>((std::numeric_limits<Integer>::max)());
}

template <typename T, typename... Args>
constexpr auto construct_at_impl(T* p,
                                 Args&&... args) noexcept(noexcept(::new (std::declval<void*>())
                                                                       T(std::declval<Args>()...)))
    -> decltype(::new (std::declval<void*>()) T(std::declval<Args>()...)) {
    if(std::is_constant_evaluated())
        return std::construct_at(p, std::forward<Args>(args)...);
    void* vp = const_cast<void*>(static_cast<const volatile void*>(p));
    return ::new (vp) T(std::forward<Args>(args)...);
}

// Single-argument construct with memcpy optimization.
template <typename T, typename U>
constexpr void construct(T* p, U&& val) noexcept(is_uninitialized_memcpyable<T, U&&>::value ||
                                                 std::is_nothrow_constructible_v<T, U&&>) {
    if constexpr(is_uninitialized_memcpyable<T, U&&>::value) {
        if(std::is_constant_evaluated()) {
            construct_at_impl(p, std::forward<U>(val));
            return;
        }
        std::memcpy(p, std::addressof(val), sizeof(T));
    } else {
        construct_at_impl(p, std::forward<U>(val));
    }
}

// Zero or 2+ argument construct.
template <typename T, typename... Args>
    requires (sizeof...(Args) != 1)
constexpr auto construct(T* p, Args&&... args) noexcept(noexcept(::new (std::declval<void*>())
                                                                     T(std::declval<Args>()...)))
    -> decltype(::new (std::declval<void*>()) T(std::declval<Args>()...), void()) {
    construct_at_impl(p, std::forward<Args>(args)...);
}

template <typename T>
constexpr void destroy(T* p) noexcept {
    if constexpr(!std::is_trivially_destructible_v<T>) {
        std::destroy_at(p);
    }
}

template <typename T>
constexpr void destroy_range(T* first, T* last) noexcept {
    if constexpr(!std::is_trivially_destructible_v<T>) {
        std::destroy(first, last);
    }
}

template <typename T>
[[nodiscard]]
constexpr std::size_t internal_range_length(const T* first, const T* last) noexcept {
    return static_cast<std::size_t>(last - first);
}

template <typename T>
[[nodiscard]]
constexpr std::size_t internal_range_length(std::move_iterator<T*> first,
                                            std::move_iterator<T*> last) noexcept {
    return internal_range_length(first.base(), last.base());
}

template <typename ForwardIt>
[[nodiscard]]
constexpr std::size_t external_range_length(ForwardIt first, ForwardIt last) {
    using ItDiffT = typename std::iterator_traits<ForwardIt>::difference_type;
    if constexpr(numeric_max<std::size_t>() < numeric_max<ItDiffT>()) {
        if constexpr(std::is_base_of_v<
                         std::random_access_iterator_tag,
                         typename std::iterator_traits<ForwardIt>::iterator_category>) {
            assert(0 <= (last - first) && "Invalid range.");
            const auto len = static_cast<std::size_t>(last - first);
#ifndef NDEBUG
            if(numeric_max<std::size_t>() < len)
                throw_range_length_error();
#endif
            return len;
        } else {
            if(std::is_constant_evaluated()) {
                ItDiffT len = 0;
                for(; !(first == last); ++first)
                    ++len;
                assert(static_cast<std::size_t>(len) <= numeric_max<std::size_t>());
                return static_cast<std::size_t>(len);
            }
            const auto len = static_cast<std::size_t>(std::distance(first, last));
#ifndef NDEBUG
            if(numeric_max<std::size_t>() < len)
                throw_range_length_error();
#endif
            return len;
        }
    } else {
        if(std::is_constant_evaluated()) {
            std::size_t len = 0;
            for(; !(first == last); ++first)
                ++len;
            return len;
        }
        return static_cast<std::size_t>(std::distance(first, last));
    }
}

template <typename Iterator,
          typename IteratorDiffT = typename std::iterator_traits<Iterator>::difference_type,
          typename Integer = IteratorDiffT>
[[nodiscard]]
constexpr Iterator unchecked_next(Iterator pos, Integer n = 1) noexcept {
    std::advance(pos, static_cast<IteratorDiffT>(n));
    return pos;
}

template <typename Iterator,
          typename IteratorDiffT = typename std::iterator_traits<Iterator>::difference_type,
          typename Integer = IteratorDiffT>
[[nodiscard]]
constexpr Iterator unchecked_prev(Iterator pos, Integer n = 1) noexcept {
    std::advance(pos, -static_cast<IteratorDiffT>(n));
    return pos;
}

template <typename Iterator,
          typename IteratorDiffT = typename std::iterator_traits<Iterator>::difference_type,
          typename Integer = IteratorDiffT>
constexpr void unchecked_advance(Iterator& pos, Integer n) noexcept {
    std::advance(pos, static_cast<IteratorDiffT>(n));
}

template <typename T, typename InputIt>
constexpr T* default_uninitialized_copy(InputIt first, InputIt last, T* d_first) {
    T* d_last = d_first;
    try {
        // Note: Not != because `using namespace std::rel_ops` can break constexpr.
        for(; !(first == last); ++first, static_cast<void>(++d_last))
            construct(d_last, *first);
        return d_last;
    } catch(...) {
        destroy_range(d_first, d_last);
        throw;
    }
}

template <typename T, typename ForwardIt>
constexpr T* uninitialized_copy(ForwardIt first, ForwardIt last, T* dest) {
    if constexpr(is_uninitialized_memcpyable_iterator<T, ForwardIt>::value) {
        static_assert(std::is_constructible_v<T, decltype(*first)>,
                      "`value_type` must be copy constructible.");
        if(std::is_constant_evaluated())
            return default_uninitialized_copy(first, last, dest);
        const std::size_t num_copy = external_range_length(first, last);
        if(num_copy != 0)
            std::memcpy(dest, std::to_address(first), num_copy * sizeof(T));
        return unchecked_next(dest, num_copy);
    } else {
        return default_uninitialized_copy(first, last, dest);
    }
}

// Unwrap move_iterators for memcpy optimization.
template <typename T, typename ForwardIt>
    requires is_uninitialized_memcpyable_iterator<T, ForwardIt>::value
constexpr T* uninitialized_copy(std::move_iterator<ForwardIt> first,
                                std::move_iterator<ForwardIt> last,
                                T* dest) noexcept {
    return uninitialized_copy(first.base(), last.base(), dest);
}

template <typename T>
constexpr T* uninitialized_value_construct(T* first, T* last) {
    if constexpr(std::is_trivially_constructible_v<T>) {
        if(!std::is_constant_evaluated()) {
            std::fill(first, last, T());
            return last;
        }
    }
    T* curr = first;
    try {
        for(; !(curr == last); ++curr)
            construct(curr);
        return curr;
    } catch(...) {
        destroy_range(first, curr);
        throw;
    }
}

template <typename T>
constexpr T* uninitialized_fill(T* first, T* last) {
    return uninitialized_value_construct(first, last);
}

template <typename T>
constexpr T* uninitialized_fill(T* first, T* last, const T& val) {
    T* curr = first;
    try {
        for(; !(curr == last); ++curr)
            construct(curr, val);
        return curr;
    } catch(...) {
        destroy_range(first, curr);
        throw;
    }
}

template <typename Pointer, typename SizeT>
class small_vector_data_base {
public:
    using ptr = Pointer;
    using size_ty = SizeT;

    small_vector_data_base(void) = default;
    small_vector_data_base(const small_vector_data_base&) = default;
    small_vector_data_base(small_vector_data_base&&) noexcept = default;
    small_vector_data_base& operator=(const small_vector_data_base&) = default;
    small_vector_data_base& operator=(small_vector_data_base&&) noexcept = default;
    ~small_vector_data_base(void) = default;

    constexpr ptr data_ptr(void) const noexcept {
        return m_data_ptr;
    }

    constexpr size_ty capacity(void) const noexcept {
        return m_capacity;
    }

    constexpr size_ty size(void) const noexcept {
        return m_size;
    }

    constexpr void set_data_ptr(ptr data_ptr) noexcept {
        m_data_ptr = data_ptr;
    }

    constexpr void set_capacity(size_ty capacity) noexcept {
        m_capacity = capacity;
    }

    constexpr void set_size(size_ty size) noexcept {
        m_size = size;
    }

    constexpr void set(ptr data_ptr, size_ty capacity, size_ty size) {
        m_data_ptr = data_ptr;
        m_capacity = capacity;
        m_size = size;
    }

    constexpr void swap_data_ptr(small_vector_data_base& other) noexcept {
        using std::swap;
        swap(m_data_ptr, other.m_data_ptr);
    }

    constexpr void swap_capacity(small_vector_data_base& other) noexcept {
        using std::swap;
        swap(m_capacity, other.m_capacity);
    }

    constexpr void swap_size(small_vector_data_base& other) noexcept {
        using std::swap;
        swap(m_size, other.m_size);
    }

    constexpr void swap(small_vector_data_base& other) noexcept {
        using std::swap;
        swap(m_data_ptr, other.m_data_ptr);
        swap(m_capacity, other.m_capacity);
        swap(m_size, other.m_size);
    }

private:
    ptr m_data_ptr;
    size_ty m_capacity;
    size_ty m_size;
};

template <typename Pointer, typename SizeT, typename T, unsigned InlineCapacity>
class small_vector_data : public small_vector_data_base<Pointer, SizeT> {
public:
    small_vector_data(void) = default;
    small_vector_data(const small_vector_data&) = delete;
    small_vector_data(small_vector_data&&) noexcept = delete;
    small_vector_data& operator=(const small_vector_data&) = delete;
    small_vector_data& operator=(small_vector_data&&) noexcept = delete;
    ~small_vector_data(void) = default;

    constexpr T* storage(void) noexcept {
        return m_storage.get_inline_ptr();
    }

private:
    inline_storage<T, InlineCapacity> m_storage;
};

template <typename Pointer, typename SizeT, typename T>
class small_vector_data<Pointer, SizeT, T, 0> : public small_vector_data_base<Pointer, SizeT> {
public:
    small_vector_data(void) = default;
    small_vector_data(const small_vector_data&) = delete;
    small_vector_data(small_vector_data&&) noexcept = delete;
    small_vector_data& operator=(const small_vector_data&) = delete;
    small_vector_data& operator=(small_vector_data&&) noexcept = delete;
    ~small_vector_data(void) = default;

    constexpr T* storage(void) noexcept {
        return nullptr;
    }
};

template <typename T, unsigned InlineCapacity>
class small_vector_base {
public:
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename SameT, unsigned DifferentInlineCapacity>
    friend class small_vector_base;

protected:
    using value_ty = T;
    using ptr = T*;
    using cptr = const T*;
    using size_ty = std::size_t;
    using diff_ty = std::ptrdiff_t;

    static_assert(
        is_complete_type<value_ty>::value || InlineCapacity == 0,
        "`value_type` must be complete for instantiation of a non-zero number " "of inline elements.");

    template <typename U>
    using is_complete = is_complete_type<U>;

    [[nodiscard]] constexpr static ptr allocate(size_ty n) {
        return std::allocator<T>{}.allocate(n);
    }

    [[nodiscard]] constexpr static ptr allocate_with_hint(size_ty n, cptr) {
        return std::allocator<T>{}.allocate(n);
    }

    constexpr static void deallocate(ptr p, size_ty n) {
        std::allocator<T>{}.deallocate(p, n);
    }

    [[nodiscard]] constexpr size_ty get_max_size(void) const noexcept {
        return (std::min)(static_cast<size_ty>(std::allocator_traits<std::allocator<T>>::max_size(
                              std::allocator<T>{})),
                          static_cast<size_ty>(numeric_max<difference_type>()));
    }

    [[nodiscard]]
    static consteval size_ty get_inline_capacity(void) noexcept {
        return static_cast<size_ty>(InlineCapacity);
    }

    // Simplified emplace traits using std type traits directly.
    template <typename... Args>
    struct is_emplace_insertable {
        constexpr static bool value = [] {
            if constexpr(is_complete_type<value_ty>::value)
                return std::is_constructible_v<value_ty, Args...>;
            else
                return false;
        }();
    };

    template <typename... Args>
    struct is_nothrow_emplace_insertable {
        constexpr static bool value = [] {
            if constexpr(is_complete_type<value_ty>::value)
                return std::is_nothrow_constructible_v<value_ty, Args...>;
            else
                return false;
        }();
    };

    template <typename V = value_ty>
    struct is_explicitly_move_insertable : is_emplace_insertable<V&&> {};

    template <typename V = value_ty>
    struct is_explicitly_nothrow_move_insertable : is_nothrow_emplace_insertable<V&&> {};

    template <typename V = value_ty>
    struct is_explicitly_copy_insertable :
        std::bool_constant<is_emplace_insertable<V&>::value &&
                           is_emplace_insertable<const V&>::value> {};

    template <typename V = value_ty>
    struct is_explicitly_nothrow_copy_insertable :
        std::bool_constant<is_nothrow_emplace_insertable<V&>::value &&
                           is_nothrow_emplace_insertable<const V&>::value> {};

    template <typename V>
    struct relocate_with_move :
        std::bool_constant<std::is_nothrow_move_constructible_v<V> ||
                           !is_explicitly_copy_insertable<V>::value> {};

    template <typename V>
    struct is_nothrow_swappable : std::bool_constant<std::is_nothrow_swappable_v<V>> {};

    template <typename QualifiedFrom, typename QualifiedTo = value_ty>
    using is_memcpyable = detail::is_memcpyable<QualifiedFrom, QualifiedTo>;

    template <typename InputIt>
    using is_memcpyable_iterator = detail::is_memcpyable_iterator<value_ty, InputIt>;

    [[noreturn]]
    constexpr static void throw_index_error(void) {
        throw std::out_of_range("The requested index was out of range.");
    }

    [[noreturn]]
    constexpr static void throw_allocation_size_error(void) {
        throw std::length_error("The required allocation exceeds the maximum size.");
    }

    [[nodiscard]] constexpr ptr ptr_cast(cptr it) noexcept {
        return unchecked_next(begin_ptr(), it - begin_ptr());
    }

private:
    class stack_temporary {
    public:
        stack_temporary(void) = delete;
        stack_temporary(const stack_temporary&) = delete;
        stack_temporary(stack_temporary&&) noexcept = delete;
        stack_temporary& operator=(const stack_temporary&) = delete;
        stack_temporary& operator=(stack_temporary&&) noexcept = delete;

        template <typename... Args>
        constexpr explicit stack_temporary(Args&&... args) {
            construct(get_pointer(), std::forward<Args>(args)...);
        }

        constexpr ~stack_temporary(void) {
            destroy(get_pointer());
        }

        [[nodiscard]] constexpr const value_ty& get(void) const noexcept {
            return *get_pointer();
        }

        [[nodiscard]] constexpr value_ty&& release(void) noexcept {
            return std::move(*get_pointer());
        }

    private:
        [[nodiscard]] constexpr cptr get_pointer(void) const noexcept {
            return static_cast<cptr>(static_cast<const void*>(std::addressof(m_data)));
        }

        [[nodiscard]] constexpr ptr get_pointer(void) noexcept {
            return static_cast<ptr>(static_cast<void*>(std::addressof(m_data)));
        }

        alignas(alignof(value_ty)) unsigned char m_data[sizeof(value_ty)];
    };

    class heap_temporary {
    public:
        heap_temporary(void) = delete;
        heap_temporary(const heap_temporary&) = delete;
        heap_temporary(heap_temporary&&) noexcept = delete;
        heap_temporary& operator=(const heap_temporary&) = delete;
        heap_temporary& operator=(heap_temporary&&) noexcept = delete;

        template <typename... Args>
        constexpr explicit heap_temporary(Args&&... args) : m_data_ptr(allocate(sizeof(value_ty))) {
            try {
                construct(m_data_ptr, std::forward<Args>(args)...);
            } catch(...) {
                deallocate(m_data_ptr, sizeof(value_ty));
                throw;
            }
        }

        constexpr ~heap_temporary(void) {
            destroy(m_data_ptr);
            deallocate(m_data_ptr, sizeof(value_ty));
        }

        [[nodiscard]] constexpr const value_ty& get(void) const noexcept {
            return *m_data_ptr;
        }

        [[nodiscard]] constexpr value_ty&& release(void) noexcept {
            return std::move(*m_data_ptr);
        }

    private:
        ptr m_data_ptr;
    };

    constexpr void wipe(void) {
        destroy_range(begin_ptr(), end_ptr());
        if(has_allocation())
            deallocate(data_ptr(), get_capacity());
    }

    constexpr void set_data_ptr(ptr data_ptr) noexcept {
        m_data.set_data_ptr(data_ptr);
    }

    constexpr void set_capacity(size_ty capacity) noexcept {
        assert(InlineCapacity <= capacity && "capacity must be greater than InlineCapacity.");
        m_data.set_capacity(static_cast<size_type>(capacity));
    }

    constexpr void set_size(size_ty size) noexcept {
        m_data.set_size(static_cast<size_type>(size));
    }

    constexpr void set_data(ptr data_ptr, size_ty capacity, size_ty size) noexcept {
        m_data.set(data_ptr, static_cast<size_type>(capacity), static_cast<size_type>(size));
    }

    constexpr void reset_data(ptr data_ptr, size_ty capacity, size_ty size) {
        wipe();
        m_data.set(data_ptr, static_cast<size_type>(capacity), static_cast<size_type>(size));
    }

    constexpr void increase_size(size_ty n) noexcept {
        m_data.set_size(get_size() + n);
    }

    constexpr void decrease_size(size_ty n) noexcept {
        m_data.set_size(get_size() - n);
    }

    constexpr ptr unchecked_allocate(size_ty n) {
        assert(InlineCapacity < n && "Allocated capacity should be greater than InlineCapacity.");
        return allocate(n);
    }

    constexpr ptr unchecked_allocate(size_ty n, cptr hint) {
        assert(InlineCapacity < n && "Allocated capacity should be greater than InlineCapacity.");
        return allocate_with_hint(n, hint);
    }

    constexpr ptr checked_allocate(size_ty n) {
        if(get_max_size() < n)
            throw_allocation_size_error();
        return unchecked_allocate(n);
    }

protected:
    [[nodiscard]] constexpr size_ty calculate_new_capacity(const size_ty current,
                                                           const size_ty required) const noexcept {
        assert(current < required);

        if(get_max_size() - current <= current)
            return get_max_size();

        const size_ty new_capacity = 2 * current;
        if(new_capacity < required)
            return required;
        return new_capacity;
    }

    [[nodiscard]] constexpr size_ty
        unchecked_calculate_new_capacity(const size_ty minimum_required_capacity) const noexcept {
        return calculate_new_capacity(get_capacity(), minimum_required_capacity);
    }

    [[nodiscard]] constexpr size_ty
        checked_calculate_new_capacity(const size_ty minimum_required_capacity) const {
        if(get_max_size() < minimum_required_capacity)
            throw_allocation_size_error();
        return unchecked_calculate_new_capacity(minimum_required_capacity);
    }

    template <typename ForwardIt>
    constexpr void overwrite_existing_elements(const ForwardIt first,
                                               const ForwardIt last,
                                               const size_ty count) {
        assert(count <= get_capacity() && "Not enough capacity.");
        if(get_size() < count) {
            ForwardIt pivot = copy_n_return_in(first, get_size(), begin_ptr());
            uninitialized_copy(pivot, last, end_ptr());
        } else {
            ptr new_end = copy_range(first, last, begin_ptr());
            destroy_range(new_end, end_ptr());
        }
    }

    template <unsigned N>
    constexpr small_vector_base& copy_assign(const small_vector_base<T, N>& other) {
        assign_with_range(other.begin_ptr(), other.end_ptr(), std::random_access_iterator_tag{});
        return *this;
    }

    template <unsigned N>
    constexpr small_vector_base& move_assign_pointer(small_vector_base<T, N>& other) noexcept {
        reset_data(other.begin_ptr(), other.get_capacity(), other.get_size());
        other.set_default();
        return *this;
    }

    template <unsigned N>
    constexpr small_vector_base& move_assign_equal_allocators(small_vector_base<T, N>& other) {
        if constexpr(N == 0 && InlineCapacity == 0) {
            return move_assign_pointer(other);
        } else {
            if(other.has_allocation() && InlineCapacity < other.get_capacity())
                return move_assign_pointer(other);
            else {
                assign_with_range(std::make_move_iterator(other.begin_ptr()),
                                  std::make_move_iterator(other.end_ptr()),
                                  std::random_access_iterator_tag{});
                return *this;
            }
        }
    }

    template <unsigned N>
    constexpr small_vector_base& move_assign(small_vector_base<T, N>& other) {
        return move_assign_equal_allocators(other);
    }

    template <unsigned I>
    constexpr void move_initialize(small_vector_base<T, I>&& other) {
        if constexpr(I == 0 && InlineCapacity == 0) {
            set_data(other.data_ptr(), other.get_capacity(), other.get_size());
            other.set_default();
        } else if constexpr(I <= InlineCapacity) {
            if(InlineCapacity < other.get_capacity()) {
                set_data(other.data_ptr(), other.get_capacity(), other.get_size());
                other.set_default();
            } else {
                set_to_inline_storage();
                uninitialized_move(other.begin_ptr(), other.end_ptr(), data_ptr());
                set_size(other.get_size());
            }
        } else {
            if(other.has_allocation()) {
                set_data(other.data_ptr(), other.get_capacity(), other.get_size());
                other.set_default();
            } else {
                if(InlineCapacity < other.get_size()) {
                    set_data_ptr(unchecked_allocate(other.get_size(), other.allocation_end_ptr()));
                    set_capacity(other.get_size());
                    try {
                        uninitialized_move(other.begin_ptr(), other.end_ptr(), data_ptr());
                    } catch(...) {
                        deallocate(data_ptr(), get_capacity());
                        throw;
                    }
                } else {
                    set_to_inline_storage();
                    uninitialized_move(other.begin_ptr(), other.end_ptr(), data_ptr());
                }
                set_size(other.get_size());
            }
        }
    }

public:
    //    small_vector_base            (void)                         = impl;
    small_vector_base(const small_vector_base&) = delete;
    small_vector_base(small_vector_base&&) noexcept = delete;
    small_vector_base& operator=(const small_vector_base&) = delete;
    small_vector_base& operator=(small_vector_base&&) noexcept = delete;

    //    ~small_vector_base           (void)                         = impl;

    constexpr small_vector_base(void) noexcept {
        set_default();
    }

    constexpr static struct bypass_tag {
    } bypass{};

    template <unsigned I>
    constexpr small_vector_base(bypass_tag, const small_vector_base<T, I>& other) {
        if(InlineCapacity < other.get_size()) {
            set_data_ptr(unchecked_allocate(other.get_size(), other.allocation_end_ptr()));
            set_capacity(other.get_size());

            try {
                uninitialized_copy(other.begin_ptr(), other.end_ptr(), data_ptr());
            } catch(...) {
                deallocate(data_ptr(), get_capacity());
                throw;
            }
        } else {
            set_to_inline_storage();
            uninitialized_copy(other.begin_ptr(), other.end_ptr(), data_ptr());
        }

        set_size(other.get_size());
    }

    template <unsigned I>
    constexpr small_vector_base(bypass_tag, small_vector_base<T, I>&& other) noexcept(
        std::is_nothrow_move_constructible<value_ty>::value || (I == 0 && I == InlineCapacity)) {
        move_initialize(std::move(other));
    }

    constexpr small_vector_base(size_ty count) {
        if(InlineCapacity < count) {
            set_data_ptr(checked_allocate(count));
            set_capacity(count);
        } else
            set_to_inline_storage();

        try {
            uninitialized_value_construct(begin_ptr(), unchecked_next(begin_ptr(), count));
        } catch(...) {
            if(has_allocation())
                deallocate(data_ptr(), get_capacity());
            throw;
        }
        set_size(count);
    }

    constexpr small_vector_base(size_ty count, const value_ty& val) {
        if(InlineCapacity < count) {
            set_data_ptr(checked_allocate(count));
            set_capacity(count);
        } else
            set_to_inline_storage();

        try {
            uninitialized_fill(begin_ptr(), unchecked_next(begin_ptr(), count), val);
        } catch(...) {
            if(has_allocation())
                deallocate(data_ptr(), get_capacity());
            throw;
        }
        set_size(count);
    }

    template <typename Generator>
    constexpr small_vector_base(size_ty count, Generator& g) {
        if(InlineCapacity < count) {
            set_data_ptr(checked_allocate(count));
            set_capacity(count);
        } else
            set_to_inline_storage();

        ptr curr = begin_ptr();
        const ptr new_end = unchecked_next(begin_ptr(), count);
        try {
            for(; !(curr == new_end); ++curr)
                construct(curr, g());
        } catch(...) {
            destroy_range(begin_ptr(), curr);
            if(has_allocation())
                deallocate(data_ptr(), get_capacity());
            throw;
        }
        set_size(count);
    }

    template <std::input_iterator InputIt>
    constexpr small_vector_base(InputIt first, InputIt last, std::input_iterator_tag) :
        small_vector_base() {
        using iterator_cat = typename std::iterator_traits<InputIt>::iterator_category;
        append_range(first, last, iterator_cat{});
    }

    template <std::forward_iterator ForwardIt>
    constexpr small_vector_base(ForwardIt first, ForwardIt last, std::forward_iterator_tag) {
        size_ty count = external_range_length(first, last);
        if(InlineCapacity < count) {
            set_data_ptr(unchecked_allocate(count));
            set_capacity(count);
            try {
                uninitialized_copy(first, last, begin_ptr());
            } catch(...) {
                deallocate(data_ptr(), get_capacity());
                throw;
            }
        } else {
            set_to_inline_storage();
            uninitialized_copy(first, last, begin_ptr());
        }

        set_size(count);
    }

    constexpr ~small_vector_base(void) noexcept {
        assert(InlineCapacity <= get_capacity() && "Invalid capacity.");
        wipe();
    }

protected:
    constexpr void set_to_inline_storage(void) {
        set_capacity(InlineCapacity);
        if(std::is_constant_evaluated())
            return set_data_ptr(allocate(InlineCapacity));
        set_data_ptr(storage_ptr());
    }

    constexpr void assign_with_copies(size_ty count, const value_ty& val) {
        if(get_capacity() < count) {
            size_ty new_capacity = checked_calculate_new_capacity(count);
            ptr new_begin = unchecked_allocate(new_capacity);

            try {
                uninitialized_fill(new_begin, unchecked_next(new_begin, count), val);
            } catch(...) {
                deallocate(new_begin, new_capacity);
                throw;
            }

            reset_data(new_begin, new_capacity, count);
        } else if(get_size() < count) {
            std::fill(begin_ptr(), end_ptr(), val);
            uninitialized_fill(end_ptr(), unchecked_next(begin_ptr(), count), val);
            set_size(count);
        } else
            erase_range(std::fill_n(begin_ptr(), count, val), end_ptr());
    }

    template <typename InputIt>
    constexpr void assign_with_range(InputIt first, InputIt last, std::input_iterator_tag) {
        if constexpr(std::is_assignable_v<value_ty&, decltype(*std::declval<InputIt>())>) {
            using iterator_cat = typename std::iterator_traits<InputIt>::iterator_category;

            ptr curr = begin_ptr();
            for(; !(end_ptr() == curr || first == last); ++curr, static_cast<void>(++first))
                *curr = *first;

            if(first == last)
                erase_to_end(curr);
            else
                append_range(first, last, iterator_cat{});
        } else {
            using iterator_cat = typename std::iterator_traits<InputIt>::iterator_category;
            // If not assignable then destroy all elements and append.
            erase_all();
            append_range(first, last, iterator_cat{});
        }
    }

    template <typename ForwardIt>
    constexpr void assign_with_range(const ForwardIt first,
                                     const ForwardIt last,
                                     std::forward_iterator_tag) {
        const size_ty count = external_range_length(first, last);
        if(get_capacity() < count) {
            size_ty new_capacity = checked_calculate_new_capacity(count);
            ptr new_begin = unchecked_allocate(new_capacity);

            try {
                uninitialized_copy(first, last, new_begin);
            } catch(...) {
                deallocate(new_begin, new_capacity);
                throw;
            }

            wipe();
            set_data_ptr(new_begin);
            set_capacity(new_capacity);
        } else if(count <= InlineCapacity && has_allocation()) {
            // Eagerly move into inline storage.

            ptr new_begin = storage_ptr();
            if(std::is_constant_evaluated())
                new_begin = this->allocate(InlineCapacity);

            uninitialized_copy(first, last, new_begin);
            destroy_range(begin_ptr(), end_ptr());
            deallocate(begin_ptr(), get_capacity());
            set_data_ptr(new_begin);
            set_capacity(InlineCapacity);
        } else
            overwrite_existing_elements(first, last, count);

        set_size(count);
    }

    // Ie. move-if-noexcept.
    struct strong_exception_policy {};

    template <typename Policy = void>
    constexpr ptr uninitialized_move(ptr first, ptr last, ptr d_first) {
        constexpr bool use_move = is_explicitly_move_insertable<>::value &&
                                  (!std::is_same_v<Policy, strong_exception_policy> ||
                                   relocate_with_move<value_ty>::value);

        if constexpr(use_move) {
            return uninitialized_copy(std::make_move_iterator(first),
                                      std::make_move_iterator(last),
                                      d_first);
        } else {
            return uninitialized_copy(first, last, d_first);
        }
    }

    constexpr ptr shift_into_uninitialized(ptr pos, size_ty n_shift) {
        assert(n_shift != 0 && "The value of `n_shift` should not be 0.");

        const ptr original_end = end_ptr();
        const ptr pivot = unchecked_prev(original_end, n_shift);

        uninitialized_move(pivot, original_end, original_end);
        increase_size(n_shift);
        return move_right(pos, pivot, original_end);
    }

    template <typename... Args>
    constexpr ptr append_element(Args&&... args) {
        if(get_size() < get_capacity())
            return emplace_into_current_end(std::forward<Args>(args)...);
        return emplace_into_reallocation_end(std::forward<Args>(args)...);
    }

    constexpr ptr append_copies(size_ty count, const value_ty& val) {
        if(num_uninitialized() < count) {
            // Reallocate.
            if(get_max_size() - get_size() < count)
                throw_allocation_size_error();

            size_ty original_size = get_size();
            size_ty new_size = get_size() + count;

            // The check is handled by the if-guard.
            size_ty new_capacity = unchecked_calculate_new_capacity(new_size);
            ptr new_data_ptr = unchecked_allocate(new_capacity, allocation_end_ptr());
            ptr new_last = unchecked_next(new_data_ptr, original_size);

            try {
                new_last = uninitialized_fill(new_last, unchecked_next(new_last, count), val);
                uninitialized_move(begin_ptr(), end_ptr(), new_data_ptr);
            } catch(...) {
                destroy_range(unchecked_next(new_data_ptr, original_size), new_last);
                deallocate(new_data_ptr, new_capacity);
                throw;
            }

            reset_data(new_data_ptr, new_capacity, new_size);
            return unchecked_next(new_data_ptr, original_size);
        } else {
            const ptr ret = end_ptr();
            uninitialized_fill(ret, unchecked_next(ret, count), val);
            increase_size(count);
            return ret;
        }
    }

    template <typename MovePolicy = void, typename InputIt>
    constexpr ptr append_range(InputIt first, InputIt last, std::input_iterator_tag) {
        size_ty original_size = get_size();
        if constexpr(std::is_same_v<MovePolicy, strong_exception_policy>) {
            // Append with a strong exception guarantee.
            for(; !(first == last); ++first) {
                try {
                    append_element(*first);
                } catch(...) {
                    erase_range(unchecked_next(begin_ptr(), original_size), end_ptr());
                    throw;
                }
            }
        } else {
            for(; !(first == last); ++first)
                append_element(*first);
        }
        return unchecked_next(begin_ptr(), original_size);
    }

    template <typename MovePolicy = void, typename ForwardIt>
    constexpr ptr append_range(ForwardIt first, ForwardIt last, std::forward_iterator_tag) {
        const size_ty num_insert = external_range_length(first, last);

        if(num_uninitialized() < num_insert) {
            // Reallocate.
            if(get_max_size() - get_size() < num_insert)
                throw_allocation_size_error();

            size_ty original_size = get_size();
            size_ty new_size = get_size() + num_insert;

            // The check is handled by the if-guard.
            size_ty new_capacity = unchecked_calculate_new_capacity(new_size);
            ptr new_data_ptr = unchecked_allocate(new_capacity, allocation_end_ptr());
            ptr new_last = unchecked_next(new_data_ptr, original_size);

            try {
                new_last = uninitialized_copy(first, last, new_last);
                uninitialized_move<MovePolicy>(begin_ptr(), end_ptr(), new_data_ptr);
            } catch(...) {
                destroy_range(unchecked_next(new_data_ptr, original_size), new_last);
                deallocate(new_data_ptr, new_capacity);
                throw;
            }

            reset_data(new_data_ptr, new_capacity, new_size);
            return unchecked_next(new_data_ptr, original_size);
        } else {
            ptr ret = end_ptr();
            uninitialized_copy(first, last, ret);
            increase_size(num_insert);
            return ret;
        }
    }

    template <typename... Args>
    constexpr ptr emplace_at(ptr pos, Args&&... args) {
        assert(get_size() <= get_capacity() && "size was greater than capacity");

        if(get_size() < get_capacity())
            return emplace_into_current(pos, std::forward<Args>(args)...);
        return emplace_into_reallocation(pos, std::forward<Args>(args)...);
    }

    constexpr ptr insert_copies(ptr pos, size_ty count, const value_ty& val) {
        if(0 == count)
            return pos;

        if(end_ptr() == pos) {
            if(1 == count)
                return append_element(val);
            return append_copies(count, val);
        }

        if(num_uninitialized() < count) {
            // Reallocate.
            if(get_max_size() - get_size() < count)
                throw_allocation_size_error();

            const size_ty offset = internal_range_length(begin_ptr(), pos);

            const size_ty new_size = get_size() + count;

            // The check is handled by the if-guard.
            const size_ty new_capacity = unchecked_calculate_new_capacity(new_size);
            ptr new_data_ptr = unchecked_allocate(new_capacity, allocation_end_ptr());
            ptr new_first = unchecked_next(new_data_ptr, offset);
            ptr new_last = new_first;

            try {
                uninitialized_fill(new_first, unchecked_next(new_first, count), val);
                unchecked_advance(new_last, count);

                uninitialized_move(begin_ptr(), pos, new_data_ptr);
                new_first = new_data_ptr;
                uninitialized_move(pos, end_ptr(), new_last);
            } catch(...) {
                destroy_range(new_first, new_last);
                deallocate(new_data_ptr, new_capacity);
                throw;
            }

            reset_data(new_data_ptr, new_capacity, new_size);
            return unchecked_next(begin_ptr(), offset);
        } else {
            const size_ty tail_size = internal_range_length(pos, end_ptr());
            if(tail_size < count) {
                ptr original_end = end_ptr();

                size_ty num_val_tail = count - tail_size;

                if(std::is_constant_evaluated()) {
                    uninitialized_fill(end_ptr(), unchecked_next(end_ptr(), num_val_tail), val);
                    increase_size(num_val_tail);

                    const heap_temporary tmp(val);

                    uninitialized_move(pos, original_end, end_ptr());
                    increase_size(tail_size);

                    std::fill_n(pos, tail_size, tmp.get());

                    return pos;
                }

                uninitialized_fill(end_ptr(), unchecked_next(end_ptr(), num_val_tail), val);
                increase_size(num_val_tail);

                try {
                    // We need to handle possible aliasing here.
                    const stack_temporary tmp(val);

                    // Now, move the tail to the end.
                    uninitialized_move(pos, original_end, end_ptr());
                    increase_size(tail_size);

                    try {
                        // Finally, try to copy the rest of the elements over.
                        std::fill_n(pos, tail_size, tmp.get());
                    } catch(...) {
                        // Attempt to roll back and destroy the tail if we fail.
                        ptr inserted_end = unchecked_prev(end_ptr(), tail_size);
                        move_left(inserted_end, end_ptr(), pos);
                        destroy_range(inserted_end, end_ptr());
                        decrease_size(tail_size);
                        throw;
                    }
                } catch(...) {
                    // Destroy the elements constructed from the input.
                    destroy_range(original_end, end_ptr());
                    decrease_size(internal_range_length(original_end, end_ptr()));
                    throw;
                }
            } else {
                if(std::is_constant_evaluated()) {
                    const heap_temporary tmp(val);

                    ptr inserted_end = shift_into_uninitialized(pos, count);
                    std::fill(pos, inserted_end, tmp.get());

                    return pos;
                }
                const stack_temporary tmp(val);

                ptr inserted_end = shift_into_uninitialized(pos, count);

                try {
                    std::fill(pos, inserted_end, tmp.get());
                } catch(...) {
                    ptr original_end = move_left(inserted_end, end_ptr(), pos);
                    destroy_range(original_end, end_ptr());
                    decrease_size(count);
                    throw;
                }
            }
            return pos;
        }
    }

    template <typename ForwardIt>
    constexpr ptr insert_range_helper(ptr pos, ForwardIt first, ForwardIt last) {
        assert(!(first == last) && "The range should not be empty.");
        assert(!(end_ptr() == pos) && "`pos` should not be at the end.");

        const size_ty num_insert = external_range_length(first, last);
        if(num_uninitialized() < num_insert) {
            // Reallocate.
            if(get_max_size() - get_size() < num_insert)
                throw_allocation_size_error();

            const size_ty offset = internal_range_length(begin_ptr(), pos);
            const size_ty new_size = get_size() + num_insert;

            // The check is handled by the if-guard.
            const size_ty new_capacity = unchecked_calculate_new_capacity(new_size);
            const ptr new_data_ptr = unchecked_allocate(new_capacity, allocation_end_ptr());
            ptr new_first = unchecked_next(new_data_ptr, offset);
            ptr new_last = new_first;

            try {
                uninitialized_copy(first, last, new_first);
                unchecked_advance(new_last, num_insert);

                uninitialized_move(begin_ptr(), pos, new_data_ptr);
                new_first = new_data_ptr;
                uninitialized_move(pos, end_ptr(), new_last);
            } catch(...) {
                destroy_range(new_first, new_last);
                deallocate(new_data_ptr, new_capacity);
                throw;
            }

            reset_data(new_data_ptr, new_capacity, new_size);
            return unchecked_next(begin_ptr(), offset);
        } else {
            const size_ty tail_size = internal_range_length(pos, end_ptr());
            if(tail_size < num_insert) {
                ptr original_end = end_ptr();
                ForwardIt pivot = unchecked_next(first, tail_size);

                uninitialized_copy(pivot, last, end_ptr());
                increase_size(num_insert - tail_size);

                try {
                    uninitialized_move(pos, original_end, end_ptr());
                    increase_size(tail_size);

                    try {
                        copy_range(first, pivot, pos);
                    } catch(...) {
                        ptr inserted_end = unchecked_prev(end_ptr(), tail_size);
                        move_left(inserted_end, end_ptr(), pos);
                        destroy_range(inserted_end, end_ptr());
                        decrease_size(tail_size);
                        throw;
                    }
                } catch(...) {
                    destroy_range(original_end, end_ptr());
                    decrease_size(internal_range_length(original_end, end_ptr()));
                    throw;
                }
            } else {
                shift_into_uninitialized(pos, num_insert);

                try {
                    copy_range(first, last, pos);
                } catch(...) {
                    ptr inserted_end = unchecked_next(pos, num_insert);
                    ptr original_end = move_left(inserted_end, end_ptr(), pos);
                    destroy_range(original_end, end_ptr());
                    decrease_size(num_insert);
                    throw;
                }
            }
            return pos;
        }
    }

    template <typename InputIt>
    constexpr ptr insert_range(ptr pos, InputIt first, InputIt last, std::input_iterator_tag) {
        assert(!(first == last) && "The range should not be empty.");

        // Ensure we use this specific overload to give a strong exception guarantee for 1 element.
        if(end_ptr() == pos)
            return append_range(first, last, std::input_iterator_tag{});

        using iterator_cat = typename std::iterator_traits<InputIt>::iterator_category;
        small_vector_base tmp(first, last, iterator_cat{});

        return insert_range_helper(pos,
                                   std::make_move_iterator(tmp.begin_ptr()),
                                   std::make_move_iterator(tmp.end_ptr()));
    }

    template <typename ForwardIt>
    constexpr ptr
        insert_range(ptr pos, ForwardIt first, ForwardIt last, std::forward_iterator_tag) {
        if(!(end_ptr() == pos))
            return insert_range_helper(pos, first, last);

        if(unchecked_next(first) == last)
            return append_element(*first);

        using iterator_cat = typename std::iterator_traits<ForwardIt>::iterator_category;
        return append_range(first, last, iterator_cat{});
    }

    template <typename... Args>
    constexpr ptr emplace_into_current_end(Args&&... args) {
        construct(end_ptr(), std::forward<Args>(args)...);
        increase_size(1);
        return unchecked_prev(end_ptr());
    }

    constexpr ptr emplace_into_current(ptr pos, value_ty&& val)
        requires std::is_nothrow_move_constructible_v<value_ty>
    {
        if(pos == end_ptr())
            return emplace_into_current_end(std::move(val));

        shift_into_uninitialized(pos, 1);
        destroy(pos);
        construct(pos, std::move(val));
        return pos;
    }

    template <typename... Args>
    constexpr ptr emplace_into_current(ptr pos, Args&&... args) {
        if(pos == end_ptr())
            return emplace_into_current_end(std::forward<Args>(args)...);

        if(std::is_constant_evaluated()) {
            heap_temporary tmp(std::forward<Args>(args)...);
            shift_into_uninitialized(pos, 1);
            *pos = tmp.release();
            return pos;
        }

        // This is necessary because of possible aliasing.
        stack_temporary tmp(std::forward<Args>(args)...);
        shift_into_uninitialized(pos, 1);
        *pos = tmp.release();
        return pos;
    }

    template <typename... Args>
    constexpr ptr emplace_into_reallocation_end(Args&&... args) {
        // Appending; strong exception guarantee.
        if(get_max_size() == get_size())
            throw_allocation_size_error();

        const size_ty new_size = get_size() + 1;

        // The check is handled by the if-guard.
        const size_ty new_capacity = unchecked_calculate_new_capacity(new_size);
        const ptr new_data_ptr = unchecked_allocate(new_capacity, allocation_end_ptr());
        const ptr emplace_pos = unchecked_next(new_data_ptr, get_size());

        try {
            construct(emplace_pos, std::forward<Args>(args)...);
            try {
                uninitialized_move<strong_exception_policy>(begin_ptr(), end_ptr(), new_data_ptr);
            } catch(...) {
                destroy(emplace_pos);
                throw;
            }
        } catch(...) {
            deallocate(new_data_ptr, new_capacity);
            throw;
        }

        reset_data(new_data_ptr, new_capacity, new_size);
        return emplace_pos;
    }

    template <typename... Args>
    constexpr ptr emplace_into_reallocation(ptr pos, Args&&... args) {
        const size_ty offset = internal_range_length(begin_ptr(), pos);
        if(offset == get_size())
            return emplace_into_reallocation_end(std::forward<Args>(args)...);

        if(get_max_size() == get_size())
            throw_allocation_size_error();

        const size_ty new_size = get_size() + 1;

        // The check is handled by the if-guard.
        const size_ty new_capacity = unchecked_calculate_new_capacity(new_size);
        const ptr new_data_ptr = unchecked_allocate(new_capacity, allocation_end_ptr());
        ptr new_first = unchecked_next(new_data_ptr, offset);
        ptr new_last = new_first;

        try {
            construct(new_first, std::forward<Args>(args)...);
            unchecked_advance(new_last, 1);

            uninitialized_move(begin_ptr(), pos, new_data_ptr);
            new_first = new_data_ptr;
            uninitialized_move(pos, end_ptr(), new_last);
        } catch(...) {
            destroy_range(new_first, new_last);
            deallocate(new_data_ptr, new_capacity);
            throw;
        }

        reset_data(new_data_ptr, new_capacity, new_size);
        return unchecked_next(begin_ptr(), offset);
    }

    constexpr ptr shrink_to_size(void) {
        if(!has_allocation() || get_size() == get_capacity())
            return begin_ptr();

        // The rest runs only if allocated.

        size_ty new_capacity;
        ptr new_data_ptr;

        if(InlineCapacity < get_size()) {
            new_capacity = get_size();
            new_data_ptr = unchecked_allocate(new_capacity, allocation_end_ptr());
        } else {
            // We move to inline storage.
            new_capacity = InlineCapacity;
            if(std::is_constant_evaluated())
                new_data_ptr = allocate(InlineCapacity);
            else
                new_data_ptr = storage_ptr();
        }

        uninitialized_move(begin_ptr(), end_ptr(), new_data_ptr);

        destroy_range(begin_ptr(), end_ptr());
        deallocate(data_ptr(), get_capacity());

        set_data_ptr(new_data_ptr);
        set_capacity(new_capacity);

        return begin_ptr();
    }

    template <typename... ValueT>
    constexpr void resize_with(size_ty new_size, const ValueT&... val) {
        // ValueT... should either be value_ty or empty.

        if(new_size == 0)
            erase_all();

        if(get_capacity() < new_size) {
            // Reallocate.

            if(get_max_size() < new_size)
                throw_allocation_size_error();

            const size_ty original_size = get_size();

            // The check is handled by the if-guard.
            const size_ty new_capacity = unchecked_calculate_new_capacity(new_size);
            ptr new_data_ptr = unchecked_allocate(new_capacity, allocation_end_ptr());
            ptr new_last = unchecked_next(new_data_ptr, original_size);

            try {
                new_last =
                    uninitialized_fill(new_last, unchecked_next(new_data_ptr, new_size), val...);

                // Strong exception guarantee.
                uninitialized_move<strong_exception_policy>(begin_ptr(), end_ptr(), new_data_ptr);
            } catch(...) {
                destroy_range(unchecked_next(new_data_ptr, original_size), new_last);
                deallocate(new_data_ptr, new_capacity);
                throw;
            }

            reset_data(new_data_ptr, new_capacity, new_size);
        } else if(get_size() < new_size) {
            // Construct in the uninitialized section.
            uninitialized_fill(end_ptr(), unchecked_next(begin_ptr(), new_size), val...);
            set_size(new_size);
        } else
            erase_range(unchecked_next(begin_ptr(), new_size), end_ptr());

        // Do nothing if the count is the same as the current size.
    }

    constexpr void request_capacity(size_ty request) {
        if(request <= get_capacity())
            return;

        size_ty new_capacity = checked_calculate_new_capacity(request);
        ptr new_begin = unchecked_allocate(new_capacity);

        try {
            uninitialized_move<strong_exception_policy>(begin_ptr(), end_ptr(), new_begin);
        } catch(...) {
            deallocate(new_begin, new_capacity);
            throw;
        }

        wipe();

        set_data_ptr(new_begin);
        set_capacity(new_capacity);
    }

    constexpr ptr erase_at(ptr pos) {
        move_left(unchecked_next(pos), end_ptr(), pos);
        erase_last();
        return pos;
    }

    constexpr void erase_last(void) {
        decrease_size(1);

        // The element located at end_ptr is still alive since the size decreased.
        destroy(end_ptr());
    }

    constexpr ptr erase_range(ptr first, ptr last) {
        if(!(first == last))
            erase_to_end(move_left(last, end_ptr(), first));
        return first;
    }

    constexpr void erase_to_end(ptr pos) {
        assert(0 <= (end_ptr() - pos) && "`pos` was in the uninitialized range");
        if(size_ty change = internal_range_length(pos, end_ptr())) {
            decrease_size(change);
            destroy_range(pos, unchecked_next(pos, change));
        }
    }

    constexpr void erase_all(void) {
        ptr curr_end = end_ptr();
        set_size(0);
        destroy_range(begin_ptr(), curr_end);
    }

    template <unsigned N>
    constexpr void swap_elements_equal(small_vector_base<T, N>& other) {
        if(other.get_size() < get_size())
            return other.swap_elements_equal(*this);

        const ptr other_tail = std::swap_ranges(begin_ptr(), end_ptr(), other.begin_ptr());
        uninitialized_move(other_tail, other.end_ptr(), end_ptr());
        destroy_range(other_tail, other.end_ptr());
    }

    template <unsigned LessEqualI>
    constexpr void swap_equal_or_propagated_allocators(small_vector_base<T, LessEqualI>& other) {
        if constexpr(LessEqualI == 0 && InlineCapacity == 0) {
            m_data.swap(other.m_data);
            return;
        }

        static_assert(LessEqualI <= InlineCapacity, "should not be instantiated");

        if(has_allocation()) {
            if(InlineCapacity < other.get_capacity()) {
                // Note: This is always the branch that will run when constant-evaluated.
                m_data.swap_data_ptr(other.m_data);
                m_data.swap_capacity(other.m_data);
            } else {
                ptr new_data_ptr = storage_ptr();
                if(std::is_constant_evaluated())
                    new_data_ptr = other.allocate(InlineCapacity);

                uninitialized_move(other.begin_ptr(), other.end_ptr(), new_data_ptr);

                other.wipe();
                other.set_data_ptr(data_ptr());
                other.set_capacity(get_capacity());

                set_data_ptr(new_data_ptr);
                set_capacity(InlineCapacity);
            }
        } else if(InlineCapacity < other.get_capacity()) {
            // This implies that `other` is allocated, and that we can use its pointer.

            size_ty new_capacity = LessEqualI;
            ptr new_data_ptr = other.storage_ptr();

            // Check to see if we can store our elements in the inline storage of `other`.
            if(LessEqualI < get_size()) {
                new_capacity = other.calculate_new_capacity(LessEqualI, get_size());
                new_data_ptr = allocate_with_hint(new_capacity, allocation_end_ptr());
                try {
                    uninitialized_move(begin_ptr(), end_ptr(), new_data_ptr);
                } catch(...) {
                    deallocate(new_data_ptr, new_capacity);
                    throw;
                }
            } else
                uninitialized_move(begin_ptr(), end_ptr(), new_data_ptr);

            destroy_range(begin_ptr(), end_ptr());

            set_data_ptr(other.data_ptr());
            set_capacity(other.get_capacity());

            other.set_data_ptr(new_data_ptr);
            other.set_capacity(new_capacity);
        } else if(LessEqualI < get_size()) {
            // We have too many elements to store in `other`. Allocate a new buffer.
            size_ty new_capacity = other.calculate_new_capacity(LessEqualI, get_size());
            ptr new_data_ptr = allocate_with_hint(new_capacity, other.allocation_end_ptr());

            try {
                const ptr new_end = uninitialized_move(begin_ptr(), end_ptr(), new_data_ptr);
                try {
                    overwrite_existing_elements(std::make_move_iterator(other.begin_ptr()),
                                                std::make_move_iterator(other.end_ptr()),
                                                other.get_size());
                } catch(...) {
                    destroy_range(new_data_ptr, new_end);
                    throw;
                }
            } catch(...) {
                deallocate(new_data_ptr, new_capacity);
                throw;
            }

            other.wipe();
            other.set_data_ptr(new_data_ptr);
            other.set_capacity(new_capacity);
        } else if(other.has_allocation()) {
            const ptr new_end = uninitialized_move(begin_ptr(), end_ptr(), other.storage_ptr());
            try {
                overwrite_existing_elements(std::make_move_iterator(other.begin_ptr()),
                                            std::make_move_iterator(other.end_ptr()),
                                            other.get_size());
            } catch(...) {
                destroy_range(other.storage_ptr(), new_end);
                throw;
            }

            destroy_range(other.begin_ptr(), other.end_ptr());
            other.deallocate(other.data_ptr(), other.get_capacity());
            other.set_data_ptr(other.storage_ptr());
            other.set_capacity(LessEqualI);
        } else
            swap_elements_equal(other);

        m_data.swap_size(other.m_data);
    }

    template <unsigned I>
    constexpr void swap(small_vector_base<T, I>& other) {
        if constexpr(InlineCapacity < I) {
            return other.swap(*this);
        } else {
            return swap_equal_or_propagated_allocators(other);
        }
    }

#ifdef __GLIBCXX__

    // These are compatibility fixes for libstdc++ because std::copy doesn't work for
    // `move_iterator`s when constant evaluated.

    template <typename InputIt>
    constexpr static InputIt unmove_iterator(InputIt it) {
        return it;
    }

    template <typename InputIt>
    constexpr static auto unmove_iterator(std::move_iterator<InputIt> it)
        -> decltype(unmove_iterator(it.base())) {
        return unmove_iterator(it.base());
    }

    template <typename InputIt>
    constexpr static auto unmove_iterator(std::reverse_iterator<InputIt> it)
        -> std::reverse_iterator<decltype(unmove_iterator(it.base()))> {
        return std::reverse_iterator<decltype(unmove_iterator(it.base()))>(
            unmove_iterator(it.base()));
    }

#else

    template <typename InputIt>
    static constexpr InputIt unmove_iterator(InputIt it) {
        return it;
    }

#endif

    template <typename InputIt>
    constexpr ptr copy_range(InputIt first, InputIt last, ptr dest) {
        if constexpr(!std::is_same<decltype(unmove_iterator(first)), InputIt>::value)
            if(std::is_constant_evaluated())
                return std::move(unmove_iterator(first), unmove_iterator(last), dest);

        return std::copy(first, last, dest);
    }

    template <typename InputIt>
    constexpr InputIt copy_n_return_in(InputIt first, size_ty count, ptr dest) {
        if constexpr(is_memcpyable_iterator<InputIt>::value) {
            if(std::is_constant_evaluated()) {
                std::copy_n(first, count, dest);
                return unchecked_next(first, count);
            }
            if(count != 0)
                std::memcpy(std::to_address(dest),
                            std::to_address(first),
                            count * sizeof(value_ty));
            return unchecked_next(first, count);
        } else if constexpr(std::is_base_of_v<
                                std::random_access_iterator_tag,
                                typename std::iterator_traits<InputIt>::iterator_category>) {
            if constexpr(!std::is_same_v<decltype(unmove_iterator(first)), InputIt>) {
                if(std::is_constant_evaluated()) {
                    auto bfirst = unmove_iterator(first);
                    auto blast = unchecked_next(bfirst, count);
                    std::move(bfirst, blast, dest);
                    return unchecked_next(first, count);
                }
            }
            InputIt last = unchecked_next(first, count);
            copy_range(first, last, dest);
            return last;
        } else {
            for(; count != 0; --count, static_cast<void>(++dest), static_cast<void>(++first))
                *dest = *first;
            return first;
        }
    }

    // Unwrap move_iterator for memcpyable types.
    template <typename InputIt>
        requires is_memcpyable_iterator<InputIt>::value
    constexpr std::move_iterator<InputIt> copy_n_return_in(std::move_iterator<InputIt> first,
                                                           size_ty count,
                                                           ptr dest) noexcept {
        return std::move_iterator<InputIt>(copy_n_return_in(first.base(), count, dest));
    }

    constexpr ptr move_left(ptr first, ptr last, ptr d_first) {
        if constexpr(is_memcpyable<value_ty>::value) {
            if(std::is_constant_evaluated())
                return std::move(first, last, d_first);
            const size_ty num_moved = internal_range_length(first, last);
            if(num_moved != 0)
                std::memmove(std::to_address(d_first),
                             std::to_address(first),
                             num_moved * sizeof(value_ty));
            return unchecked_next(d_first, num_moved);
        } else {
            return std::move(first, last, d_first);
        }
    }

    constexpr ptr move_right(ptr first, ptr last, ptr d_last) {
        if constexpr(is_memcpyable<value_ty>::value) {
            if(std::is_constant_evaluated())
                return std::move_backward(first, last, d_last);
            const size_ty num_moved = internal_range_length(first, last);
            const ptr dest = unchecked_prev(d_last, num_moved);
            if(num_moved != 0)
                std::memmove(std::to_address(dest),
                             std::to_address(first),
                             num_moved * sizeof(value_ty));
            return dest;
        } else {
            return std::move_backward(first, last, d_last);
        }
    }

public:
    constexpr void set_default(void) {
        set_to_inline_storage();
        set_size(0);
    }

    [[nodiscard]] constexpr ptr data_ptr(void) noexcept {
        return m_data.data_ptr();
    }

    [[nodiscard]] constexpr cptr data_ptr(void) const noexcept {
        return m_data.data_ptr();
    }

    [[nodiscard]] constexpr size_ty get_capacity(void) const noexcept {
        return m_data.capacity();
    }

    [[nodiscard]] constexpr size_ty get_size(void) const noexcept {
        return m_data.size();
    }

    [[nodiscard]] constexpr size_ty num_uninitialized(void) const noexcept {
        return get_capacity() - get_size();
    }

    [[nodiscard]] constexpr ptr begin_ptr(void) noexcept {
        return data_ptr();
    }

    [[nodiscard]]
    constexpr cptr begin_ptr(void) const noexcept {
        return data_ptr();
    }

    [[nodiscard]] constexpr ptr end_ptr(void) noexcept {
        return unchecked_next(begin_ptr(), get_size());
    }

    [[nodiscard]] constexpr cptr end_ptr(void) const noexcept {
        return unchecked_next(begin_ptr(), get_size());
    }

    [[nodiscard]] constexpr ptr allocation_end_ptr(void) noexcept {
        return unchecked_next(begin_ptr(), get_capacity());
    }

    [[nodiscard]] constexpr cptr allocation_end_ptr(void) const noexcept {
        return unchecked_next(begin_ptr(), get_capacity());
    }

    [[nodiscard]] constexpr ptr storage_ptr(void) noexcept {
        if(std::is_constant_evaluated())
            return nullptr;
        return m_data.storage();
    }

    [[nodiscard]] constexpr bool has_allocation(void) const noexcept {
        if(std::is_constant_evaluated())
            return true;
        return InlineCapacity < get_capacity();
    }

    [[nodiscard]] constexpr bool is_inlinable(void) const noexcept {
        return get_size() <= InlineCapacity;
    }

private:
    small_vector_data<ptr, size_type, value_ty, InlineCapacity> m_data;
};

}  // namespace detail

template <typename T, unsigned InlineCapacity>
class small_vector : private detail::small_vector_base<T, InlineCapacity> {
    using base = detail::small_vector_base<T, InlineCapacity>;

public:
    template <typename SameT, unsigned DifferentInlineCapacity>
    friend class small_vector;

    using value_type = T;
    using size_type = typename base::size_type;
    using difference_type = typename base::difference_type;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = T*;
    using const_pointer = const T*;

    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static_assert(InlineCapacity <= (std::numeric_limits<size_type>::max)(),
                  "InlineCapacity must be less than or equal to the maximum value of size_type.");

    constexpr static unsigned inline_capacity_v = InlineCapacity;

private:
    constexpr static bool Destructible = concepts::small_vector::Destructible<value_type>;

    constexpr static bool MoveAssignable = concepts::small_vector::MoveAssignable<value_type>;

    constexpr static bool CopyAssignable = concepts::small_vector::CopyAssignable<value_type>;

    constexpr static bool MoveConstructible = concepts::small_vector::MoveConstructible<value_type>;

    constexpr static bool CopyConstructible = concepts::small_vector::CopyConstructible<value_type>;

    constexpr static bool Swappable = concepts::small_vector::Swappable<value_type>;

    constexpr static bool DefaultInsertable = concepts::small_vector::DefaultInsertable<value_type>;

    constexpr static bool MoveInsertable = concepts::small_vector::MoveInsertable<value_type>;

    constexpr static bool CopyInsertable = concepts::small_vector::CopyInsertable<value_type>;

    constexpr static bool Erasable = concepts::small_vector::Erasable<value_type>;

    template <typename... Args>
    struct EmplaceConstructible {
        constexpr static bool value =
            concepts::small_vector::EmplaceConstructible<value_type, Args...>;
    };

public:
    constexpr small_vector(void) noexcept = default;

    constexpr small_vector(const small_vector& other)
        requires CopyInsertable
        : base(base::bypass, other) {}

    constexpr small_vector(small_vector&& other) noexcept(
        std::is_nothrow_move_constructible<value_type>::value || InlineCapacity == 0)
        requires MoveInsertable
        : base(base::bypass, std::move(other)) {}

    constexpr explicit small_vector(size_type count)
        requires DefaultInsertable
        : base(count) {}

    constexpr small_vector(size_type count, const_reference value)
        requires CopyInsertable
        : base(count, value) {}

    template <typename Generator>
        requires std::invocable<Generator&> &&
                 EmplaceConstructible<std::invoke_result_t<Generator&>>::value
    constexpr small_vector(size_type count, Generator g) : base(count, g) {}

    template <std::input_iterator InputIt>
        requires EmplaceConstructible<std::iter_reference_t<InputIt>>::value &&
                 (std::forward_iterator<InputIt> || MoveInsertable)
    constexpr small_vector(InputIt first, InputIt last) :
        base(first, last, typename std::iterator_traits<InputIt>::iterator_category{}) {}

    constexpr small_vector(std::initializer_list<value_type> init)
        requires EmplaceConstructible<const_reference>::value
        : small_vector(init.begin(), init.end()) {}

    template <unsigned I>
        requires CopyInsertable
    constexpr explicit small_vector(const small_vector<T, I>& other) : base(base::bypass, other) {}

    template <unsigned I>
        requires MoveInsertable
    constexpr explicit small_vector(small_vector<T, I>&& other) noexcept(
        std::is_nothrow_move_constructible<value_type>::value && I < InlineCapacity) :
        base(base::bypass, std::move(other)) {}

    constexpr ~small_vector(void)
        requires Erasable
    = default;

    constexpr small_vector& operator=(const small_vector& other)
        requires CopyInsertable && CopyAssignable
    {
        assign(other);
        return *this;
    }

    constexpr small_vector& operator=(small_vector&& other) noexcept(
        (std::is_nothrow_move_assignable<value_type>::value &&
         std::is_nothrow_move_constructible<value_type>::value) ||
        InlineCapacity == 0)
        requires MoveInsertable && MoveAssignable
    {
        assign(std::move(other));
        return *this;
    }

    constexpr small_vector& operator=(std::initializer_list<value_type> ilist)
        requires CopyInsertable && CopyAssignable
    {
        assign(ilist);
        return *this;
    }

    constexpr void assign(size_type count, const_reference value)
        requires CopyInsertable && CopyAssignable
    {
        base::assign_with_copies(count, value);
    }

    template <std::input_iterator InputIt>
        requires EmplaceConstructible<std::iter_reference_t<InputIt>>::value &&
                 (std::forward_iterator<InputIt> || MoveInsertable)
    constexpr void assign(InputIt first, InputIt last) {
        using iterator_cat = typename std::iterator_traits<InputIt>::iterator_category;
        base::assign_with_range(first, last, iterator_cat{});
    }

    constexpr void assign(std::initializer_list<value_type> ilist)
        requires EmplaceConstructible<const_reference>::value
    {
        assign(ilist.begin(), ilist.end());
    }

    constexpr void assign(const small_vector& other)
        requires CopyInsertable && CopyAssignable
    {
        if(&other != this)
            base::copy_assign(other);
    }

    template <unsigned I>
        requires CopyInsertable && CopyAssignable
    constexpr void assign(const small_vector<T, I>& other) {
        base::copy_assign(other);
    }

    constexpr void assign(small_vector&& other) noexcept(
        (std::is_nothrow_move_assignable<value_type>::value &&
         std::is_nothrow_move_constructible<value_type>::value) ||
        InlineCapacity == 0)
        requires MoveInsertable && MoveAssignable
    {
        if(&other != this)
            base::move_assign(other);
    }

    template <unsigned I>
        requires MoveInsertable && MoveAssignable
    constexpr void assign(small_vector<T, I>&& other) noexcept(
        I <= InlineCapacity && std::is_nothrow_move_assignable<value_type>::value &&
        std::is_nothrow_move_constructible<value_type>::value) {
        base::move_assign(other);
    }

    constexpr void
        swap(small_vector& other) noexcept((std::is_nothrow_move_constructible<value_type>::value &&
                                            std::is_nothrow_move_assignable<value_type>::value &&
                                            std::is_nothrow_swappable<value_type>::value) ||
                                           InlineCapacity == 0)
        requires ((MoveInsertable && MoveAssignable && Swappable) || InlineCapacity == 0)
    {
        base::swap(other);
    }

    template <unsigned I>
    constexpr void swap(small_vector<T, I>& other)
        requires (MoveInsertable && MoveAssignable && Swappable)
    {
        base::swap(other);
    }

    constexpr iterator begin(void) noexcept {
        return base::begin_ptr();
    }

    constexpr const_iterator begin(void) const noexcept {
        return base::begin_ptr();
    }

    constexpr const_iterator cbegin(void) const noexcept {
        return begin();
    }

    constexpr iterator end(void) noexcept {
        return base::end_ptr();
    }

    constexpr const_iterator end(void) const noexcept {
        return base::end_ptr();
    }

    constexpr const_iterator cend(void) const noexcept {
        return end();
    }

    constexpr reverse_iterator rbegin(void) noexcept {
        return reverse_iterator{end()};
    }

    constexpr const_reverse_iterator rbegin(void) const noexcept {
        return const_reverse_iterator{end()};
    }

    constexpr const_reverse_iterator crbegin(void) const noexcept {
        return rbegin();
    }

    constexpr reverse_iterator rend(void) noexcept {
        return reverse_iterator{begin()};
    }

    constexpr const_reverse_iterator rend(void) const noexcept {
        return const_reverse_iterator{begin()};
    }

    constexpr const_reverse_iterator crend(void) const noexcept {
        return rend();
    }

    constexpr reference at(size_type pos) {
        if(size() <= pos)
            base::throw_index_error();
        return begin()[static_cast<difference_type>(pos)];
    }

    constexpr const_reference at(size_type pos) const {
        if(size() <= pos)
            base::throw_index_error();
        return begin()[static_cast<difference_type>(pos)];
    }

    constexpr reference operator[](size_type pos) {
        return begin()[static_cast<difference_type>(pos)];
    }

    constexpr const_reference operator[](size_type pos) const {
        return begin()[static_cast<difference_type>(pos)];
    }

    constexpr reference front(void) {
        return (*this)[0];
    }

    constexpr const_reference front(void) const {
        return (*this)[0];
    }

    constexpr reference back(void) {
        return (*this)[size() - 1];
    }

    constexpr const_reference back(void) const {
        return (*this)[size() - 1];
    }

    constexpr pointer data(void) noexcept {
        return base::begin_ptr();
    }

    constexpr const_pointer data(void) const noexcept {
        return base::begin_ptr();
    }

    constexpr size_type size(void) const noexcept {
        return static_cast<size_type>(base::get_size());
    }

    [[nodiscard]] constexpr bool empty(void) const noexcept {
        return size() == 0;
    }

    constexpr size_type max_size(void) const noexcept {
        return static_cast<size_type>(base::get_max_size());
    }

    constexpr size_type capacity(void) const noexcept {
        return static_cast<size_type>(base::get_capacity());
    }

    constexpr iterator insert(const_iterator pos, const_reference value)
        requires CopyInsertable && CopyAssignable
    {
        return emplace(pos, value);
    }

    constexpr iterator insert(const_iterator pos, value_type&& value)
        requires MoveInsertable && MoveAssignable
    {
        return emplace(pos, std::move(value));
    }

    constexpr iterator insert(const_iterator pos, size_type count, const_reference value)
        requires CopyInsertable && CopyAssignable
    {
        return base::insert_copies(base::ptr_cast(pos), count, value);
    }

    template <std::input_iterator InputIt>
        requires EmplaceConstructible<std::iter_reference_t<InputIt>>::value && MoveInsertable &&
                 MoveAssignable
    constexpr iterator insert(const_iterator pos, InputIt first, InputIt last) {
        if(first == last)
            return base::ptr_cast(pos);

        using iterator_cat = typename std::iterator_traits<InputIt>::iterator_category;
        return base::insert_range(base::ptr_cast(pos), first, last, iterator_cat{});
    }

    constexpr iterator insert(const_iterator pos, std::initializer_list<value_type> ilist)
        requires EmplaceConstructible<const_reference>::value && MoveInsertable && MoveAssignable
    {
        return insert(pos, ilist.begin(), ilist.end());
    }

    template <typename... Args>
        requires EmplaceConstructible<Args...>::value && MoveInsertable && MoveAssignable
    constexpr iterator emplace(const_iterator pos, Args&&... args) {
        return base::emplace_at(base::ptr_cast(pos), std::forward<Args>(args)...);
    }

    constexpr iterator erase(const_iterator pos)
        requires MoveAssignable && Erasable
    {
        assert(0 <= (pos - begin()) && "`pos` is out of bounds (before `begin ()`).");
        assert(0 < (end() - pos) && "`pos` is out of bounds (at or after `end ()`).");

        return base::erase_at(base::ptr_cast(pos));
    }

    constexpr iterator erase(const_iterator first, const_iterator last)
        requires MoveAssignable && Erasable
    {
        assert(0 <= (last - first) && "Invalid range.");
        assert(0 <= (first - begin()) && "`first` is out of bounds (before `begin ()`).");
        assert(0 <= (end() - last) && "`last` is out of bounds (after `end ()`).");

        return base::erase_range(base::ptr_cast(first), base::ptr_cast(last));
    }

    constexpr void push_back(const_reference value)
        requires CopyInsertable
    {
        emplace_back(value);
    }

    constexpr void push_back(value_type&& value)
        requires MoveInsertable
    {
        emplace_back(std::move(value));
    }

    template <typename... Args>
        requires EmplaceConstructible<Args...>::value && MoveInsertable
    constexpr reference emplace_back(Args&&... args) {
        return *base::append_element(std::forward<Args>(args)...);
    }

    constexpr void pop_back(void)
        requires Erasable
    {
        assert(!empty() && "`pop_back ()` called on an empty `small_vector`.");
        base::erase_last();
    }

    constexpr void reserve(size_type new_capacity)
        requires MoveInsertable
    {
        base::request_capacity(new_capacity);
    }

    constexpr void shrink_to_fit(void)
        requires MoveInsertable
    {
        base::shrink_to_size();
    }

    constexpr void clear(void) noexcept
        requires Erasable
    {
        base::erase_all();
    }

    constexpr void resize(size_type count)
        requires MoveInsertable && DefaultInsertable
    {
        base::resize_with(count);
    }

    constexpr void resize(size_type count, const_reference value)
        requires CopyInsertable
    {
        base::resize_with(count, value);
    }

    [[nodiscard]] constexpr bool inlined(void) const noexcept {
        return !base::has_allocation();
    }

    [[nodiscard]] constexpr bool inlinable(void) const noexcept {
        return base::is_inlinable();
    }

    [[nodiscard]]
    static consteval size_type inline_capacity(void) noexcept {
        return static_cast<size_type>(inline_capacity_v);
    }

    template <std::input_iterator InputIt>
        requires EmplaceConstructible<std::iter_reference_t<InputIt>>::value && MoveInsertable
    constexpr small_vector& append(InputIt first, InputIt last) {
        using policy = typename base::strong_exception_policy;
        using iterator_cat = typename std::iterator_traits<InputIt>::iterator_category;
        base::template append_range<policy>(first, last, iterator_cat{});
        return *this;
    }

    constexpr small_vector& append(std::initializer_list<value_type> ilist)
        requires EmplaceConstructible<const_reference>::value && MoveInsertable
    {
        return append(ilist.begin(), ilist.end());
    }

    template <unsigned I>
    constexpr small_vector& append(const small_vector<T, I>& other)
        requires CopyInsertable
    {
        return append(other.begin(), other.end());
    }

    template <unsigned I>
    constexpr small_vector& append(small_vector<T, I>&& other)
        requires MoveInsertable
    {
        // Provide a strong exception guarantee for `other` as well.
        using move_iter_type =
            typename std::conditional<base::template relocate_with_move<value_type>::value,
                                      std::move_iterator<iterator>,
                                      iterator>::type;

        append(move_iter_type{other.begin()}, move_iter_type{other.end()});
        other.clear();
        return *this;
    }
};

template <typename T, unsigned InlineCapacityLHS, unsigned InlineCapacityRHS>
constexpr inline bool operator==(const small_vector<T, InlineCapacityLHS>& lhs,
                                 const small_vector<T, InlineCapacityRHS>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

template <typename T, unsigned InlineCapacity>
constexpr inline bool operator==(const small_vector<T, InlineCapacity>& lhs,
                                 const small_vector<T, InlineCapacity>& rhs) {
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

template <typename T, unsigned InlineCapacityLHS, unsigned InlineCapacityRHS>
    requires std::three_way_comparable<T>
constexpr auto operator<=>(const small_vector<T, InlineCapacityLHS>& lhs,
                           const small_vector<T, InlineCapacityRHS>& rhs) {
    return std::lexicographical_compare_three_way(lhs.begin(),
                                                  lhs.end(),
                                                  rhs.begin(),
                                                  rhs.end(),
                                                  std::compare_three_way{});
}

template <typename T, unsigned InlineCapacity>
    requires std::three_way_comparable<T>
constexpr auto operator<=>(const small_vector<T, InlineCapacity>& lhs,
                           const small_vector<T, InlineCapacity>& rhs) {
    return std::lexicographical_compare_three_way(lhs.begin(),
                                                  lhs.end(),
                                                  rhs.begin(),
                                                  rhs.end(),
                                                  std::compare_three_way{});
}

template <typename T, unsigned InlineCapacityLHS, unsigned InlineCapacityRHS>
constexpr auto operator<=>(const small_vector<T, InlineCapacityLHS>& lhs,
                           const small_vector<T, InlineCapacityRHS>& rhs) {
    constexpr auto comparison = [](const T& l, const T& r) {
        return (l < r)   ? std::weak_ordering::less
               : (r < l) ? std::weak_ordering::greater
                         : std::weak_ordering::equivalent;
    };

    return std::lexicographical_compare_three_way(lhs.begin(),
                                                  lhs.end(),
                                                  rhs.begin(),
                                                  rhs.end(),
                                                  comparison);
}

template <typename T, unsigned InlineCapacity>
constexpr auto operator<=>(const small_vector<T, InlineCapacity>& lhs,
                           const small_vector<T, InlineCapacity>& rhs) {
    constexpr auto comparison = [](const T& l, const T& r) {
        return (l < r)   ? std::weak_ordering::less
               : (r < l) ? std::weak_ordering::greater
                         : std::weak_ordering::equivalent;
    };

    return std::lexicographical_compare_three_way(lhs.begin(),
                                                  lhs.end(),
                                                  rhs.begin(),
                                                  rhs.end(),
                                                  comparison);
}

template <typename T, unsigned InlineCapacity>
constexpr inline void swap(small_vector<T, InlineCapacity>& lhs,
                           small_vector<T, InlineCapacity>& rhs) noexcept(noexcept(lhs.swap(rhs)))
    requires concepts::small_vector::MoveInsertable<T> &&
             concepts::small_vector::MoveAssignable<T> && concepts::small_vector::Swappable<T>
{
    lhs.swap(rhs);
}

template <typename T, unsigned InlineCapacityLHS, unsigned InlineCapacityRHS>
constexpr inline void
    swap(small_vector<T, InlineCapacityLHS>& lhs,
         small_vector<T, InlineCapacityRHS>& rhs) noexcept(noexcept(lhs.swap(rhs)))
    requires concepts::small_vector::MoveInsertable<T> &&
             concepts::small_vector::MoveAssignable<T> && concepts::small_vector::Swappable<T>
{
    lhs.swap(rhs);
}

template <typename T, unsigned InlineCapacity, typename U>
constexpr inline typename small_vector<T, InlineCapacity>::size_type
    erase(small_vector<T, InlineCapacity>& v, const U& value) {
    const auto original_size = v.size();
    v.erase(std::remove(v.begin(), v.end(), value), v.end());
    return original_size - v.size();
}

template <typename T, unsigned InlineCapacity, typename Pred>
constexpr inline typename small_vector<T, InlineCapacity>::size_type
    erase_if(small_vector<T, InlineCapacity>& v, Pred pred) {
    const auto original_size = v.size();
    v.erase(std::remove_if(v.begin(), v.end(), pred), v.end());
    return original_size - v.size();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::iterator
    begin(small_vector<T, InlineCapacity>& v) noexcept {
    return v.begin();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::const_iterator
    begin(const small_vector<T, InlineCapacity>& v) noexcept {
    return v.begin();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::const_iterator
    cbegin(const small_vector<T, InlineCapacity>& v) noexcept {
    return begin(v);
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::iterator
    end(small_vector<T, InlineCapacity>& v) noexcept {
    return v.end();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::const_iterator
    end(const small_vector<T, InlineCapacity>& v) noexcept {
    return v.end();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::const_iterator
    cend(const small_vector<T, InlineCapacity>& v) noexcept {
    return end(v);
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::reverse_iterator
    rbegin(small_vector<T, InlineCapacity>& v) noexcept {
    return v.rbegin();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::const_reverse_iterator
    rbegin(const small_vector<T, InlineCapacity>& v) noexcept {
    return v.rbegin();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::const_reverse_iterator
    crbegin(const small_vector<T, InlineCapacity>& v) noexcept {
    return rbegin(v);
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::reverse_iterator
    rend(small_vector<T, InlineCapacity>& v) noexcept {
    return v.rend();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::const_reverse_iterator
    rend(const small_vector<T, InlineCapacity>& v) noexcept {
    return v.rend();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::const_reverse_iterator
    crend(const small_vector<T, InlineCapacity>& v) noexcept {
    return rend(v);
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::size_type
    size(const small_vector<T, InlineCapacity>& v) noexcept {
    return v.size();
}

template <typename T, unsigned InlineCapacity>
constexpr typename std::common_type<
    std::ptrdiff_t,
    typename std::make_signed<typename small_vector<T, InlineCapacity>::size_type>::type>::type
    ssize(const small_vector<T, InlineCapacity>& v) noexcept {
    using ret_type =
        typename std::common_type<std::ptrdiff_t,
                                  typename std::make_signed<decltype(v.size())>::type>::type;
    return static_cast<ret_type>(v.size());
}

template <typename T, unsigned InlineCapacity>
[[nodiscard]] constexpr bool empty(const small_vector<T, InlineCapacity>& v) noexcept {
    return v.empty();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::pointer
    data(small_vector<T, InlineCapacity>& v) noexcept {
    return v.data();
}

template <typename T, unsigned InlineCapacity>
constexpr typename small_vector<T, InlineCapacity>::const_pointer
    data(const small_vector<T, InlineCapacity>& v) noexcept {
    return v.data();
}

template <typename InputIt,
          unsigned InlineCapacity =
              default_buffer_size<typename std::iterator_traits<InputIt>::value_type>::value>
small_vector(InputIt, InputIt)
    -> small_vector<typename std::iterator_traits<InputIt>::value_type, InlineCapacity>;

}  // namespace eventide
