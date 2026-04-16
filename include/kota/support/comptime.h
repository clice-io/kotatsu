#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace kota::comptime {

template <size_t reserved>
struct Record {
    size_t count = 0;
    std::array<size_t, reserved> data{};
    bool counting = true;
};

template <size_t reserved>
constexpr inline static auto counting_flag = Record<reserved>{};

template <auto record>
class ComptimeMemoryResource {
public:
    constexpr static bool is_counting = record.counting;
    constexpr static size_t reserved_size = record.data.size();

private:
    using counting_cont = std::allocator<char>;
    using storage_cont = char[record.count + 1];

    alignas(std::max_align_t) std::conditional_t<is_counting, counting_cont, storage_cont> memory{};
    std::array<size_t, reserved_size> reserved{};
    size_t idx = 0;

public:
    constexpr static size_t aligned_offset(size_t current_idx, size_t alignment) {
        return (current_idx + alignment - 1) & ~(alignment - 1);
    }

    constexpr void* allocate(size_t n, size_t alignment) {
        size_t start = aligned_offset(idx, alignment);
        idx = start + n;

        if constexpr(is_counting) {
            return memory.allocate(n);
        } else {
            return &memory[start];
        }
    }

    constexpr void deallocate(void* p, size_t n) {
        if constexpr(is_counting) {
            memory.deallocate(static_cast<char*>(p), n);
        }
    }

    template <typename T>
    constexpr T* allocate_type(size_t count_elements) {
        size_t start = aligned_offset(idx, alignof(T));
        idx = start + count_elements * sizeof(T);

        if constexpr(is_counting) {
            return std::allocator<T>{}.allocate(count_elements);
        } else {
            return static_cast<T*>(&memory[start]);
        }
    }

    template <typename T>
    constexpr void deallocate_type(T* p, size_t count_elements) {
        if constexpr(is_counting) {
            std::allocator<T>{}.deallocate(p, count_elements);
        }
    }

    constexpr size_t used_size() const {
        return idx;
    }

    constexpr void set_reserved(size_t i, size_t value) {
        static_assert(is_counting, "Reserved data can only be set in counting mode");
        assert(i < reserved.size());
        reserved[i] = value;
    }

    template <size_t i>
    constexpr static auto read_reserved() {
        static_assert(i < reserved_size, "Reserved index out of range");
        return record.data[i];
    }

    constexpr static auto read_reserved(size_t i) {
        assert(i < reserved_size);
        return record.data[i];
    }

    consteval auto gen_record() const {
        static_assert(is_counting, "Record can only be generated in counting mode");
        return Record<reserved_size>{idx, reserved, false};
    }

    constexpr ComptimeMemoryResource() = default;
};

template <typename T, auto record>
class ComptimeAllocator {
public:
    using Resource = ComptimeMemoryResource<record>;
    constexpr static bool is_counting = Resource::is_counting;
    using value_type = T;

    template <typename U>
    struct rebind {
        using other = ComptimeAllocator<U, record>;
    };

    Resource* res;

    constexpr ComptimeAllocator(Resource& r) : res(&r) {}

    template <typename U>
    constexpr ComptimeAllocator(const ComptimeAllocator<U, record>& other) : res(other.res) {}

    constexpr T* allocate(std::size_t n) {
        return res->template allocate_type<T>(n);
    }

    constexpr void deallocate(T* p, std::size_t n) {
        res->template deallocate_type<T>(p, n);
    }

    template <typename U>
    constexpr bool operator==(const ComptimeAllocator<U, record>& other) const {
        return res == other.res;
    }
};

template <typename T, typename ResourceTy, size_t reserved_id>
class ComptimeVector {
public:
    static_assert(reserved_id < ResourceTy::reserved_size, "Reserved id out of range");

    using value_type = T;
    using cont_ty =
        std::conditional_t<ResourceTy::is_counting,
                           std::vector<T>,
                           std::array<T, ResourceTy::template read_reserved<reserved_id>()>>;

private:
    constexpr void sync_counting_state() {
        if constexpr(ResourceTy::is_counting) {
            sz = storage.size();
            resource.set_reserved(reserved_id, sz);
        }
    }

    size_t sz;
    ResourceTy& resource;
    cont_ty storage = {};

public:
    constexpr ComptimeVector(ResourceTy& resource) : sz(0), resource(resource) {}

    constexpr size_t size() const {
        return sz;
    }

    constexpr bool empty() const {
        return sz == 0;
    }

    constexpr size_t capacity() const {
        if constexpr(ResourceTy::is_counting) {
            return storage.capacity();
        } else {
            return storage.size();
        }
    }

    constexpr T* data() {
        return storage.data();
    }

    constexpr const T* data() const {
        return storage.data();
    }

    constexpr T* begin() {
        return data();
    }

    constexpr const T* begin() const {
        return data();
    }

    constexpr T* end() {
        return data() + sz;
    }

    constexpr const T* end() const {
        return data() + sz;
    }

    constexpr const T* cbegin() const {
        return begin();
    }

    constexpr const T* cend() const {
        return end();
    }

    constexpr void reserve(size_t n) {
        if constexpr(ResourceTy::is_counting) {
            storage.reserve(n);
        } else {
            assert(n <= storage.size());
        }
    }

    constexpr void push_back(const T& value) {
        if constexpr(ResourceTy::is_counting) {
            storage.push_back(value);
            sync_counting_state();
        } else {
            assert(sz < storage.size());
            storage[sz++] = value;
        }
    }

    constexpr void push_back(T&& value) {
        if constexpr(ResourceTy::is_counting) {
            storage.push_back(std::move(value));
            sync_counting_state();
        } else {
            assert(sz < storage.size());
            storage[sz++] = std::move(value);
        }
    }

    template <typename... Args>
    constexpr T& emplace_back(Args&&... args) {
        if constexpr(ResourceTy::is_counting) {
            storage.emplace_back(std::forward<Args>(args)...);
            sync_counting_state();
            return storage.back();
        } else {
            assert(sz < storage.size());
            storage[sz] = T(std::forward<Args>(args)...);
            ++sz;
            return storage[sz - 1];
        }
    }

    constexpr void pop_back() {
        assert(sz > 0);
        if constexpr(ResourceTy::is_counting) {
            storage.pop_back();
            sync_counting_state();
        } else {
            --sz;
        }
    }

    constexpr T& front() {
        assert(sz > 0);
        return storage[0];
    }

    constexpr const T& front() const {
        assert(sz > 0);
        return storage[0];
    }

    constexpr T& back() {
        assert(sz > 0);
        return storage[sz - 1];
    }

    constexpr const T& back() const {
        assert(sz > 0);
        return storage[sz - 1];
    }

    constexpr const T& operator[](size_t index) const {
        assert(index < sz);
        return storage[index];
    }

    constexpr T& operator[](size_t index) {
        assert(index < sz);
        return storage[index];
    }

    constexpr bool operator==(const ComptimeVector& other) const {
        if(sz != other.sz)
            return false;
        for(size_t i = 0; i < sz; ++i) {
            if(storage[i] != other.storage[i])
                return false;
        }
        return true;
    }

    constexpr void clear() {
        if constexpr(ResourceTy::is_counting) {
            storage.clear();
            sync_counting_state();
        } else {
            sz = 0;
        }
    }
};

}  // namespace kota::comptime
