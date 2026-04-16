#pragma once

#include <cstddef>
#include <vector>

namespace kota {

class ring_buffer {
public:
    explicit ring_buffer(size_t cap = 64 * 1024) : data(cap) {}

    size_t readable_bytes() const {
        return size;
    }

    size_t writable_bytes() const {
        return data.size() - size;
    }

    size_t read(char* dest, size_t len);

    std::pair<const char*, size_t> get_read_ptr() const;
    void advance_read(size_t len);

    std::pair<char*, size_t> get_write_ptr();
    void advance_write(size_t len);

private:
    std::vector<char> data;
    size_t head = 0;
    size_t tail = 0;
    size_t size = 0;
};

}  // namespace kota
