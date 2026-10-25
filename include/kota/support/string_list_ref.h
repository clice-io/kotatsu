#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <string_view>

namespace kota {

/// A contiguous range of strings of any type, by reference: of anything a
/// std::string_view converts from, such as std::string, std::string_view,
/// small_string<N> or llvm::StringRef. What a function that reads a list of
/// strings takes, so that its caller passes the list it has as it is. It
/// refers to the range as std::span does, and must not outlive it.
class string_list_ref {
    using Element = std::string_view (*)(const void* first, std::size_t index);

public:
    template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> &&
                     std::convertible_to<const std::ranges::range_value_t<Range>&, std::string_view>
    string_list_ref(const Range& range) noexcept :
        first(std::ranges::data(range)), count(std::ranges::size(range)),
        at(&element<std::ranges::range_value_t<Range>>) {}

    /// The strings of a braced list, which lives until the end of the full
    /// expression it is written in.
    string_list_ref(std::initializer_list<std::string_view> list) noexcept :
        first(list.begin()), count(list.size()), at(&element<std::string_view>) {}

    std::size_t size() const noexcept {
        return count;
    }

    bool empty() const noexcept {
        return count == 0;
    }

    std::string_view operator[](std::size_t index) const {
        return at(first, index);
    }

    class iterator {
    public:
        using iterator_concept = std::random_access_iterator_tag;
        using value_type = std::string_view;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        std::string_view operator*() const {
            return at(first, index);
        }

        std::string_view operator[](difference_type offset) const {
            return at(first,
                      static_cast<std::size_t>(static_cast<difference_type>(index) + offset));
        }

        iterator& operator++() {
            ++index;
            return *this;
        }

        iterator operator++(int) {
            auto old = *this;
            ++index;
            return old;
        }

        iterator& operator--() {
            --index;
            return *this;
        }

        iterator operator--(int) {
            auto old = *this;
            --index;
            return old;
        }

        iterator& operator+=(difference_type offset) {
            index = static_cast<std::size_t>(static_cast<difference_type>(index) + offset);
            return *this;
        }

        iterator& operator-=(difference_type offset) {
            return *this += -offset;
        }

        friend constexpr iterator operator+(iterator it, difference_type offset) {
            return it += offset;
        }

        friend constexpr iterator operator+(difference_type offset, iterator it) {
            return it += offset;
        }

        friend constexpr iterator operator-(iterator it, difference_type offset) {
            return it -= offset;
        }

        friend constexpr difference_type operator-(const iterator& a, const iterator& b) {
            return static_cast<difference_type>(a.index) - static_cast<difference_type>(b.index);
        }

        friend constexpr bool operator==(const iterator& a, const iterator& b) {
            return a.index == b.index;
        }

        friend constexpr std::strong_ordering operator<=>(const iterator& a, const iterator& b) {
            return a.index <=> b.index;
        }

    private:
        friend class string_list_ref;

        iterator(const string_list_ref& list, std::size_t index) :
            first(list.first), at(list.at), index(index) {}

        const void* first = nullptr;

        Element at = nullptr;

        std::size_t index = 0;
    };

    iterator begin() const {
        return {*this, 0};
    }

    iterator end() const {
        return {*this, count};
    }

private:
    template <typename String>
    static std::string_view element(const void* first, std::size_t index) {
        return static_cast<const String*>(first)[index];
    }

    const void* first;

    std::size_t count;

    Element at;
};

}  // namespace kota

/// Its iterators refer to the strings, not to it.
template <>
constexpr inline bool std::ranges::enable_borrowed_range<kota::string_list_ref> = true;
