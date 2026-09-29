#pragma once

#include <cstddef>
#include <string>
#include <utility>

#include "kota/support/memory.h"
#include "kota/support/small_string.h"
#include "kota/support/string_ref.h"

namespace kota {

/// A copy-on-write string that can either borrow a reference to external data
/// or own its own heap-allocated copy. Designed for zero-copy deserialization:
/// unescaped strings borrow from the source buffer, while escaped strings
/// allocate and own their data.
///
/// Layout: three machine words {pointer, size, capacity}.
///   - allocated == 0: borrowed mode, pointer refers to external buffer
///   - allocated > 0: owned mode, pointer refers to allocator-managed memory
///
/// Owned buffers are allocated via mem::allocate<char> and can be transferred
/// to small_string via release().
class cow_string {
public:
    /// Default constructor: empty borrowed string.
    constexpr cow_string() noexcept = default;

    /// Construct a borrowed string from a string_ref.
    constexpr cow_string(string_ref sv) noexcept :
        text(const_cast<char*>(sv.data())), length(sv.size()), allocated(0) {}

    /// Copy constructor: borrowed copies as borrowed, owned deep-copies.
    constexpr cow_string(const cow_string& other) :
        text(other.text), length(other.length), allocated(0) {
        if(other.allocated > 0) {
            text = alloc_copy(other.text, other.length, other.allocated);
            allocated = other.allocated;
        }
    }

    /// Move constructor: transfers ownership, source becomes empty.
    constexpr cow_string(cow_string&& other) noexcept :
        text(other.text), length(other.length), allocated(other.allocated) {
        other.text = nullptr;
        other.length = 0;
        other.allocated = 0;
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
            text = other.text;
            length = other.length;
            allocated = other.allocated;
            other.text = nullptr;
            other.length = 0;
            other.allocated = 0;
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

    /// Create an owned string by copying from a string_ref; an empty one stays borrowed.
    [[nodiscard]] constexpr static cow_string owned(string_ref sv) {
        cow_string result;
        if(!sv.empty()) {
            result.length = sv.size();
            result.allocated = sv.size();
            result.text = alloc_copy(sv.data(), sv.size(), sv.size());
        }
        return result;
    }

    // --- Observers ---

    [[nodiscard]] constexpr const char* data() const noexcept {
        return text;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return length;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return length == 0;
    }

    [[nodiscard]] constexpr bool is_borrowed() const noexcept {
        return allocated == 0;
    }

    [[nodiscard]] constexpr bool is_owned() const noexcept {
        return allocated > 0;
    }

    // --- Conversion ---

    /// Return a string_ref view of this string.
    [[nodiscard]] constexpr string_ref ref() const noexcept {
        return string_ref(text, length);
    }

    /// Implicit conversion to string_ref.
    constexpr operator string_ref() const noexcept {
        return ref();
    }

    /// Implicit conversion to std::string_view.
    constexpr operator std::string_view() const noexcept {
        return std::string_view(text, length);
    }

    /// Convert to std::string.
    [[nodiscard]] constexpr std::string to_string() const {
        return std::string(text, length);
    }

    // --- Mutation ---

    /// Convert this string to owned mode (no-op if already owned).
    constexpr void make_owned() {
        if(allocated == 0 && length > 0) {
            text = alloc_copy(text, length, length);
            allocated = length;
        }
    }

    /// Release the buffer as a small_string<N>, transferring ownership.
    /// If owned, the buffer is moved directly without copying.
    /// If borrowed and it fits the inline buffer, copies into it
    /// without heap allocation; otherwise allocates.
    /// After this call, the cow_string is empty.
    template <unsigned N = 0>
    [[nodiscard]] constexpr small_string<N> release() {
        if(allocated == 0) {
            // Borrowed: construct small_string from the view directly.
            // If length <= N, small_string will use its inline buffer (no heap alloc).
            small_string<N> result(string_ref(text, length));
            text = nullptr;
            length = 0;
            return result;
        }
        // Owned: transfer the buffer directly.
        auto result = small_string<N>::from_raw_parts(text, length, allocated);
        text = nullptr;
        length = 0;
        allocated = 0;
        return result;
    }

    constexpr void swap(cow_string& other) noexcept {
        std::swap(text, other.text);
        std::swap(length, other.length);
        std::swap(allocated, other.allocated);
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
        if(allocated > 0) {
            mem::deallocate(text, allocated);
        }
    }

    constexpr static char* alloc_copy(const char* src, std::size_t len, std::size_t cap) {
        char* buf = mem::allocate<char>(cap);
        if(len > 0) {
            mem::uninitialized_copy(std::ranges::subrange(src, src + len), buf);
        }
        return buf;
    }

    char* text = nullptr;
    std::size_t length = 0;
    std::size_t allocated = 0;
};

}  // namespace kota
