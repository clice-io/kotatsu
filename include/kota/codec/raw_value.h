#pragma once
#include <string>

namespace kota::codec {

struct RawValue {
    std::string data;

    bool empty() const noexcept {
        return data.empty();
    }
};

}  // namespace kota::codec
