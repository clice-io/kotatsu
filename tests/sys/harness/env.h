#pragma once

// ScopedEnv, a variable of the environment set or removed until it goes.

#include <optional>
#include <string>
#include <string_view>

#include "kota/zest/zest.h"
#include "kota/sys/env.h"

namespace kota::test {

/// Variable `name` set to `value`, or removed for none, until this goes,
/// which puts back what it was.
struct ScopedEnv {
    ScopedEnv(std::string name, std::optional<std::string_view> value) : name(std::move(name)) {
        if(std::string held; sys::get_env(held, this->name)) {
            previous = std::move(held);
        }
        auto error = value ? sys::set_env(this->name, *value) : sys::unset_env(this->name);
        ZASSERT(!error);
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

    ~ScopedEnv() {
        if(previous) {
            sys::set_env(name, *previous);
        } else {
            sys::unset_env(name);
        }
    }

    std::string name;
    std::optional<std::string> previous;
};

}  // namespace kota::test
