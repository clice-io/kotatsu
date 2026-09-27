#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace kota {

/// The bytes a stream has read and its reader has not consumed yet, in a
/// fixed ring allocated by the first read.
class ring_buffer {
public:
    constexpr static std::size_t capacity = 64 * 1024;

    bool empty() const noexcept {
        return size == 0;
    }

    bool full() const noexcept {
        return size == capacity;
    }

    /// The oldest unread bytes that lie in one piece.
    std::span<const char> readable() const noexcept;

    /// Drops the oldest `n` unread bytes.
    void consume(std::size_t n) noexcept;

    /// The room the next read fills, in one piece; the ring must not be
    /// full.
    std::span<char> writable();

    /// Adds the `n` bytes a read put at the front of writable().
    void commit(std::size_t n) noexcept;

private:
    std::vector<char> data;
    /// Where the unread bytes start; back at 0 whenever the ring empties,
    /// so that a read after a drain gets all the room in one piece.
    std::size_t head = 0;
    std::size_t size = 0;
};

}  // namespace kota
