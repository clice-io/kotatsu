#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "kota/support/config.h"

namespace kota::detail {

template <std::size_t MaxN>
struct unique_lengths_result {
    std::array<std::size_t, MaxN> values{};
    std::size_t count = 0;
};

template <std::size_t N>
consteval auto compute_unique_lengths(const std::array<std::string_view, N>& names) {
    unique_lengths_result<N> result{};
    for(std::size_t i = 0; i < N; ++i) {
        auto len = names[i].size();
        bool found = false;
        for(std::size_t j = 0; j < result.count; ++j) {
            if(result.values[j] == len) {
                found = true;
                break;
            }
        }
        if(!found) {
            result.values[result.count++] = len;
        }
    }
    for(std::size_t i = 0; i < result.count; ++i) {
        for(std::size_t j = i + 1; j < result.count; ++j) {
            if(result.values[i] > result.values[j]) {
                auto tmp = result.values[i];
                result.values[i] = result.values[j];
                result.values[j] = tmp;
            }
        }
    }
    return result;
}

template <const auto& Names, std::size_t TargetLen, std::size_t... Is>
KOTA_ALWAYS_INLINE constexpr auto match_in_length_group(std::string_view key, std::index_sequence<Is...>)
    -> std::optional<std::size_t> {
    std::optional<std::size_t> result;
    (([&] -> bool {
         if constexpr(Names[Is].size() == TargetLen) {
             if(key == Names[Is]) {
                 result = Is;
                 return true;
             }
         }
         return false;
     }()) ||
     ...);
    return result;
}

}  // namespace kota::detail

namespace kota {

template <const auto& Names>
KOTA_ALWAYS_INLINE constexpr auto string_match(std::string_view key) -> std::optional<std::size_t> {
    constexpr std::size_t N = Names.size();
    if constexpr(N == 0) {
        return std::nullopt;
    } else {
        constexpr auto lengths = detail::compute_unique_lengths(Names);

        return [&]<std::size_t... Ls>(std::index_sequence<Ls...>) -> std::optional<std::size_t> {
            std::optional<std::size_t> result;
            (([&] -> bool {
                 if constexpr(Ls < lengths.count) {
                     if(key.size() == lengths.values[Ls]) {
                         result = detail::match_in_length_group<Names, lengths.values[Ls]>(
                             key,
                             std::make_index_sequence<N>{});
                         return true;
                     }
                 }
                 return false;
             }()) ||
             ...);
            return result;
        }(std::make_index_sequence<N>{});
    }
}

}  // namespace kota
