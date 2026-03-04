#pragma once

#include <memory>

namespace eventide {

template <typename T>
struct destroy_handle {
    void operator()(T* ptr) const noexcept {
        T::destroy(ptr);
    }
};

template <typename T>
using unique_handle = std::unique_ptr<T, destroy_handle<T>>;

}  // namespace eventide
