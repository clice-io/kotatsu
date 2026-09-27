#pragma once

#include <memory>

namespace kota::detail {

/// Frees the state behind a resource through `T::destroy`.
template <typename T>
struct destroy_handle {
    void operator()(T* ptr) const noexcept {
        T::destroy(ptr);
    }
};

/// The state behind a resource, which `T::destroy` frees.
template <typename T>
using unique_handle = std::unique_ptr<T, destroy_handle<T>>;

}  // namespace kota::detail
