#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace kota {

template <std::size_t N>
struct fixed_string : std::array<char, N + 1> {
    constexpr fixed_string(const char* str) {
        for(std::size_t i = 0; i < N; ++i) {
            this->data()[i] = str[i];
        }
        this->data()[N] = '\0';
    }

    template <std::size_t M>
    constexpr fixed_string(const char (&str)[M]) {
        for(std::size_t i = 0; i < N; ++i) {
            this->data()[i] = str[i];
        }
        this->data()[N] = '\0';
    }

    constexpr static auto size() {
        return N;
    }

    constexpr operator std::string_view() const {
        return std::string_view(this->data(), N);
    }
};

template <std::size_t M>
fixed_string(const char (&)[M]) -> fixed_string<M - 1>;

}  // namespace kota
