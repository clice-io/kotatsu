#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "kota/support/memory.h"
#include "kota/support/small_vector.h"
#include "kota/support/string_ref.h"

namespace kota {

/// A SmallVector<char> with string-like convenience methods.
/// All string operations delegate to string_ref.
template <unsigned InlineCapacity>
class small_string : public small_vector<char, InlineCapacity> {
    using base = small_vector<char, InlineCapacity>;

public:
    /// Default ctor - Initialize to empty.
    constexpr small_string() = default;

    /// Initialize from a string_view.
    constexpr small_string(std::string_view s) : base(s) {}

    /// Initialize by concatenating a list of string_views.
    constexpr small_string(std::initializer_list<std::string_view> refs) : base() {
        this->append(refs);
    }

    /// Adopt a pre-allocated buffer as a small_string.
    /// The buffer must have been allocated with mem::allocate<char>.
    [[nodiscard]] constexpr static small_string from_raw_parts(char* data,
                                                               std::size_t count,
                                                               std::size_t capacity) {
        small_string result;
        static_cast<base&>(result) = base::from_raw_parts(data, count, capacity);
        return result;
    }

    // --- String Assignment ---

    using base::assign;

    /// Assign from a string_view.
    constexpr void assign(std::string_view rhs) {
        base::assign(rhs);
    }

    /// Assign the concatenation of a list of string_views, which may view this string.
    constexpr void assign(std::initializer_list<std::string_view> refs) {
        small_string joined(refs);
        *this = std::move(joined);
    }

    // --- String Concatenation ---

    using base::append;

    /// Append from a string_view.
    constexpr void append(std::string_view rhs) {
        base::append(rhs);
    }

    /// Append the concatenation of a list of string_views, which may view this string.
    constexpr void append(std::initializer_list<std::string_view> refs) {
        std::size_t count = 0;
        for(auto ref: refs) {
            count += ref.size();
        }
        this->append_with(count, [refs](char* out) {
            for(auto ref: refs) {
                out = mem::uninitialized_copy(ref, out);
            }
        });
    }

    // --- Conversion ---

    /// Get a string_ref view of this string.
    [[nodiscard]] constexpr string_ref ref() const {
        return string_ref(this->data(), this->size());
    }

    /// Get a null-terminated C string.
    constexpr const char* c_str() {
        this->push_back(0);
        this->pop_back();
        return this->data();
    }

    /// Implicit conversion to string_ref.
    constexpr operator string_ref() const {
        return ref();
    }

    /// Implicit conversion to string_view.
    constexpr operator std::string_view() const {
        return std::string_view(this->data(), this->size());
    }

    /// Explicit conversion to std::string.
    constexpr explicit operator std::string() const {
        return std::string(this->data(), this->size());
    }

    // --- Operators ---

    constexpr small_string& operator=(std::string_view rhs) {
        this->assign(rhs);
        return *this;
    }

    constexpr small_string& operator+=(std::string_view rhs) {
        this->append(rhs);
        return *this;
    }

    constexpr small_string& operator+=(char c) {
        this->push_back(c);
        return *this;
    }

    friend constexpr bool operator==(const small_string& lhs, std::string_view rhs) {
        return lhs.ref() == rhs;
    }

    friend constexpr auto operator<=>(const small_string& lhs, std::string_view rhs) {
        return lhs.ref().compare(rhs) <=> 0;
    }
};

}  // namespace kota
