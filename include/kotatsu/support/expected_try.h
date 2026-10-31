#pragma once

// KOTATSU_EXPECTED_TRY(expr)
// For std::expected<void, E> — checks for error and propagates it.
// Usage: KOTATSU_EXPECTED_TRY(d.deserialize_bool(v));
#define KOTATSU_EXPECTED_TRY(expr)                                                                 \
    do {                                                                                           \
        auto _kotatsu_try_result_ = (expr);                                                        \
        if(!_kotatsu_try_result_)                                                                  \
            return std::unexpected(std::move(_kotatsu_try_result_).error());                       \
    } while(0)

#define KOTATSU_CONCAT_IMPL_(a, b) a##b
#define KOTATSU_CONCAT_(a, b) KOTATSU_CONCAT_IMPL_(a, b)

// KOTATSU_EXPECTED_TRY_V(decl, expr)
// For std::expected<T, E> — checks for error, on success declares variable with the value.
// Usage: KOTATSU_EXPECTED_TRY_V(auto val, d.deserialize_seq(len));
#define KOTATSU_EXPECTED_TRY_V(decl, expr)                                                         \
    auto KOTATSU_CONCAT_(_kotatsu_try_tmp_, __LINE__) = (expr);                                    \
    if(!KOTATSU_CONCAT_(_kotatsu_try_tmp_, __LINE__))                                              \
        return std::unexpected(std::move(KOTATSU_CONCAT_(_kotatsu_try_tmp_, __LINE__)).error());   \
    decl = std::move(*KOTATSU_CONCAT_(_kotatsu_try_tmp_, __LINE__))
