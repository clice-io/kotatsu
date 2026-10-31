#pragma once

#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kotatsu/support/scope_exit.h"
#include "kotatsu/support/string_list_ref.h"

namespace kotatsu {

/// Strings owned in one buffer, each followed by a NUL, with the pointers to
/// them that a C function takes for argv. As a vector's push_back moves its
/// elements, a push may move every string: a view or pointer into the list
/// holds until the next push.
class string_list {
public:
    using iterator = string_list_ref::iterator;

    string_list() = default;

    /// The strings of `strings`, of anything a std::string_view converts from.
    template <std::ranges::input_range Range>
        requires (!std::is_same_v<std::remove_cvref_t<Range>, string_list>) &&
                 std::convertible_to<std::ranges::range_reference_t<Range>, std::string_view>
    explicit string_list(Range&& strings) {
        for(auto&& string: strings) {
            push_back(string);
        }
    }

    string_list(const string_list& other) :
        buffer(other.buffer), starts(other.starts), pointers(other.pointers.size()) {
        rebase();
    }

    string_list(string_list&& other) noexcept :
        buffer(std::move(other.buffer)), starts(std::move(other.starts)),
        pointers(std::move(other.pointers)) {
        rebase();
        other.reset();
    }

    string_list& operator=(const string_list& other) {
        return *this = string_list(other);
    }

    string_list& operator=(string_list&& other) noexcept {
        buffer = std::move(other.buffer);
        starts = std::move(other.starts);
        pointers = std::move(other.pointers);
        rebase();
        other.reset();
        return *this;
    }

    void push_back(std::string_view string) {
        // Room first, so that a failed allocation leaves the list as it was.
        starts.reserve(starts.size() + 1);
        pointers.reserve(starts.size() + 2);
        auto start = buffer.size();
        auto capacity = buffer.capacity();
        // Into the string, which takes `string` even if it views the list.
        buffer.append(string);
        ScopeExit undo([&] { buffer.resize(start); });
        buffer.push_back('\0');
        undo.release();
        starts.push_back(start);
        if(pointers.empty()) {
            pointers.push_back(nullptr);
        }
        pointers.push_back(nullptr);
        if(buffer.capacity() == capacity) {
            pointers[starts.size() - 1] = buffer.data() + start;
        } else {
            rebase();
        }
    }

    std::size_t size() const noexcept {
        return starts.size();
    }

    bool empty() const noexcept {
        return starts.empty();
    }

    std::string_view operator[](std::size_t index) const {
        auto end = index + 1 < starts.size() ? starts[index + 1] : buffer.size();
        return {buffer.data() + starts[index], end - starts[index] - 1};
    }

    iterator begin() const noexcept {
        return string_list_ref(*this).begin();
    }

    iterator end() const noexcept {
        return string_list_ref(*this).end();
    }

    /// The pointers to the strings, followed by a null: argv.
    std::span<const char* const> c_strs() const noexcept {
        constexpr const static char* none = nullptr;
        return pointers.empty() ? std::span(&none, 0) : std::span(pointers.data(), starts.size());
    }

    operator string_list_ref() const noexcept {
        return string_list_ref(this, starts.size(), &element);
    }

private:
    static std::string_view element(const void* list, std::size_t index) {
        return (*static_cast<const string_list*>(list))[index];
    }

    /// Points `pointers` into `buffer` again, which has moved.
    void rebase() noexcept {
        for(std::size_t i = 0; i < starts.size(); ++i) {
            pointers[i] = buffer.data() + starts[i];
        }
    }

    /// Leaves the list empty, as a move leaves the one it moves from.
    void reset() noexcept {
        buffer.clear();
        starts.clear();
        pointers.clear();
    }

    /// The strings, each followed by a NUL.
    std::string buffer;

    /// Where each string starts in `buffer`.
    std::vector<std::size_t> starts;

    /// Where each string is, and a null after them; empty with no strings.
    std::vector<const char*> pointers;
};

}  // namespace kotatsu
