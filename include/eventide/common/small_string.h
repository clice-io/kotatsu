#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "small_vector.h"
#include "string_ref.h"

namespace eventide {

/// A SmallVector<char> with string-like convenience methods.
/// All string operations delegate to string_ref.
template <unsigned InlineCapacity>
class small_string : public small_vector<char, InlineCapacity> {
    using base = small_vector<char, InlineCapacity>;

public:
    /// Default ctor - Initialize to empty.
    small_string() = default;

    /// Initialize from a string_view.
    small_string(std::string_view s) : base(s) {}

    /// Initialize by concatenating a list of string_views.
    small_string(std::initializer_list<std::string_view> refs) : base() {
        this->append(refs);
    }

    /// Adopt a pre-allocated buffer as a small_string.
    /// The buffer must have been allocated with mem::allocate<char>.
    [[nodiscard]] static small_string from_raw_parts(char* data,
                                                     std::size_t count,
                                                     std::size_t capacity) {
        small_string result;
        if(data != nullptr && capacity > 0) {
            result.adopt_allocation(data, count, capacity);
        }
        return result;
    }

    // --- String Assignment ---

    using base::assign;

    /// Assign from a string_view.
    void assign(std::string_view rhs) {
        base::assign(rhs);
    }

    /// Assign from a list of string_views.
    void assign(std::initializer_list<std::string_view> refs) {
        this->clear();
        append(refs);
    }

    // --- String Concatenation ---

    using base::append;

    /// Append from a string_view.
    void append(std::string_view rhs) {
        base::append(rhs);
    }

    /// Append from a list of string_views.
    void append(std::initializer_list<std::string_view> refs) {
        std::size_t current_size = this->size();
        std::size_t size_needed = current_size;
        for(const std::string_view& ref: refs) {
            size_needed += ref.size();
        }
        this->resize_for_overwrite(size_needed);
        for(const std::string_view& ref: refs) {
            std::copy(ref.begin(), ref.end(), this->begin() + current_size);
            current_size += ref.size();
        }
    }

    // --- Conversion ---

    /// Get a string_ref view of this string.
    [[nodiscard]] string_ref ref() const {
        return string_ref(this->data(), this->size());
    }

    /// Get a null-terminated C string.
    const char* c_str() {
        this->push_back(0);
        this->pop_back();
        return this->data();
    }

    /// Implicit conversion to string_ref.
    operator string_ref() const {
        return ref();
    }

    /// Implicit conversion to string_view.
    operator std::string_view() const {
        return std::string_view(this->data(), this->size());
    }

    /// Explicit conversion to std::string.
    explicit operator std::string() const {
        return std::string(this->data(), this->size());
    }

    // --- Operators ---

    small_string& operator=(std::string_view rhs) {
        this->assign(rhs);
        return *this;
    }

    small_string& operator+=(std::string_view rhs) {
        this->append(rhs);
        return *this;
    }

    small_string& operator+=(char c) {
        this->push_back(c);
        return *this;
    }

    friend bool operator==(const small_string& lhs, std::string_view rhs) {
        return lhs.ref() == rhs;
    }

    friend auto operator<=>(const small_string& lhs, std::string_view rhs) {
        return lhs.ref().compare(rhs) <=> 0;
    }
};

}  // namespace eventide
