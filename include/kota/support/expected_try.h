#pragma once

// KOTA_EXPECTED_TRY(expr)
// For std::expected<void, E> — checks for error and propagates it.
// Usage: KOTA_EXPECTED_TRY(d.deserialize_bool(v));
#define KOTA_EXPECTED_TRY(expr)                                                                    \
    do {                                                                                           \
        auto _kota_try_result_ = (expr);                                                           \
        if(!_kota_try_result_)                                                                     \
            return std::unexpected(std::move(_kota_try_result_).error());                          \
    } while(0)

#define KOTA_CONCAT_IMPL_(a, b) a##b
#define KOTA_CONCAT_(a, b) KOTA_CONCAT_IMPL_(a, b)

// KOTA_EXPECTED_TRY_V(decl, expr)
// For std::expected<T, E> — checks for error, on success declares variable with the value.
// Usage: KOTA_EXPECTED_TRY_V(auto val, d.deserialize_seq(len));
#define KOTA_EXPECTED_TRY_V(decl, expr)                                                            \
    auto KOTA_CONCAT_(_kota_try_tmp_, __LINE__) = (expr);                                          \
    if(!KOTA_CONCAT_(_kota_try_tmp_, __LINE__))                                                    \
        return std::unexpected(std::move(KOTA_CONCAT_(_kota_try_tmp_, __LINE__)).error());         \
    decl = std::move(*KOTA_CONCAT_(_kota_try_tmp_, __LINE__))
