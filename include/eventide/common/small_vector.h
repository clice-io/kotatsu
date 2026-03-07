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
#include <new>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "memory.h"

namespace eventide {

template <typename T>
class hybrid_vector;

template <typename T, unsigned int InlineCapacity>
class small_vector;

namespace detail {

template <typename T>
struct is_small_vector : std::false_type {};

template <typename T>
struct is_small_vector<hybrid_vector<T>> : std::true_type {};

template <typename T, unsigned int InlineCapacity>
struct is_small_vector<small_vector<T, InlineCapacity>> : std::true_type {};

template <typename Range, typename T>
concept small_vector_compatible_range =
    std::ranges::input_range<Range> &&
    std::constructible_from<T, std::ranges::range_reference_t<Range>> &&
    !is_small_vector<std::remove_cvref_t<Range>>::value;

template <typename T>
using small_vector_size_type =
    std::conditional_t<sizeof(T) < 4 && sizeof(void*) >= 8, std::uint64_t, std::uint32_t>;

template <typename T>
struct default_buffer_size {
    constexpr static std::size_t preferred_size = 64;

    static_assert(
        sizeof(T) <= 256,
        "Default small_vector inline storage would be too large. " "Use small_vector<T, N> with an explicit inline capacity.");

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
        requires requires { lhs <=> rhs; }
    {
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
            })
    {
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

template <typename T>
struct alignas(T) inline_buffer<T, 0> {};

}  // namespace detail

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
    using size_storage_type = detail::small_vector_size_type<value_type>;

    constexpr static bool takes_param_by_value =
        std::is_trivially_copy_constructible_v<value_type> &&
        std::is_trivially_move_constructible_v<value_type> &&
        std::is_trivially_destructible_v<value_type> && sizeof(value_type) <= 2 * sizeof(void*);

protected:
    constexpr explicit hybrid_vector(size_type inline_capacity) noexcept :
        m_begin(std::is_constant_evaluated() ? nullptr : first_element()),
        m_capacity(
            static_cast<size_storage_type>(std::is_constant_evaluated() ? 0 : inline_capacity)) {}

    [[nodiscard]] constexpr pointer inline_begin() noexcept {
        if(std::is_constant_evaluated()) {
            return nullptr;
        }
        return first_element();
    }

    [[nodiscard]] constexpr const_pointer inline_begin() const noexcept {
        if(std::is_constant_evaluated()) {
            return nullptr;
        }
        return first_element();
    }

    constexpr void reset_to_small(size_type inline_capacity) noexcept {
        this->m_begin = inline_begin();
        this->m_size = 0;
        this->m_capacity = static_cast<size_storage_type>(inline_capacity);
    }

    constexpr void set_size(size_type count) noexcept {
        assert(count <= capacity());
        m_size = static_cast<size_storage_type>(count);
    }

private:
    constexpr static std::size_t header_alignment = alignof(pointer) > alignof(size_storage_type)
                                                        ? alignof(pointer)
                                                        : alignof(size_storage_type);

    struct alignment_and_size {
        alignas(header_alignment) std::byte header[sizeof(pointer) + 2 * sizeof(size_storage_type)];
        alignas(value_type) std::byte first_element[sizeof(value_type)];
    };

    pointer m_begin;
    size_storage_type m_size = 0;
    size_storage_type m_capacity;

protected:
    [[nodiscard]] constexpr static auto pointer_range(pointer first, pointer last) noexcept {
        return std::ranges::subrange(first, last);
    }

    [[nodiscard]]
    constexpr static auto pointer_range(const_pointer first, const_pointer last) noexcept {
        return std::ranges::subrange(first, last);
    }

    [[nodiscard]] constexpr static auto counted_range(pointer first, size_type count) noexcept {
        if(count == 0) {
            return pointer_range(first, first);
        }

        assert(first != nullptr);
        return pointer_range(first, first + static_cast<difference_type>(count));
    }

    [[nodiscard]]
    constexpr static auto counted_range(const_pointer first, size_type count) noexcept {
        if(count == 0) {
            return pointer_range(first, first);
        }

        assert(first != nullptr);
        return pointer_range(first, first + static_cast<difference_type>(count));
    }

    [[nodiscard]] constexpr auto range_to(pointer last) noexcept {
        return pointer_range(begin(), last);
    }

    [[nodiscard]] constexpr auto range_to(const_pointer last) const noexcept {
        return pointer_range(begin(), last);
    }

private:
    [[nodiscard]] pointer first_element() noexcept {
        return reinterpret_cast<pointer>(reinterpret_cast<std::byte*>(this) +
                                         offsetof(alignment_and_size, first_element));
    }

    [[nodiscard]] const_pointer first_element() const noexcept {
        return reinterpret_cast<const_pointer>(reinterpret_cast<const std::byte*>(this) +
                                               offsetof(alignment_and_size, first_element));
    }

    [[nodiscard]] constexpr auto elements() noexcept {
        return pointer_range(begin(), end());
    }

    [[nodiscard]] constexpr auto elements() const noexcept {
        return pointer_range(begin(), end());
    }

    [[nodiscard]] constexpr auto prefix(size_type count) noexcept {
        return counted_range(begin(), count);
    }

    [[nodiscard]] constexpr auto prefix(size_type count) const noexcept {
        return counted_range(begin(), count);
    }

    [[nodiscard]] constexpr auto suffix(size_type offset) noexcept {
        return pointer_range(prefix(offset).end(), end());
    }

    [[nodiscard]] constexpr auto suffix(size_type offset) const noexcept {
        return pointer_range(prefix(offset).end(), end());
    }

    [[nodiscard]] constexpr static auto make_allocation_guard(size_type capacity) {
        return mem::allocation_guard<value_type>(capacity);
    }

    template <typename... Args>
    [[nodiscard]] constexpr static auto make_temporary(Args&&... args) {
        return mem::stack_temporary<value_type>(std::forward<Args>(args)...);
    }

    [[nodiscard]] constexpr bool references_storage(const_pointer ptr) const noexcept {
        if(std::is_constant_evaluated()) {
            for(auto current = begin(); current != end(); ++current) {
                if(std::addressof(*current) == ptr) {
                    return true;
                }
            }
            return false;
        }

        return mem::pointer_in_range(ptr, elements());
    }

    template <std::ranges::range Range>
    [[nodiscard]] constexpr bool range_references_storage(Range&& range) const noexcept {
        using reference_type = std::ranges::range_reference_t<Range>;

        if constexpr(std::is_reference_v<reference_type> &&
                     std::same_as<std::remove_cvref_t<reference_type>, value_type>) {
            for(auto&& element: range) {
                if(references_storage(std::addressof(element))) {
                    return true;
                }
            }
        }

        return false;
    }

    constexpr void assert_safe_to_reference_after_resize(const_pointer ptr,
                                                         size_type new_size) const noexcept {
        auto safe = [&]() noexcept {
            if(!references_storage(ptr)) {
                return true;
            }
            if(new_size <= size()) {
                return ptr < prefix(new_size).end();
            }
            return new_size <= capacity();
        };
        assert(
            safe() &&
            "Attempting to reference an element of the vector in an operation that " "invalidates it");
    }

    template <std::ranges::range Range>
    constexpr void assert_safe_to_reference_after_clear(Range&& range) const noexcept {
        if(std::is_constant_evaluated()) {
            return;
        }

        if constexpr(std::ranges::contiguous_range<Range> && std::ranges::sized_range<Range> &&
                     std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>,
                                  value_type>) {
            const auto count = static_cast<size_type>(std::ranges::size(range));
            if(count == 0) {
                return;
            }

            const_pointer first = std::ranges::data(range);
            assert_safe_to_reference_after_resize(first, 0);
            assert_safe_to_reference_after_resize(first + static_cast<difference_type>(count - 1),
                                                  0);
        }
    }

    template <std::ranges::range Range>
    constexpr void assert_safe_to_add_range(Range&& range) const noexcept {
        if(std::is_constant_evaluated()) {
            return;
        }

        if constexpr(std::ranges::contiguous_range<Range> && std::ranges::sized_range<Range> &&
                     std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>,
                                  value_type>) {
            const auto count = static_cast<size_type>(std::ranges::size(range));
            if(count == 0) {
                return;
            }

            const_pointer first = std::ranges::data(range);
            const auto new_size = checked_size(this->m_size, count);
            assert_safe_to_reference_after_resize(first, new_size);
            assert_safe_to_reference_after_resize(first + static_cast<difference_type>(count - 1),
                                                  new_size);
        }
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

    [[nodiscard]] constexpr bool same_object(const hybrid_vector& other) const noexcept {
        return reinterpret_cast<const void*>(this) == reinterpret_cast<const void*>(&other);
    }

    [[nodiscard]] constexpr size_type checked_size(size_type base, size_type extra) const {
        if(extra > max_size() - base) {
            EVENTIDE_THROW(std::length_error("small_vector capacity overflow"));
        }
        return base + extra;
    }

    [[nodiscard]] constexpr size_type next_capacity(size_type min_capacity) const {
        if(min_capacity > max_size()) {
            EVENTIDE_THROW(std::length_error("small_vector capacity overflow"));
        }

        size_type grown = this->m_capacity == 0 ? 1 : this->m_capacity * 2;
        if(grown < this->m_capacity || grown < min_capacity) {
            grown = min_capacity;
        }
        return grown;
    }

    constexpr void destroy_elements() noexcept {
        mem::destroy_range(elements());
        this->m_size = 0;
    }

    constexpr void commit_replacement(pointer new_begin,
                                      size_type new_size,
                                      size_type new_capacity) noexcept {
        auto old_begin = this->m_begin;
        const auto old_size = this->m_size;
        const auto old_capacity = this->m_capacity;
        const auto was_inline = old_begin == inline_begin();

        this->m_begin = new_begin;
        this->m_size = static_cast<decltype(this->m_size)>(new_size);
        this->m_capacity = static_cast<decltype(this->m_capacity)>(new_capacity);

        mem::destroy_range(counted_range(old_begin, old_size));
        if(!was_inline) {
            mem::deallocate(old_begin, old_capacity);
        }
    }

    constexpr void grow_to(size_type min_capacity) {
        const auto new_capacity = next_capacity(min_capacity);
        auto guard = make_allocation_guard(new_capacity);
        auto* out = mem::uninitialized_relocate(elements(), guard.data());
        guard.mark(out);
        commit_replacement(guard.release(), size(), new_capacity);
    }

    constexpr void reserve_for_append(size_type count) {
        const auto new_size = checked_size(size(), count);
        if(new_size > capacity()) {
            grow_to(new_size);
        }
    }

    constexpr void resize_fill(size_type count, const_reference value) {
        if(count < size()) {
            shrink_to_size(count);
            return;
        }

        if(count == size()) {
            return;
        }

        reserve(count);
        mem::uninitialized_fill(counted_range(end(), count - size()), value);
        this->set_size(count);
    }

    constexpr void shrink_to_size(size_type count) noexcept {
        mem::destroy_range(suffix(count));
        this->set_size(count);
    }

    template <bool ForOverwrite>
    constexpr void resize_impl(size_type count) {
        if(count < size()) {
            shrink_to_size(count);
            return;
        }

        if(count == size()) {
            return;
        }

        reserve(count);
        if constexpr(ForOverwrite) {
            mem::uninitialized_default_construct(counted_range(end(), count - size()));
        } else {
            mem::uninitialized_value_construct(counted_range(end(), count - size()));
        }
        this->set_size(count);
    }

    [[nodiscard]] constexpr bool
        should_steal_allocation_from(const hybrid_vector& other) const noexcept {
        return other.begin() != other.inline_begin();
    }

    constexpr void append_copies(size_type count, const_reference value) {
        if(count == 0) {
            return;
        }

        reserve_for_append(count);
        mem::uninitialized_fill(counted_range(end(), count), value);
        this->set_size(size() + count);
    }

    constexpr void assign_copies(size_type count, const_reference value) {
        if(count > capacity()) {
            const auto new_capacity = next_capacity(count);
            auto guard = make_allocation_guard(new_capacity);
            auto* out = mem::uninitialized_fill(counted_range(guard.data(), count), value);
            guard.mark(out);
            commit_replacement(guard.release(), count, new_capacity);
            return;
        }

        std::ranges::fill_n(begin(), (std::min)(count, size()), value);
        if(count > size()) {
            mem::uninitialized_fill(counted_range(end(), count - size()), value);
        } else if(count < size()) {
            mem::destroy_range(suffix(count));
        }
        this->set_size(count);
    }

    constexpr iterator insert_copies(iterator pos, size_type count, const_reference value) {
        if(count == 0) {
            return pos;
        }

        if(checked_size(size(), count) > capacity()) {
            return insert_fill_reallocate(pos, count, value);
        }
        return insert_fill_inplace(pos, count, value);
    }

    template <typename... Args>
    constexpr reference reallocate_and_emplace_back(Args&&... args) {
        const auto old_size = size();
        const auto new_size = checked_size(old_size, 1);
        const auto new_capacity = next_capacity(new_size);
        auto* new_begin = mem::allocate<value_type>(new_capacity);
        auto* relocated_end = new_begin;
        bool back_constructed = false;

        EVENTIDE_TRY {
            mem::construct(counted_range(new_begin, old_size).end(), std::forward<Args>(args)...);
            back_constructed = true;
            relocated_end = mem::uninitialized_relocate(elements(), new_begin);
        }
        EVENTIDE_CATCH_ALL() {
            mem::destroy_range(std::ranges::subrange(new_begin, relocated_end));
            if(back_constructed) {
                mem::destroy(counted_range(new_begin, old_size).end());
            }
            mem::deallocate(new_begin, new_capacity);
            EVENTIDE_RETHROW();
        }

        commit_replacement(new_begin, new_size, new_capacity);
        return back();
    }

    template <typename U>
    constexpr iterator reallocate_and_insert_one(const_iterator pos, U&& value) {
        const auto index = mem::range_length(range_to(pos));
        const auto old_size = size();
        const auto new_size = checked_size(old_size, 1);
        const auto new_capacity = next_capacity(new_size);
        auto guard = make_allocation_guard(new_capacity);

        auto* out = mem::uninitialized_relocate(prefix(index), guard.data());
        guard.mark(out);
        mem::construct(out, std::forward<U>(value));
        ++out;
        guard.mark(out);
        out = mem::uninitialized_relocate(suffix(index), out);
        guard.mark(out);

        commit_replacement(guard.release(), new_size, new_capacity);
        return prefix(index).end();
    }

    template <typename U>
    constexpr iterator insert_one_impl(const_iterator pos, U&& value) {
        const auto index = mem::range_length(range_to(pos));

        if(index == size()) {
            emplace_back(std::forward<U>(value));
            return end() - 1;
        }

        if(size() == capacity()) {
            return reallocate_and_insert_one(pos, std::forward<U>(value));
        }

        auto insert_pos = prefix(index).end();
        auto old_end = end();

        mem::construct(old_end, std::move(*(old_end - 1)));
        this->set_size(size() + 1);
        std::ranges::move_backward(insert_pos, old_end - 1, old_end);
        *insert_pos = std::forward<U>(value);
        return insert_pos;
    }

    constexpr iterator insert_fill_reallocate(const_iterator pos,
                                              size_type count,
                                              const_reference value) {
        const auto index = mem::range_length(range_to(pos));
        const auto old_size = size();
        const auto new_size = checked_size(old_size, count);
        const auto new_capacity = next_capacity(new_size);
        auto guard = make_allocation_guard(new_capacity);

        auto* out = mem::uninitialized_relocate(prefix(index), guard.data());
        guard.mark(out);
        out = mem::uninitialized_fill(counted_range(out, count), value);
        guard.mark(out);
        out = mem::uninitialized_relocate(suffix(index), out);
        guard.mark(out);

        commit_replacement(guard.release(), new_size, new_capacity);
        return prefix(index).end();
    }

    constexpr iterator insert_fill_inplace(const_iterator pos,
                                           size_type count,
                                           const_reference value) {
        const auto index = mem::range_length(range_to(pos));
        auto insert_pos = prefix(index).end();

        if(index == size()) {
            append(count, value);
            return prefix(index).end();
        }

        auto old_end = end();
        const auto elems_after = static_cast<size_type>(old_end - insert_pos);

        if(elems_after > count) {
            mem::uninitialized_copy(mem::move_range(old_end - count, old_end), old_end);
            this->set_size(size() + count);
            std::ranges::move_backward(insert_pos, old_end - count, old_end);
            std::ranges::fill_n(insert_pos, count, value);
            return insert_pos;
        }

        auto* middle = mem::uninitialized_fill(counted_range(old_end, count - elems_after), value);
        middle = mem::uninitialized_copy(mem::move_range(insert_pos, old_end), middle);
        this->set_size(size() + count);
        std::ranges::fill_n(insert_pos, elems_after, value);
        return insert_pos;
    }

    template <std::ranges::forward_range Range>
    constexpr iterator insert_range_reallocate(const_iterator pos, Range&& range, size_type count) {
        const auto index = mem::range_length(range_to(pos));
        const auto old_size = size();
        const auto new_size = checked_size(old_size, count);
        const auto new_capacity = next_capacity(new_size);
        auto guard = make_allocation_guard(new_capacity);

        auto* out = mem::uninitialized_relocate(prefix(index), guard.data());
        guard.mark(out);
        out = mem::uninitialized_copy(std::forward<Range>(range), out);
        guard.mark(out);
        out = mem::uninitialized_relocate(suffix(index), out);
        guard.mark(out);

        commit_replacement(guard.release(), new_size, new_capacity);
        return prefix(index).end();
    }

    template <std::ranges::forward_range Range>
    constexpr iterator insert_aliased_forward_range(const_iterator pos,
                                                    Range&& range,
                                                    size_type count) {
        auto guard = make_allocation_guard(count);
        auto* out = mem::uninitialized_copy(std::forward<Range>(range), guard.data());
        guard.mark(out);

        auto temp_range = std::ranges::subrange(guard.data(), out);
        if(checked_size(size(), count) > capacity()) {
            return insert_range_reallocate(pos, temp_range, count);
        }

        return insert_range_inplace(pos, temp_range, count);
    }

    template <std::ranges::forward_range Range>
    constexpr iterator insert_range_inplace(const_iterator pos, Range&& range, size_type count) {
        const auto index = mem::range_length(range_to(pos));
        auto insert_pos = prefix(index).end();

        if(index == size()) {
            append(std::forward<Range>(range));
            return prefix(index).end();
        }

        auto old_end = end();
        const auto elems_after = static_cast<size_type>(old_end - insert_pos);

        if(elems_after > count) {
            mem::uninitialized_copy(mem::move_range(old_end - count, old_end), old_end);
            this->set_size(size() + count);
            std::ranges::move_backward(insert_pos, old_end - count, old_end);
            std::ranges::copy(range, insert_pos);
            return insert_pos;
        }

        auto first_part_end = std::ranges::next(std::ranges::begin(range), elems_after);
        auto tail_range = std::ranges::subrange(first_part_end, std::ranges::end(range));
        auto* middle = mem::uninitialized_copy(tail_range, old_end);
        middle = mem::uninitialized_copy(mem::move_range(insert_pos, old_end), middle);
        this->set_size(size() + count);
        std::ranges::copy(std::ranges::subrange(std::ranges::begin(range), first_part_end),
                          insert_pos);
        return insert_pos;
    }

    template <std::ranges::forward_range Range>
    constexpr iterator insert_forward_range(const_iterator pos, Range&& range) {
        const auto count = mem::range_length(range);
        if(count == 0) {
            return const_cast<pointer>(pos);
        }

        if(range_references_storage(range)) {
            return insert_aliased_forward_range(pos, std::forward<Range>(range), count);
        }

        assert_safe_to_add_range(range);
        if(checked_size(size(), count) > capacity()) {
            return insert_range_reallocate(pos, std::forward<Range>(range), count);
        }

        return insert_range_inplace(pos, std::forward<Range>(range), count);
    }

    template <std::ranges::input_range Range>
    constexpr iterator insert_input_range(const_iterator pos, Range&& range) {
        size_type index = mem::range_length(range_to(pos));
        const auto original_index = index;
        for(auto&& value: range) {
            insert(prefix(index).end(), std::forward<decltype(value)>(value));
            ++index;
        }
        return prefix(original_index).end();
    }

    constexpr void steal_allocation_from(hybrid_vector& other) noexcept {
        this->m_begin = other.m_begin;
        this->m_size = other.m_size;
        this->m_capacity = other.m_capacity;
        other.reset_to_small(0);
    }

protected:
    /// Adopt a pre-allocated buffer. The buffer must have been allocated with
    /// mem::allocate<value_type>. After this call, the hybrid_vector owns the
    /// buffer and will deallocate it on destruction.
    constexpr void adopt_allocation(pointer data, size_type count, size_type cap) noexcept {
        assert(count <= cap);
        destroy_elements();
        if(begin() != inline_begin()) {
            mem::deallocate(this->m_begin, this->m_capacity);
        }
        this->m_begin = data;
        this->m_size = static_cast<size_storage_type>(count);
        this->m_capacity = static_cast<size_storage_type>(cap);
    }

    constexpr void move_from_other(hybrid_vector&& other) {
        if(other.empty()) {
            return;
        }

        if(should_steal_allocation_from(other)) {
            steal_allocation_from(other);
            return;
        }

        append(mem::move_range(other.begin(), other.end()));
        other.clear();
    }

    constexpr void copy_assign_from_other(const hybrid_vector& other) {
        const auto other_size = other.size();
        size_type current_size = size();

        if(current_size >= other_size) {
            if(other_size != 0) {
                std::ranges::copy_n(other.begin(), other_size, begin());
            }
            mem::destroy_range(suffix(other_size));
            this->set_size(other_size);
            return;
        }

        if(capacity() < other_size) {
            clear();
            current_size = 0;
            reserve(other_size);
        } else if(current_size != 0) {
            std::ranges::copy_n(other.begin(), current_size, begin());
        }

        mem::uninitialized_copy(other.suffix(current_size), prefix(current_size).end());
        this->set_size(other_size);
    }

    constexpr void move_assign_from_other(hybrid_vector&& other) {
        if(should_steal_allocation_from(other)) {
            auto old_begin = this->m_begin;
            const auto old_capacity = this->m_capacity;
            const auto was_inline = old_begin == inline_begin();
            destroy_elements();
            if(!was_inline) {
                mem::deallocate(old_begin, old_capacity);
            }
            steal_allocation_from(other);
            return;
        }

        const auto other_size = other.size();
        size_type current_size = size();

        if(current_size >= other_size) {
            if(other_size != 0) {
                std::ranges::move(other.begin(), other.end(), begin());
            }
            mem::destroy_range(suffix(other_size));
            this->set_size(other_size);
            other.clear();
            return;
        }

        if(capacity() < other_size) {
            clear();
            current_size = 0;
            reserve(other_size);
        } else if(current_size != 0) {
            std::ranges::move(other.prefix(current_size), begin());
        }

        mem::uninitialized_copy(mem::move_range(other.prefix(current_size).end(), other.end()),
                                prefix(current_size).end());
        this->set_size(other_size);
        other.clear();
    }

public:
    constexpr ~hybrid_vector() {
        destroy_elements();
        if(begin() != inline_begin()) {
            mem::deallocate(this->m_begin, this->m_capacity);
        }
    }

    [[nodiscard]] constexpr iterator begin() noexcept {
        return this->m_begin;
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return this->m_begin;
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return begin();
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        return prefix(size()).end();
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        return prefix(size()).end();
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
        return begin();
    }

    [[nodiscard]] constexpr const_pointer data() const noexcept {
        return begin();
    }

    [[nodiscard]] constexpr size_type size() const noexcept {
        return m_size;
    }

    [[nodiscard]] constexpr size_type size_in_bytes() const noexcept {
        return size() * sizeof(value_type);
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return m_size == 0;
    }

    [[nodiscard]] constexpr size_type capacity() const noexcept {
        return m_capacity;
    }

    [[nodiscard]] constexpr size_type capacity_in_bytes() const noexcept {
        return capacity() * sizeof(value_type);
    }

    [[nodiscard]] constexpr bool inlined() const noexcept {
        if(std::is_constant_evaluated()) {
            return false;
        }
        return begin() == inline_begin();
    }

    [[nodiscard]] constexpr size_type max_size() const noexcept {
        return (std::min)(static_cast<size_type>((std::numeric_limits<size_storage_type>::max)()),
                          (std::numeric_limits<size_type>::max)() / sizeof(value_type));
    }

    [[nodiscard]] constexpr reference operator[](size_type idx) noexcept {
        assert(idx < size());
        return begin()[idx];
    }

    [[nodiscard]] constexpr const_reference operator[](size_type idx) const noexcept {
        assert(idx < size());
        return begin()[idx];
    }

    constexpr reference at(size_type idx) {
        if(idx >= size()) {
            EVENTIDE_THROW(std::out_of_range("small_vector index out of range"));
        }
        return (*this)[idx];
    }

    constexpr const_reference at(size_type idx) const {
        if(idx >= size()) {
            EVENTIDE_THROW(std::out_of_range("small_vector index out of range"));
        }
        return (*this)[idx];
    }

    [[nodiscard]] constexpr reference front() noexcept {
        assert(!empty());
        return begin()[0];
    }

    [[nodiscard]] constexpr const_reference front() const noexcept {
        assert(!empty());
        return begin()[0];
    }

    [[nodiscard]] constexpr reference back() noexcept {
        assert(!empty());
        return end()[-1];
    }

    [[nodiscard]] constexpr const_reference back() const noexcept {
        assert(!empty());
        return end()[-1];
    }

    constexpr void clear() noexcept {
        destroy_elements();
    }

    constexpr void reserve(size_type new_capacity) {
        if(new_capacity > capacity()) {
            grow_to(new_capacity);
        }
    }

    constexpr void resize(size_type count) {
        resize_impl<false>(count);
    }

    constexpr void resize_for_overwrite(size_type count) {
        resize_impl<true>(count);
    }

    constexpr void resize(size_type count, value_type value)
        requires (takes_param_by_value)
    {
        resize_fill(count, value);
    }

    constexpr void resize(size_type count, const_reference value)
        requires (!takes_param_by_value)
    {
        if(count > capacity() && references_storage(std::addressof(value))) {
            auto tmp = make_temporary(value);
            resize_fill(count, tmp.get());
            return;
        }

        resize_fill(count, value);
    }

    constexpr void truncate(size_type count) {
        assert(count <= size());
        shrink_to_size(count);
    }

    constexpr void push_back(value_type value)
        requires (takes_param_by_value)
    {
        emplace_back(value);
    }

    constexpr void push_back(const_reference value)
        requires (!takes_param_by_value)
    {
        if(references_storage(std::addressof(value))) {
            auto tmp = make_temporary(value);
            emplace_back(tmp.get());
            return;
        }
        emplace_back(value);
    }

    constexpr void push_back(value_type&& value)
        requires (!takes_param_by_value)
    {
        if(references_storage(std::addressof(value))) {
            auto tmp = make_temporary(std::move(value));
            emplace_back(std::move(tmp.release()));
            return;
        }
        emplace_back(std::move(value));
    }

    template <typename... Args>
    constexpr reference emplace_back(Args&&... args) {
        if(size() == capacity()) {
            return reallocate_and_emplace_back(std::forward<Args>(args)...);
        }

        mem::construct(end(), std::forward<Args>(args)...);
        this->set_size(size() + 1);
        return back();
    }

    constexpr void pop_back() noexcept {
        assert(!empty());
        this->set_size(size() - 1);
        mem::destroy(end());
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

    constexpr void append(size_type count, value_type value)
        requires (takes_param_by_value)
    {
        append_copies(count, value);
    }

    constexpr void append(size_type count, const_reference value)
        requires (!takes_param_by_value)
    {
        if(references_storage(std::addressof(value))) {
            auto tmp = make_temporary(value);
            append_copies(count, tmp.get());
            return;
        }

        append_copies(count, value);
    }

    template <detail::small_vector_compatible_range<value_type> Range>
    constexpr void append(Range&& range) {
        if constexpr(std::ranges::forward_range<Range>) {
            assert_safe_to_add_range(range);
            const auto count = mem::range_length(range);
            if(count == 0) {
                return;
            }

            reserve_for_append(count);
            mem::uninitialized_copy(std::forward<Range>(range), end());
            this->set_size(size() + count);
        } else {
            for(auto&& current: range) {
                emplace_back(std::forward<decltype(current)>(current));
            }
        }
    }

    constexpr void append(std::initializer_list<value_type> init) {
        append(std::ranges::subrange(init.begin(), init.end()));
    }

    constexpr void append(const hybrid_vector& other) {
        append(std::ranges::subrange(other.begin(), other.end()));
    }

    constexpr void append(hybrid_vector&& other) {
        if(same_object(other)) {
            return;
        }
        append(mem::move_range(other.begin(), other.end()));
        other.clear();
    }

    constexpr void assign(size_type count, value_type value)
        requires (takes_param_by_value)
    {
        assign_copies(count, value);
    }

    constexpr void assign(size_type count, const_reference value)
        requires (!takes_param_by_value)
    {
        if(references_storage(std::addressof(value))) {
            auto tmp = make_temporary(value);
            assign_copies(count, tmp.get());
            return;
        }

        assign_copies(count, value);
    }

    template <detail::small_vector_compatible_range<value_type> Range>
    constexpr void assign(Range&& range) {
        assert_safe_to_reference_after_clear(range);
        destroy_elements();
        append(std::forward<Range>(range));
    }

    constexpr void assign(std::initializer_list<value_type> init) {
        destroy_elements();
        append(init);
    }

    constexpr void assign(const hybrid_vector& other) {
        if(same_object(other)) {
            return;
        }
        destroy_elements();
        append(std::ranges::subrange(other.begin(), other.end()));
    }

    constexpr void assign(hybrid_vector&& other) {
        if(same_object(other)) {
            return;
        }

        move_assign_from_other(std::move(other));
    }

    constexpr iterator insert(iterator pos, value_type value)
        requires (takes_param_by_value)
    {
        assert(valid_insert_position(pos));
        return insert_one_impl(pos, value);
    }

    constexpr iterator insert(iterator pos, const_reference value)
        requires (!takes_param_by_value)
    {
        assert(valid_insert_position(pos));
        auto tmp = make_temporary(value);
        return insert_one_impl(pos, std::move(tmp.release()));
    }

    constexpr iterator insert(iterator pos, value_type&& value)
        requires (!takes_param_by_value)
    {
        assert(valid_insert_position(pos));
        if(references_storage(std::addressof(value))) {
            auto tmp = make_temporary(std::move(value));
            return insert_one_impl(pos, std::move(tmp.release()));
        }
        return insert_one_impl(pos, std::move(value));
    }

    constexpr iterator insert(iterator pos, size_type count, value_type value)
        requires (takes_param_by_value)
    {
        assert(valid_insert_position(pos));
        return insert_copies(pos, count, value);
    }

    constexpr iterator insert(iterator pos, size_type count, const_reference value)
        requires (!takes_param_by_value)
    {
        assert(valid_insert_position(pos));
        auto tmp = make_temporary(value);
        return insert_copies(pos, count, tmp.get());
    }

    template <detail::small_vector_compatible_range<value_type> Range>
    constexpr iterator insert(iterator pos, Range&& range) {
        assert(valid_insert_position(pos));
        if constexpr(std::ranges::forward_range<Range>) {
            return insert_forward_range(pos, std::forward<Range>(range));
        }
        assert_safe_to_add_range(range);
        return insert_input_range(pos, std::forward<Range>(range));
    }

    constexpr iterator insert(iterator pos, std::initializer_list<value_type> init) {
        return insert(pos, std::ranges::subrange(init.begin(), init.end()));
    }

    template <typename... Args>
    constexpr iterator emplace(iterator pos, Args&&... args) {
        auto tmp = make_temporary(std::forward<Args>(args)...);
        return insert(pos, std::move(tmp.release()));
    }

    constexpr iterator erase(const_iterator pos) {
        assert(valid_erase_range(pos, std::next(pos)));
        return erase(pos, pos + 1);
    }

    constexpr iterator erase(const_iterator first, const_iterator last) {
        assert(valid_erase_range(first, last));
        if(first == last) {
            return const_cast<pointer>(first);
        }

        auto erase_begin = const_cast<pointer>(first);
        auto erase_end = const_cast<pointer>(last);
        auto new_end = std::ranges::move(erase_end, end(), erase_begin).out;
        mem::destroy_range(std::ranges::subrange(new_end, end()));
        this->set_size(static_cast<size_type>(new_end - begin()));
        return erase_begin;
    }

    constexpr void swap(hybrid_vector& other) {
        if(same_object(other)) {
            return;
        }

        if(begin() != inline_begin() && other.begin() != other.inline_begin()) {
            std::swap(this->m_begin, other.m_begin);
            std::swap(this->m_size, other.m_size);
            std::swap(this->m_capacity, other.m_capacity);
            return;
        }

        reserve(other.size());
        other.reserve(size());

        const size_type shared = (std::min)(size(), other.size());

        for(size_type i = 0; i != shared; ++i) {
            std::swap((*this)[i], other[i]);
        }

        if(size() > other.size()) {
            const auto diff = size() - other.size();
            mem::uninitialized_copy(mem::move_range(prefix(shared).end(), end()), other.end());
            other.set_size(other.size() + diff);
            mem::destroy_range(suffix(shared));
            this->set_size(shared);
        } else if(other.size() > size()) {
            const auto diff = other.size() - size();
            mem::uninitialized_copy(mem::move_range(other.prefix(shared).end(), other.end()),
                                    end());
            this->set_size(size() + diff);
            mem::destroy_range(other.suffix(shared));
            other.set_size(shared);
        }
    }

    friend constexpr bool operator==(const hybrid_vector& lhs, const hybrid_vector& rhs) {
        return lhs.size() == rhs.size() && std::ranges::equal(lhs, rhs);
    }

    friend constexpr auto operator<=>(const hybrid_vector& lhs, const hybrid_vector& rhs) {
        return std::lexicographical_compare_three_way(lhs.begin(),
                                                      lhs.end(),
                                                      rhs.begin(),
                                                      rhs.end(),
                                                      detail::synth_three_way{});
    }
};

template <typename T, unsigned int InlineCapacity = detail::default_buffer_size<T>::value>
class small_vector : public hybrid_vector<T>, private detail::inline_buffer<T, InlineCapacity> {
    using base_type = hybrid_vector<T>;
    using storage_base = detail::inline_buffer<T, InlineCapacity>;

public:
    using typename base_type::const_reference;
    using typename base_type::size_type;
    using typename base_type::value_type;
    using base_type::assign;

    constexpr static size_type inline_capacity_v = InlineCapacity;

private:
    template <unsigned int OtherCapacity>
    constexpr void move_from_typed_small_vector(small_vector<value_type, OtherCapacity>&& other) {
        this->move_from_other(static_cast<base_type&&>(other));
        other.reset_to_small(OtherCapacity);
    }

public:
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
        this->append(std::ranges::subrange(other.begin(), other.end()));
    }

    constexpr small_vector(base_type&& other) : small_vector() {
        this->move_from_other(std::move(other));
    }

    constexpr small_vector(const small_vector& other) :
        small_vector(static_cast<const base_type&>(other)) {}

    template <unsigned int OtherCapacity>
    constexpr small_vector(const small_vector<value_type, OtherCapacity>& other) :
        small_vector(static_cast<const base_type&>(other)) {}

    constexpr small_vector(small_vector&& other) noexcept(
        std::is_nothrow_move_constructible_v<value_type>) : small_vector() {
        move_from_typed_small_vector(std::move(other));
    }

    template <unsigned int OtherCapacity>
    constexpr small_vector(small_vector<value_type, OtherCapacity>&& other) : small_vector() {
        move_from_typed_small_vector(std::move(other));
    }

    constexpr small_vector& operator=(const base_type& other) {
        if(static_cast<const base_type*>(this) == std::addressof(other)) {
            return *this;
        }
        this->copy_assign_from_other(other);
        return *this;
    }

    constexpr small_vector&
        operator=(base_type&& other) noexcept(std::is_nothrow_move_constructible_v<value_type>) {
        if(static_cast<const base_type*>(this) == std::addressof(other)) {
            return *this;
        }
        this->move_assign_from_other(std::move(other));
        return *this;
    }

    constexpr small_vector& operator=(const small_vector& other) {
        return *this = static_cast<const base_type&>(other);
    }

    constexpr small_vector&
        operator=(small_vector&& other) noexcept(std::is_nothrow_move_constructible_v<value_type>) {
        if(this != std::addressof(other)) {
            this->move_assign_from_other(static_cast<base_type&&>(other));
            other.reset_to_small(InlineCapacity);
        }
        return *this;
    }

    template <unsigned int OtherCapacity>
    constexpr small_vector& operator=(small_vector<value_type, OtherCapacity>&& other) {
        this->move_assign_from_other(static_cast<base_type&&>(other));
        other.reset_to_small(OtherCapacity);
        return *this;
    }

    constexpr small_vector& operator=(std::initializer_list<value_type> init) {
        this->assign(init);
        return *this;
    }

    template <unsigned int OtherCapacity>
    constexpr void assign(small_vector<value_type, OtherCapacity>&& other) {
        if(reinterpret_cast<const void*>(this) ==
           reinterpret_cast<const void*>(std::addressof(other))) {
            return;
        }

        this->move_assign_from_other(static_cast<base_type&&>(other));
        other.reset_to_small(OtherCapacity);
    }

    /// Construct a small_vector by adopting a pre-allocated buffer.
    /// The buffer must have been allocated with mem::allocate<value_type>.
    /// The small_vector takes ownership and will deallocate it on destruction.
    [[nodiscard]] constexpr static small_vector from_raw_parts(value_type* data,
                                                               size_type count,
                                                               size_type capacity) {
        small_vector result;
        if(data != nullptr && capacity > 0) {
            result.adopt_allocation(data, count, capacity);
        }
        return result;
    }

    [[nodiscard]] constexpr size_type inline_capacity() const noexcept {
        return InlineCapacity;
    }

    [[nodiscard]] constexpr bool inlinable() const noexcept {
        return this->size() <= InlineCapacity;
    }

    constexpr void shrink_to_fit() {
        if(this->begin() == this->inline_begin()) {
            return;
        }

        if(this->size() == 0) {
            auto old_begin = this->m_begin;
            const auto old_capacity = this->m_capacity;
            this->reset_to_small(InlineCapacity);
            mem::deallocate(old_begin, old_capacity);
            return;
        }

        if(!std::is_constant_evaluated() && this->size() <= InlineCapacity) {
            auto* new_begin = this->inline_begin();
            auto* constructed = new_begin;
            EVENTIDE_TRY {
                constructed =
                    mem::uninitialized_relocate(std::ranges::subrange(this->begin(), this->end()),
                                                new_begin);
            }
            EVENTIDE_CATCH_ALL() {
                mem::destroy_range(std::ranges::subrange(new_begin, constructed));
                EVENTIDE_RETHROW();
            }

            auto old_begin = this->m_begin;
            const auto old_size = this->m_size;
            const auto old_capacity = this->m_capacity;

            this->m_begin = new_begin;
            this->m_capacity = static_cast<decltype(this->m_capacity)>(InlineCapacity);
            mem::destroy_range(base_type::counted_range(old_begin, old_size));
            mem::deallocate(old_begin, old_capacity);
            return;
        }

        if(this->size() == this->capacity()) {
            return;
        }

        auto guard = mem::allocation_guard<value_type>(this->size());
        auto* out = mem::uninitialized_relocate(std::ranges::subrange(this->begin(), this->end()),
                                                guard.data());
        guard.mark(out);

        this->commit_replacement(guard.release(), this->size(), this->size());
    }
};

template <typename Range>
    requires detail::small_vector_compatible_range<Range, std::ranges::range_value_t<Range>>
small_vector(Range&&) -> small_vector<std::ranges::range_value_t<Range>>;

template <typename T>
small_vector(std::initializer_list<T>) -> small_vector<T>;

template <typename T>
using vector = small_vector<T, 0>;

}  // namespace eventide
