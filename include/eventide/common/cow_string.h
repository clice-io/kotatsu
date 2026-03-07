#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

#include "memory.h"
#include "small_string.h"
#include "string_ref.h"

namespace eventide {

/// A copy-on-write string that can either borrow a reference to external data
/// or own its own heap-allocated copy. Designed for zero-copy deserialization:
/// unescaped strings borrow from the source buffer, while escaped strings
/// allocate and own their data.
///
/// Layout: three machine words {pointer, size, capacity}.
///   - capacity == 0: borrowed mode, pointer refers to external buffer
///   - capacity > 0: owned mode, pointer refers to allocator-managed memory
///
/// Owned buffers are allocated via mem::allocate<char> and can be transferred
/// to small_string via release().
class cow_string {
public:
    /// Default constructor: empty borrowed string.
    constexpr cow_string() noexcept = default;

    /// Construct a borrowed string from a string_ref.
    constexpr cow_string(string_ref sv) noexcept :
        m_data(const_cast<char*>(sv.data())), m_size(sv.size()), m_capacity(0) {}

    /// Copy constructor: borrowed copies as borrowed, owned deep-copies.
    constexpr cow_string(const cow_string& other) :
        m_data(other.m_data), m_size(other.m_size), m_capacity(0) {
        if(other.m_capacity > 0) {
            m_data = alloc_copy(other.m_data, other.m_size, other.m_capacity);
            m_capacity = other.m_capacity;
        }
    }

    /// Move constructor: transfers ownership, source becomes empty.
    constexpr cow_string(cow_string&& other) noexcept :
        m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity) {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    /// Copy assignment.
    constexpr cow_string& operator=(const cow_string& other) {
        if(this != &other) {
            cow_string tmp(other);
            swap(tmp);
        }
        return *this;
    }

    /// Move assignment.
    constexpr cow_string& operator=(cow_string&& other) noexcept {
        if(this != &other) {
            free();
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    constexpr ~cow_string() {
        free();
    }

    // --- Named constructors ---

    /// Explicitly create a borrowed string referencing external data.
    [[nodiscard]] constexpr static cow_string borrowed(string_ref sv) noexcept {
        return cow_string(sv);
    }

    /// Create an owned string by taking ownership of an std::string.
    [[nodiscard]] constexpr static cow_string owned(std::string&& s) {
        cow_string result;
        if(!s.empty()) {
            result.m_size = s.size();
            result.m_capacity = s.size();
            result.m_data = alloc_copy(s.data(), s.size(), s.size());
        }
        return result;
    }

    /// Create an owned string by copying from a string_ref.
    [[nodiscard]] constexpr static cow_string owned(string_ref sv) {
        cow_string result;
        if(!sv.empty()) {
            result.m_size = sv.size();
            result.m_capacity = sv.size();
            result.m_data = alloc_copy(sv.data(), sv.size(), sv.size());
        }
        return result;
    }

    // --- Observers ---

    [[nodiscard]] constexpr const char* data() const noexcept {
        return m_data;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return m_size;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return m_size == 0;
    }

    [[nodiscard]] constexpr bool is_borrowed() const noexcept {
        return m_capacity == 0;
    }

    [[nodiscard]] constexpr bool is_owned() const noexcept {
        return m_capacity > 0;
    }

    // --- Conversion ---

    /// Return a string_ref view of this string.
    [[nodiscard]] constexpr string_ref ref() const noexcept {
        return string_ref(m_data, m_size);
    }

    /// Implicit conversion to string_ref.
    constexpr operator string_ref() const noexcept {
        return ref();
    }

    /// Implicit conversion to std::string_view.
    constexpr operator std::string_view() const noexcept {
        return std::string_view(m_data, m_size);
    }

    /// Convert to std::string.
    [[nodiscard]] constexpr std::string to_string() const {
        return std::string(m_data, m_size);
    }

    // --- Mutation ---

    /// Convert this string to owned mode (no-op if already owned).
    constexpr void make_owned() {
        if(m_capacity == 0 && m_size > 0) {
            m_data = alloc_copy(m_data, m_size, m_size);
            m_capacity = m_size;
        }
    }

    /// Release the buffer as a small_string<N>, transferring ownership.
    /// If owned, the buffer is moved directly without copying.
    /// If borrowed and fits in inline storage, copies to inline buffer
    /// without heap allocation; otherwise allocates.
    /// After this call, the cow_string is empty.
    template <unsigned N = 0>
    [[nodiscard]] constexpr small_string<N> release() {
        if(m_capacity == 0) {
            // Borrowed: construct small_string from the view directly.
            // If m_size <= N, small_string will use its inline buffer (no heap alloc).
            small_string<N> result(string_ref(m_data, m_size));
            m_data = nullptr;
            m_size = 0;
            return result;
        }
        // Owned: transfer the buffer directly.
        auto result = small_string<N>::from_raw_parts(m_data, m_size, m_capacity);
        m_data = nullptr;
        m_size = 0;
        m_capacity = 0;
        return result;
    }

    constexpr void swap(cow_string& other) noexcept {
        std::swap(m_data, other.m_data);
        std::swap(m_size, other.m_size);
        std::swap(m_capacity, other.m_capacity);
    }

    // --- Comparison ---

    friend constexpr bool operator==(const cow_string& lhs, const cow_string& rhs) noexcept {
        return lhs.ref() == rhs.ref();
    }

    friend constexpr bool operator==(const cow_string& lhs, string_ref rhs) noexcept {
        return lhs.ref() == rhs;
    }

    friend constexpr bool operator==(const cow_string& lhs, const char* rhs) noexcept {
        return lhs.ref() == string_ref(rhs);
    }

private:
    constexpr void free() noexcept {
        if(m_capacity > 0) {
            mem::deallocate(m_data, m_capacity);
        }
    }

    constexpr static char* alloc_copy(const char* src, std::size_t len, std::size_t cap) {
        char* buf = mem::allocate<char>(cap);
        for(std::size_t i = 0; i < len; ++i) {
            buf[i] = src[i];
        }
        return buf;
    }

    char* m_data = nullptr;
    std::size_t m_size = 0;
    std::size_t m_capacity = 0;
};

}  // namespace eventide
