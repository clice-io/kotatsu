#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "kota/support/config.h"
#include "kota/support/memory.h"

namespace kota {

template <typename T>
class hybrid_vector;

template <typename T, unsigned int InlineCapacity>
class small_vector;

namespace detail {

template <typename T>
constexpr inline bool is_small_vector_v = false;

template <typename T>
constexpr inline bool is_small_vector_v<hybrid_vector<T>> = true;

template <typename T, unsigned int InlineCapacity>
constexpr inline bool is_small_vector_v<small_vector<T, InlineCapacity>> = true;

template <typename Range, typename T>
concept small_vector_compatible_range =
    std::ranges::input_range<Range> &&
    std::constructible_from<T, std::ranges::range_reference_t<Range>> &&
    !is_small_vector_v<std::remove_cvref_t<Range>>;

/// The integer a vector keeps its size and capacity in: 32 bits, or a word for elements
/// under 4 bytes, of which an address space holds more than 32 bits count.
template <typename T>
using small_vector_size_type =
    std::conditional_t<sizeof(T) < 4 && sizeof(void*) >= 8, std::uint64_t, std::uint32_t>;

template <typename T>
struct default_buffer_size {
    constexpr static std::size_t preferred_size = 64;

    static_assert(sizeof(T) <= 256,
                  "Default small_vector inline buffer would be too large. "
                  "Use small_vector<T, N> with an explicit inline capacity.");

    constexpr static std::size_t inline_bytes = preferred_size > sizeof(small_vector<T, 0>)
                                                    ? preferred_size - sizeof(small_vector<T, 0>)
                                                    : 0;
    constexpr static std::size_t value =
        inline_bytes / sizeof(T) == 0 ? 1 : inline_bytes / sizeof(T);
};

struct synth_three_way {
    template <typename LHS, typename RHS>
    [[nodiscard]]
    constexpr auto operator()(const LHS& lhs, const RHS& rhs) const
        requires requires { lhs <=> rhs; } {
        return lhs <=> rhs;
    }

    template <typename LHS, typename RHS>
    [[nodiscard]]
    constexpr std::weak_ordering operator()(const LHS& lhs, const RHS& rhs) const
        requires (
            !requires { lhs <=> rhs; } &&
            requires {
                { lhs < rhs } -> std::convertible_to<bool>;
                { rhs < lhs } -> std::convertible_to<bool>;
            }) {
        if(lhs < rhs) {
            return std::weak_ordering::less;
        }
        if(rhs < lhs) {
            return std::weak_ordering::greater;
        }
        return std::weak_ordering::equivalent;
    }
};

template <typename T, unsigned int InlineCapacity>
struct inline_buffer {
protected:
    alignas(T) std::byte buffer[InlineCapacity * sizeof(T)];
};

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // empty inline buffer intentionally carries T's alignment
#endif
template <typename T>
struct alignas(T) inline_buffer<T, 0> {};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace detail

/// A small_vector of any inline capacity, for code that takes one by reference without
/// caring what that capacity is. A small_vector places its inline buffer right after this
/// header, where inline_begin() finds it.
///
/// An argument may view the vector's own elements: an element to copy, or a range of them
/// to append, assign or insert.
template <typename T>
class hybrid_vector {
    template <typename, unsigned int>
    friend class small_vector;

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    hybrid_vector(const hybrid_vector&) = delete;

    /// Copies `other`'s elements, reusing this vector's elements and buffer where they suffice.
    constexpr hybrid_vector& operator=(const hybrid_vector& other) {
        if(!same_object(other)) {
            assign_items(other.begin(), other.size());
        }
        return *this;
    }

    /// Takes `other`'s allocation, or moves its elements when they are inline, and leaves it
    /// empty. Through this type `other`'s inline capacity is unknown, so taking its
    /// allocation leaves it a capacity of 0 until it grows again.
    constexpr hybrid_vector& operator=(hybrid_vector&& other) {
        if(!same_object(other) && !take_allocation(other)) {
            assign_items(std::make_move_iterator(other.begin()), other.size());
            other.clear();
        }
        return *this;
    }

    constexpr ~hybrid_vector() {
        destroy_elements();
        free_allocation();
    }

    [[nodiscard]] constexpr iterator begin() noexcept {
        return head;
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return head;
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return begin();
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        return head + used;
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        return head + used;
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return end();
    }

    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
        return reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
        return const_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
        return const_reverse_iterator(end());
    }

    [[nodiscard]] constexpr reverse_iterator rend() noexcept {
        return reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
        return const_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
        return const_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr pointer data() noexcept {
        return head;
    }

    [[nodiscard]] constexpr const_pointer data() const noexcept {
        return head;
    }

    [[nodiscard]] constexpr size_type size() const noexcept {
        return used;
    }

    [[nodiscard]] constexpr size_type size_in_bytes() const noexcept {
        return size() * sizeof(value_type);
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return used == 0;
    }

    [[nodiscard]] constexpr size_type capacity() const noexcept {
        return room;
    }

    [[nodiscard]] constexpr size_type capacity_in_bytes() const noexcept {
        return capacity() * sizeof(value_type);
    }

    /// Whether the elements are in the inline buffer; never in constant evaluation, where
    /// every buffer is allocated.
    [[nodiscard]] constexpr bool inlined() const noexcept {
        if consteval {
            return false;
        } else {
            return !on_heap();
        }
    }

    [[nodiscard]] constexpr size_type max_size() const noexcept {
        return (std::min)(static_cast<size_type>((std::numeric_limits<compact_size_type>::max)()),
                          (std::numeric_limits<size_type>::max)() / sizeof(value_type));
    }

    [[nodiscard]] constexpr reference operator[](size_type idx) noexcept {
        assert(idx < size());
        return head[idx];
    }

    [[nodiscard]] constexpr const_reference operator[](size_type idx) const noexcept {
        assert(idx < size());
        return head[idx];
    }

    constexpr reference at(size_type idx) {
        if(idx >= size()) {
            KOTA_THROW(std::out_of_range("small_vector index out of range"));
        }
        return head[idx];
    }

    constexpr const_reference at(size_type idx) const {
        if(idx >= size()) {
            KOTA_THROW(std::out_of_range("small_vector index out of range"));
        }
        return head[idx];
    }

    [[nodiscard]] constexpr reference front() noexcept {
        assert(!empty());
        return head[0];
    }

    [[nodiscard]] constexpr const_reference front() const noexcept {
        assert(!empty());
        return head[0];
    }

    [[nodiscard]] constexpr reference back() noexcept {
        assert(!empty());
        return head[used - 1];
    }

    [[nodiscard]] constexpr const_reference back() const noexcept {
        assert(!empty());
        return head[used - 1];
    }

    constexpr void clear() noexcept {
        destroy_elements();
    }

    constexpr void reserve(size_type new_capacity) {
        if(new_capacity > capacity()) {
            rebuild(next_capacity(new_capacity), size(), [this](pointer first) {
                return mem::uninitialized_move(begin(), end(), first);
            });
        }
    }

    constexpr void resize(size_type count) {
        if(count <= size()) {
            truncate(count);
            return;
        }
        reserve(count);
        mem::uninitialized_value_construct(end(), head + count);
        set_size(count);
    }

    /// Like resize(count), but default-initializes the new elements.
    constexpr void resize_for_overwrite(size_type count) {
        if(count <= size()) {
            truncate(count);
            return;
        }
        reserve(count);
        mem::uninitialized_default_construct(end(), head + count);
        set_size(count);
    }

    constexpr void resize(size_type count, const_reference value) {
        if(count <= size()) {
            truncate(count);
            return;
        }
        append(count - size(), value);
    }

    constexpr void truncate(size_type count) {
        assert(count <= size());
        std::ranges::destroy(head + count, end());
        set_size(count);
    }

    constexpr void push_back(const_reference value) {
        emplace_back(value);
    }

    constexpr void push_back(value_type&& value) {
        emplace_back(std::move(value));
    }

    template <typename... Args>
    constexpr reference emplace_back(Args&&... args) {
        append_with(1, [&](pointer tail) { std::construct_at(tail, std::forward<Args>(args)...); });
        return back();
    }

    constexpr void pop_back() noexcept {
        assert(!empty());
        set_size(size() - 1);
        std::destroy_at(end());
    }

    constexpr void pop_back_n(size_type count) noexcept {
        assert(count <= size());
        truncate(size() - count);
    }

    [[nodiscard]] constexpr value_type pop_back_val() {
        value_type result = std::move(back());
        pop_back();
        return result;
    }

    constexpr void append(size_type count, const_reference value) {
        append_with(count,
                    [&](pointer tail) { mem::uninitialized_fill(tail, tail + count, value); });
    }

    template <detail::small_vector_compatible_range<value_type> Range>
    constexpr void append(Range&& range) {
        if constexpr(std::ranges::forward_range<Range>) {
            append_with(mem::range_length(range), [&](pointer tail) {
                mem::uninitialized_copy(std::forward<Range>(range), tail);
            });
        } else {
            for(auto&& value: range) {
                emplace_back(std::forward<decltype(value)>(value));
            }
        }
    }

    constexpr void append(std::initializer_list<value_type> init) {
        append(std::ranges::subrange(init.begin(), init.end()));
    }

    constexpr void append(const hybrid_vector& other) {
        append(std::ranges::subrange(other.begin(), other.end()));
    }

    /// Moves `other`'s elements to the end and leaves it empty; appending a vector to itself
    /// this way does nothing.
    constexpr void append(hybrid_vector&& other) {
        if(same_object(other)) {
            return;
        }
        append(mem::move_range(other.begin(), other.end()));
        other.clear();
    }

    constexpr void assign(size_type count, const_reference value) {
        if(count > capacity()) {
            // The copies are made in the new allocation while `value`, which may be an
            // element, is still in place.
            rebuild(next_capacity(count), count, [&](pointer first) {
                return mem::uninitialized_fill(first, first + count, value);
            });
            return;
        }
        std::ranges::fill_n(begin(), (std::min)(count, size()), value);
        if(count > size()) {
            mem::uninitialized_fill(end(), head + count, value);
        } else {
            std::ranges::destroy(head + count, end());
        }
        set_size(count);
    }

    template <detail::small_vector_compatible_range<value_type> Range>
    constexpr void assign(Range&& range) {
        if constexpr(std::ranges::forward_range<Range>) {
            if(range_references_elements(range)) {
                // Clearing destroys what the range views: copy it out first.
                const auto count = mem::range_length(range);
                mem::AllocationGuard<value_type> copies(mem::allocate<value_type>(count), count);
                copies.mark(mem::uninitialized_copy(range, copies.data()));
                destroy_elements();
                append(mem::move_range(copies.data(), copies.data() + count));
                return;
            }
        }
        destroy_elements();
        append(std::forward<Range>(range));
    }

    constexpr void assign(std::initializer_list<value_type> init) {
        destroy_elements();
        append(init);
    }

    constexpr void assign(const hybrid_vector& other) {
        *this = other;
    }

    constexpr void assign(hybrid_vector&& other) {
        *this = std::move(other);
    }

    constexpr iterator insert(iterator pos, const_reference value) {
        return emplace(pos, value);
    }

    constexpr iterator insert(iterator pos, value_type&& value) {
        assert(valid_insert_position(pos));
        if(references_elements(std::addressof(value))) {
            return emplace(pos, std::move(value));
        }
        return insert_one(index_of(pos), std::move(value));
    }

    constexpr iterator insert(iterator pos, size_type count, const_reference value) {
        assert(valid_insert_position(pos));
        if(count == 0) {
            return pos;
        }
        // The insertion moves the elements, and `value` may be one of them.
        const value_type copy(value);
        return insert_items(
            index_of(pos),
            count,
            [&](pointer dest, size_type from) {
                return mem::uninitialized_fill(dest, dest + (count - from), copy);
            },
            [&](pointer dest, size_type n) { std::fill_n(dest, n, copy); });
    }

    template <detail::small_vector_compatible_range<value_type> Range>
    constexpr iterator insert(iterator pos, Range&& range) {
        assert(valid_insert_position(pos));
        const auto index = index_of(pos);
        if constexpr(std::ranges::forward_range<Range>) {
            const auto count = mem::range_length(range);
            if(!range_references_elements(range)) {
                return insert_from(index, count, std::ranges::begin(range));
            }
            // The insertion moves the elements the range views: copy them out first.
            mem::AllocationGuard<value_type> copies(mem::allocate<value_type>(count), count);
            copies.mark(mem::uninitialized_copy(range, copies.data()));
            return insert_from(index, count, std::make_move_iterator(copies.data()));
        } else {
            // An input range is read once: each element is inserted as it comes.
            auto at = index;
            for(auto&& value: range) {
                insert(head + at, std::forward<decltype(value)>(value));
                ++at;
            }
            return head + index;
        }
    }

    constexpr iterator insert(iterator pos, std::initializer_list<value_type> init) {
        return insert(pos, std::ranges::subrange(init.begin(), init.end()));
    }

    template <typename... Args>
    constexpr iterator emplace(iterator pos, Args&&... args) {
        assert(valid_insert_position(pos));
        // The arguments may name elements the insertion moves.
        value_type value(std::forward<Args>(args)...);
        return insert_one(index_of(pos), std::move(value));
    }

    constexpr iterator erase(const_iterator pos) {
        assert(valid_erase_range(pos, std::next(pos)));
        return erase(pos, pos + 1);
    }

    constexpr iterator erase(const_iterator first, const_iterator last) {
        assert(valid_erase_range(first, last));
        auto* erase_begin = head + index_of(first);
        auto* erase_end = head + index_of(last);
        if(erase_begin != erase_end) {
            auto* new_end = std::ranges::move(erase_end, end(), erase_begin).out;
            truncate(index_of(new_end));
        }
        return erase_begin;
    }

    constexpr void swap(hybrid_vector& other) {
        if(same_object(other)) {
            return;
        }
        if(on_heap() && other.on_heap() && head != other.inline_begin() &&
           other.head != inline_begin()) {
            std::swap(head, other.head);
            std::swap(used, other.used);
            std::swap(room, other.room);
            return;
        }
        reserve(other.size());
        other.reserve(size());
        const auto shared = (std::min)(size(), other.size());
        std::swap_ranges(begin(), begin() + shared, other.begin());
        auto& longer = size() > other.size() ? *this : other;
        auto& shorter = size() > other.size() ? other : *this;
        mem::uninitialized_move(longer.begin() + shared, longer.end(), shorter.end());
        shorter.set_size(longer.size());
        longer.truncate(shared);
    }

    friend constexpr bool operator==(const hybrid_vector& lhs, const hybrid_vector& rhs) {
        return std::ranges::equal(lhs, rhs);
    }

    friend constexpr auto operator<=>(const hybrid_vector& lhs, const hybrid_vector& rhs) {
        return std::lexicographical_compare_three_way(lhs.begin(),
                                                      lhs.end(),
                                                      rhs.begin(),
                                                      rhs.end(),
                                                      detail::synth_three_way{});
    }

protected:
    constexpr explicit hybrid_vector(size_type inline_capacity) noexcept : head(nullptr), room(0) {
        reset_to_small(inline_capacity);
    }

    /// The inline buffer; null in constant evaluation, where every buffer is allocated.
    [[nodiscard]] constexpr pointer inline_begin() noexcept {
        if consteval {
            return nullptr;
        } else {
            return reinterpret_cast<pointer>(reinterpret_cast<std::byte*>(this) +
                                             offsetof(alignment_and_size, first_element));
        }
    }

    [[nodiscard]] constexpr const_pointer inline_begin() const noexcept {
        if consteval {
            return nullptr;
        } else {
            return reinterpret_cast<const_pointer>(reinterpret_cast<const std::byte*>(this) +
                                                   offsetof(alignment_and_size, first_element));
        }
    }

    /// Makes the empty inline buffer, of `inline_capacity` elements, the vector's buffer,
    /// forgetting any allocation.
    constexpr void reset_to_small(size_type inline_capacity) noexcept {
        head = inline_begin();
        used = 0;
        if consteval {
            room = 0;
        } else {
            room = static_cast<compact_size_type>(inline_capacity);
        }
    }

    [[nodiscard]] constexpr bool on_heap() const noexcept {
        return head != inline_begin();
    }

    constexpr void set_size(size_type count) noexcept {
        assert(count <= capacity());
        used = static_cast<compact_size_type>(count);
    }

    /// Makes `data` the vector's buffer, holding `count` elements with room for `capacity`:
    /// an allocation from allocate() or mem::allocate<value_type>, or the inline buffer. The
    /// old elements are destroyed and the old allocation is freed.
    constexpr void adopt_allocation(pointer data, size_type count, size_type capacity) noexcept {
        assert(count <= capacity);
        destroy_elements();
        free_allocation();
        head = data;
        used = static_cast<compact_size_type>(count);
        room = static_cast<compact_size_type>(capacity);
    }

    /// Moves `other`'s elements into this empty vector, constructing rather than assigning
    /// them, and leaves `other` empty: by taking its allocation when it has one, as operator=
    /// does.
    constexpr void take_elements(hybrid_vector& other) {
        if(!take_allocation(other)) {
            append(mem::move_range(other.begin(), other.end()));
            other.clear();
        }
    }

    /// An allocation for `capacity` elements. An inline buffer of no elements ends the
    /// object, and an allocator can hand out the address right after it, which would read as
    /// the inline buffer and so never be freed: such an allocation is traded for another. A
    /// move or a swap does not take an allocation from another vector that starts there
    /// either, and moves the elements instead.
    [[nodiscard]] constexpr pointer allocate(size_type capacity) {
        auto* allocation = mem::allocate<value_type>(capacity);
        if(allocation != inline_begin()) [[likely]] {
            return allocation;
        }
        auto* other = mem::allocate<value_type>(capacity);
        mem::deallocate(allocation, capacity);
        return other;
    }

    /// Replaces the buffer with an allocation of room for `new_room`, in which `fill(first)`
    /// constructs `count` elements, which may read the old ones, and returns their end.
    template <typename Fill>
    constexpr void rebuild(size_type new_room, size_type count, Fill fill) {
        mem::AllocationGuard<value_type> guard(allocate(new_room), new_room);
        guard.mark(fill(guard.data()));
        adopt_allocation(guard.release(), count, new_room);
    }

    /// Adds `count` elements, which `construct(tail)` builds from `tail` on. It may read the
    /// elements: when the vector grows, it builds the new ones in the new allocation while the
    /// old ones are still in place, and moves those after.
    template <typename Construct>
    constexpr void append_with(size_type count, Construct construct) {
        const auto new_size = checked_size(size(), count);
        if(new_size <= capacity()) {
            construct(end());
            set_size(new_size);
            return;
        }
        const auto new_room = next_capacity(new_size);
        auto* new_head = allocate(new_room);
        auto* tail = new_head + size();
        KOTA_TRY {
            construct(tail);
        }
        KOTA_CATCH_ALL() {
            mem::deallocate(new_head, new_room);
            KOTA_RETHROW();
        }
        KOTA_TRY {
            mem::uninitialized_move(begin(), end(), new_head);
        }
        KOTA_CATCH_ALL() {
            std::ranges::destroy(tail, new_head + new_size);
            mem::deallocate(new_head, new_room);
            KOTA_RETHROW();
        }
        adopt_allocation(new_head, new_size, new_room);
    }

private:
    using compact_size_type = detail::small_vector_size_type<value_type>;

    constexpr static std::size_t header_alignment = alignof(pointer) > alignof(compact_size_type)
                                                        ? alignof(pointer)
                                                        : alignof(compact_size_type);

    struct alignment_and_size {
        alignas(header_alignment) std::byte header[sizeof(pointer) + 2 * sizeof(compact_size_type)];
        alignas(value_type) std::byte first_element[sizeof(value_type)];
    };

    /// The first element, in the inline buffer or an allocation.
    pointer head;
    compact_size_type used = 0;
    /// How many elements the buffer has room for.
    compact_size_type room;

    [[nodiscard]] constexpr bool same_object(const hybrid_vector& other) const noexcept {
        return this == std::addressof(other);
    }

    [[nodiscard]] constexpr size_type index_of(const_iterator pos) const noexcept {
        return static_cast<size_type>(pos - begin());
    }

    [[nodiscard]] constexpr bool valid_insert_position(const_iterator pos) const noexcept {
        std::less<const_pointer> less;
        return !less(pos, begin()) && !less(end(), pos);
    }

    [[nodiscard]] constexpr bool valid_erase_range(const_iterator first,
                                                   const_iterator last) const noexcept {
        std::less<const_pointer> less;
        return !less(first, begin()) && !less(last, first) && !less(end(), last);
    }

    [[nodiscard]] constexpr bool references_elements(const_pointer ptr) const noexcept {
        if consteval {
            // Pointers into different objects do not order in constant evaluation.
            return std::ranges::any_of(*this, [ptr](const value_type& element) {
                return std::addressof(element) == ptr;
            });
        } else {
            return mem::pointer_in_range(ptr, begin(), end());
        }
    }

    template <std::ranges::forward_range Range>
    [[nodiscard]] constexpr bool range_references_elements(Range& range) const noexcept {
        using reference_type = std::ranges::range_reference_t<Range>;
        if constexpr(!std::is_reference_v<reference_type> ||
                     !std::same_as<std::remove_cvref_t<reference_type>, value_type>) {
            return false;
        } else if constexpr(std::ranges::contiguous_range<Range>) {
            // A contiguous range that views any element has its first or last one among them.
            const auto count = mem::range_length(range);
            const auto* first = std::ranges::data(range);
            return count != 0 &&
                   (references_elements(first) || references_elements(first + (count - 1)));
        } else {
            return std::ranges::any_of(range, [this](const value_type& element) {
                return references_elements(std::addressof(element));
            });
        }
    }

    [[nodiscard]] constexpr size_type checked_size(size_type base, size_type extra) const {
        if(extra > max_size() - base) {
            KOTA_THROW(std::length_error("small_vector capacity overflow"));
        }
        return base + extra;
    }

    /// The capacity to grow to for at least `min_capacity` elements: twice the current one,
    /// within max_size().
    [[nodiscard]] constexpr size_type next_capacity(size_type min_capacity) const {
        if(min_capacity > max_size()) {
            KOTA_THROW(std::length_error("small_vector capacity overflow"));
        }
        const auto doubled =
            capacity() > max_size() / 2 ? max_size() : (std::max)(2 * capacity(), size_type(1));
        return (std::max)(doubled, min_capacity);
    }

    constexpr void destroy_elements() noexcept {
        std::ranges::destroy(begin(), end());
        used = 0;
    }

    constexpr void free_allocation() noexcept {
        if(on_heap()) {
            mem::deallocate(head, room);
        }
    }

    /// Takes `other`'s allocation, and its elements with it, when it has one this vector can
    /// hold (see allocate()); this vector's elements and allocation go first. `other` is left
    /// with neither, and a capacity of 0.
    constexpr bool take_allocation(hybrid_vector& other) noexcept {
        if(!other.on_heap() || other.head == inline_begin()) {
            return false;
        }
        adopt_allocation(other.head, other.used, other.room);
        other.reset_to_small(0);
        return true;
    }

    /// Makes the elements copies of the `count` from `first` on, which are not this vector's,
    /// reusing its elements and buffer where they suffice.
    template <typename Iterator>
    constexpr void assign_items(Iterator first, size_type count) {
        if(count > capacity()) {
            rebuild(next_capacity(count), count, [&](pointer dest) {
                return mem::uninitialized_copy(counted(first, count), dest);
            });
            return;
        }
        const auto kept = (std::min)(count, size());
        std::ranges::copy(counted(first, kept), begin());
        if(count > size()) {
            mem::uninitialized_copy(counted(std::ranges::next(first, kept), count - kept), end());
        } else {
            std::ranges::destroy(head + count, end());
        }
        set_size(count);
    }

    /// The `count` elements from `first` on.
    template <typename Iterator>
    [[nodiscard]] constexpr static auto counted(Iterator first, size_type count) {
        return std::views::counted(first, static_cast<std::iter_difference_t<Iterator>>(count));
    }

    /// Inserts `value`, which is not an element, at `index`.
    template <typename U>
    constexpr iterator insert_one(size_type index, U&& value) {
        return insert_items(
            index,
            1,
            [&](pointer dest, size_type from) {
                return from == 0 ? std::construct_at(dest, std::forward<U>(value)) + 1 : dest;
            },
            [&](pointer dest, size_type n) {
                if(n != 0) {
                    *dest = std::forward<U>(value);
                }
            });
    }

    /// Inserts copies of the `count` elements from `first` on, which are not this vector's,
    /// at `index`.
    template <typename Iterator>
    constexpr iterator insert_from(size_type index, size_type count, Iterator first) {
        return insert_items(
            index,
            count,
            [&](pointer dest, size_type from) {
                return mem::uninitialized_copy(
                    counted(std::ranges::next(first, from), count - from),
                    dest);
            },
            [&](pointer dest, size_type n) { std::ranges::copy(counted(first, n), dest); });
    }

    /// Inserts `count` elements at `index` and returns the first. `construct(dest, from)`
    /// constructs those from the `from`-th on at uninitialized `dest` and returns their end,
    /// and `assign(dest, n)` assigns the first `n` over [dest, dest + n); neither reads this
    /// vector.
    template <typename Construct, typename Assign>
    constexpr iterator
        insert_items(size_type index, size_type count, Construct construct, Assign assign) {
        const auto new_size = checked_size(size(), count);
        if(new_size > capacity()) {
            const auto new_room = next_capacity(new_size);
            mem::AllocationGuard<value_type> guard(allocate(new_room), new_room);
            auto* out = mem::uninitialized_move(begin(), head + index, guard.data());
            guard.mark(out);
            out = construct(out, 0);
            guard.mark(out);
            guard.mark(mem::uninitialized_move(head + index, end(), out));
            adopt_allocation(guard.release(), new_size, new_room);
            return head + index;
        }

        auto* gap = head + index;
        auto* old_end = end();
        const auto after = static_cast<size_type>(old_end - gap);
        if(after > count) {
            // The last `count` elements move into uninitialized memory, the rest move back
            // over elements, and the new ones are assigned into the gap.
            mem::uninitialized_move(old_end - count, old_end, old_end);
            set_size(new_size);
            std::move_backward(gap, old_end - count, old_end);
            assign(gap, count);
            return gap;
        }
        // The new elements that land past the old end are constructed there, the elements
        // after the gap move behind them, and the rest of the new ones are assigned into the
        // gap. The size grows after each step, so that an exception leaves no element unowned.
        construct(old_end, after);
        set_size(size() + count - after);
        mem::uninitialized_move(gap, old_end, gap + count);
        set_size(new_size);
        assign(gap, after);
        return gap;
    }
};

template <typename T, unsigned int InlineCapacity = detail::default_buffer_size<T>::value>
class small_vector : public hybrid_vector<T>, private detail::inline_buffer<T, InlineCapacity> {
    using base_type = hybrid_vector<T>;

public:
    using typename base_type::const_reference;
    using typename base_type::size_type;
    using typename base_type::value_type;
    using base_type::assign;

    constexpr static size_type inline_capacity_v = InlineCapacity;

    constexpr small_vector() noexcept : base_type(InlineCapacity) {}

    constexpr explicit small_vector(size_type count) : small_vector() {
        this->resize(count);
    }

    constexpr small_vector(size_type count, const_reference value) : small_vector() {
        this->assign(count, value);
    }

    template <typename Generator>
        requires std::invocable<Generator&> &&
                 std::constructible_from<value_type, std::invoke_result_t<Generator&>>
    constexpr small_vector(size_type count, Generator generator) : small_vector() {
        this->reserve(count);
        for(size_type i = 0; i < count; ++i) {
            this->emplace_back(generator());
        }
    }

    template <detail::small_vector_compatible_range<value_type> Range>
    constexpr small_vector(Range&& range) : small_vector() {
        this->append(std::forward<Range>(range));
    }

    constexpr small_vector(std::initializer_list<value_type> init) : small_vector() {
        this->append(init);
    }

    constexpr small_vector(const base_type& other) : small_vector() {
        this->append(other);
    }

    constexpr small_vector(base_type&& other) : small_vector() {
        this->take_elements(other);
    }

    constexpr small_vector(const small_vector& other) :
        small_vector(static_cast<const base_type&>(other)) {}

    template <unsigned int OtherCapacity>
    constexpr small_vector(const small_vector<value_type, OtherCapacity>& other) :
        small_vector(static_cast<const base_type&>(other)) {}

    constexpr small_vector(small_vector&& other) noexcept(
        std::is_nothrow_move_constructible_v<value_type>) : small_vector() {
        this->take_elements(other);
        refill(other);
    }

    template <unsigned int OtherCapacity>
    constexpr small_vector(small_vector<value_type, OtherCapacity>&& other) : small_vector() {
        this->take_elements(other);
        refill(other);
    }

    constexpr small_vector& operator=(const base_type& other) {
        base_type::operator=(other);
        return *this;
    }

    constexpr small_vector& operator=(base_type&& other) {
        base_type::operator=(std::move(other));
        return *this;
    }

    constexpr small_vector& operator=(const small_vector& other) {
        base_type::operator=(other);
        return *this;
    }

    constexpr small_vector&
        operator=(small_vector&& other) noexcept(std::is_nothrow_move_constructible_v<value_type>) {
        if(this != std::addressof(other)) {
            base_type::operator=(std::move(other));
            refill(other);
        }
        return *this;
    }

    template <unsigned int OtherCapacity>
    constexpr small_vector& operator=(small_vector<value_type, OtherCapacity>&& other) {
        base_type::operator=(std::move(other));
        refill(other);
        return *this;
    }

    constexpr small_vector& operator=(std::initializer_list<value_type> init) {
        this->assign(init);
        return *this;
    }

    template <unsigned int OtherCapacity>
    constexpr void assign(small_vector<value_type, OtherCapacity>&& other) {
        *this = std::move(other);
    }

    /// Construct a small_vector by adopting a pre-allocated buffer.
    /// The buffer must have been allocated with mem::allocate<value_type>.
    /// The small_vector takes ownership and will deallocate it on destruction.
    [[nodiscard]] constexpr static small_vector from_raw_parts(value_type* data,
                                                               size_type count,
                                                               size_type capacity) {
        // Moved out of a local, so that the move checks the buffer against the inline buffer
        // of the vector it ends up in (see allocate()).
        small_vector adopted;
        if(data != nullptr && capacity > 0) {
            adopted.adopt_allocation(data, count, capacity);
        }
        return small_vector(std::move(adopted));
    }

    [[nodiscard]] constexpr size_type inline_capacity() const noexcept {
        return InlineCapacity;
    }

    [[nodiscard]] constexpr bool inlinable() const noexcept {
        return this->size() <= InlineCapacity;
    }

    /// Frees what the elements do not need: back into the inline buffer when they fit, into
    /// an allocation of their size otherwise.
    constexpr void shrink_to_fit() {
        if(!this->on_heap()) {
            return;
        }
        const auto count = this->size();
        if(!std::is_constant_evaluated() && count <= InlineCapacity) {
            auto* inline_head = this->inline_begin();
            mem::uninitialized_move(this->begin(), this->end(), inline_head);
            this->adopt_allocation(inline_head, count, InlineCapacity);
        } else if(count != this->capacity()) {
            this->rebuild(count, count, [this](value_type* first) {
                return mem::uninitialized_move(this->begin(), this->end(), first);
            });
        }
    }

private:
    /// Gives `other`, moved from, the room of its inline buffer back: through the base,
    /// taking its allocation left it none. One that kept its allocation keeps it.
    template <unsigned int OtherCapacity>
    constexpr static void refill(small_vector<value_type, OtherCapacity>& other) noexcept {
        if(!other.on_heap()) {
            other.reset_to_small(OtherCapacity);
        }
    }
};

template <typename Range>
    requires detail::small_vector_compatible_range<Range, std::ranges::range_value_t<Range>>
small_vector(Range&&) -> small_vector<std::ranges::range_value_t<Range>>;

template <typename T>
small_vector(std::initializer_list<T>) -> small_vector<T>;

template <typename T>
using vector = small_vector<T, 0>;

}  // namespace kota
