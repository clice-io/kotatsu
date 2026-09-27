#include "ring_buffer.h"

#include <algorithm>
#include <cassert>

namespace kota {

std::span<const char> ring_buffer::readable() const noexcept {
    return {data.data() + head, std::min(size, capacity - head)};
}

void ring_buffer::consume(std::size_t n) noexcept {
    assert(n <= size && "consume() past the unread bytes");
    size -= n;
    head = (head + n) % capacity;
}

std::span<char> ring_buffer::writable() {
    assert(!full() && "writable() on a full ring");
    if(data.empty()) {
        data.resize(capacity);
    }
    // Starting over at the front when the ring is empty gives the read all
    // the room in one piece. Only here: libuv may fill the room it was given
    // loop turns later (a Windows console reading a line), so what lies
    // there must not move until commit().
    if(empty()) {
        head = 0;
    }
    // Past the unread bytes up to the end, or, once they wrap, up to them.
    const auto tail = (head + size) % capacity;
    return {data.data() + tail, tail < head ? head - tail : capacity - tail};
}

void ring_buffer::commit(std::size_t n) noexcept {
    assert(n <= capacity - size && "commit() past the room writable() gave");
    size += n;
}

}  // namespace kota
