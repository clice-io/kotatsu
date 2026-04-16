#pragma once
#include <string>

namespace eventide::serde {

struct RawValue {
    std::string data;

    bool empty() const noexcept {
        return data.empty();
    }
};

}  // namespace eventide::serde
