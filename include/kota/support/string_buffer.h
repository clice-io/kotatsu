#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace kota {

/// A contiguous, resizable buffer of `Char`: std::basic_string, std::vector,
/// small_string<N>, small_vector<Char, N>, llvm::SmallString<N>.
template <typename Buffer, typename Char = char>
concept string_buffer = requires(Buffer& buffer, std::size_t size) {
    { buffer.data() } -> std::same_as<Char*>;
    { buffer.size() } -> std::convertible_to<std::size_t>;
    buffer.resize(size);
};

/// A string_buffer of any type, by reference: what a function that writes
/// text takes, so that its caller decides where the text goes, and may keep
/// one buffer for many calls. It refers to the buffer as std::string_view
/// refers to a string, and must not outlive it.
template <typename Char>
class BasicStringBufferRef {
public:
    template <string_buffer<Char> Buffer>
    constexpr BasicStringBufferRef(Buffer& buffer) noexcept :
        buffer(std::addressof(buffer)), ops(&ops_of<Buffer>) {}

    /// What the buffer holds.
    constexpr std::basic_string_view<Char> view() const {
        auto chars = ops->chars(buffer);
        return {chars.data(), chars.size()};
    }

    /// What the buffer holds, to write over.
    constexpr std::span<Char> chars() {
        return ops->chars(buffer);
    }

    constexpr std::size_t size() const {
        return view().size();
    }

    constexpr bool empty() const {
        return size() == 0;
    }

    /// Makes the buffer `size` characters long, its first ones kept and the
    /// rest for the caller to write: the characters, which a resize may have
    /// moved.
    constexpr std::span<Char> resize(std::size_t size) {
        return ops->resize(buffer, size);
    }

    /// Makes `text`, which may view the buffer itself, the whole of it.
    constexpr void assign(std::basic_string_view<Char> text) {
        auto held = view();
        // Pointers into other arrays compare only through std::less.
        std::less<const Char*> before;
        if(!before(text.data(), held.data()) && before(text.data(), held.data() + held.size())) {
            auto chars = this->chars();
            std::copy(text.begin(), text.end(), chars.begin());
            resize(text.size());
            return;
        }
        auto chars = resize(text.size());
        std::ranges::copy(text, chars.begin());
    }

    /// Appends `text`, which must not view the buffer.
    constexpr void append(std::basic_string_view<Char> text) {
        auto at = size();
        auto chars = resize(at + text.size());
        std::ranges::copy(text, chars.begin() + static_cast<std::ptrdiff_t>(at));
    }

    constexpr void push_back(Char c) {
        auto at = size();
        resize(at + 1)[at] = c;
    }

    constexpr void clear() {
        resize(0);
    }

private:
    struct Ops {
        std::span<Char> (*chars)(void* buffer);
        std::span<Char> (*resize)(void* buffer, std::size_t size);
    };

    template <typename Buffer>
    constexpr static Ops ops_of = {
        .chars = [](void* buffer) -> std::span<Char> {
            auto& held = *static_cast<Buffer*>(buffer);
            return {held.data(), static_cast<std::size_t>(held.size())};
        },
        .resize = [](void* buffer, std::size_t size) -> std::span<Char> {
            auto& held = *static_cast<Buffer*>(buffer);
            // What the caller writes need not be zeroed first.
            if constexpr(requires { held.resize_for_overwrite(size); }) {
                held.resize_for_overwrite(size);
            } else {
                held.resize(size);
            }
            return {held.data(), static_cast<std::size_t>(held.size())};
        },
    };

    void* buffer;

    const Ops* ops;
};

using StringBufferRef = BasicStringBufferRef<char>;

}  // namespace kota
