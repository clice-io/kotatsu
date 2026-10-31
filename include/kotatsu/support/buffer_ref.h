#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace kotatsu {

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
/// refers to a string, and must not outlive it. What a function that fails
/// leaves in the buffer is unspecified, and what else it takes must not view
/// the buffer, unless it says.
template <typename Char>
class basic_buffer_ref {
public:
    template <string_buffer<Char> Buffer>
    basic_buffer_ref(Buffer& buffer) noexcept :
        buffer(std::addressof(buffer)), access(&access_of<Buffer>) {}

    /// What the buffer holds.
    std::basic_string_view<Char> view() const {
        auto chars = access(buffer, unchanged);
        return {chars.data(), chars.size()};
    }

    /// What the buffer holds, to write over.
    std::span<Char> chars() {
        return access(buffer, unchanged);
    }

    std::size_t size() const {
        return view().size();
    }

    bool empty() const {
        return size() == 0;
    }

    /// Makes the buffer `size` characters long, its first ones kept and the
    /// rest for the caller to write: the characters, which a resize may have
    /// moved.
    std::span<Char> resize(std::size_t size) {
        return access(buffer, size);
    }

    /// Makes `text`, which may view the buffer itself, the whole of it.
    void assign(std::basic_string_view<Char> text) {
        auto held = view();
        // Pointers into other arrays compare only through std::less.
        std::less<const Char*> before;
        if(!before(text.data(), held.data()) && before(text.data(), held.data() + held.size())) {
            std::char_traits<Char>::move(chars().data(), text.data(), text.size());
            resize(text.size());
            return;
        }
        auto chars = resize(text.size());
        std::ranges::copy(text, chars.begin());
    }

    /// Appends `text`, which must not view the buffer.
    void append(std::basic_string_view<Char> text) {
        auto at = size();
        auto chars = resize(at + text.size());
        std::ranges::copy(text, chars.begin() + static_cast<std::ptrdiff_t>(at));
    }

    void push_back(Char c) {
        auto at = size();
        resize(at + 1)[at] = c;
    }

    void clear() {
        resize(0);
    }

private:
    /// What access_of() takes for a size to leave the buffer as it is.
    constexpr static std::size_t unchanged = static_cast<std::size_t>(-1);

    /// The characters of the buffer, made `size` long first unless `size` is
    /// `unchanged`.
    template <typename Buffer>
    static std::span<Char> access_of(void* buffer, std::size_t size) {
        auto& text = *static_cast<Buffer*>(buffer);
        if(size != unchanged) {
            // What the caller writes need not be zeroed first.
            if constexpr(requires { text.resize_for_overwrite(size); }) {
                text.resize_for_overwrite(size);
            } else {
                text.resize(size);
            }
        }
        return {text.data(), static_cast<std::size_t>(text.size())};
    }

    void* buffer;

    std::span<Char> (*access)(void* buffer, std::size_t size);
};

using buffer_ref = basic_buffer_ref<char>;

}  // namespace kotatsu
