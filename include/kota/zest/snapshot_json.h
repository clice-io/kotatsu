#pragma once

#include <print>

#include "kota/zest/macro.h"
#include "kota/codec/json/json.h"

// clang-format off
#define ZEST_SNAPSHOT_JSON_IMPL(return_action, value, ...)                                         \
    do {                                                                                           \
        auto _zest_snap_json = ::kota::codec::json::to_json(value);                                \
        if(!_zest_snap_json.has_value()) {                                                         \
            std::println("[snapshot] json serialization failed");                                   \
            ::kota::zest::print_trace(std::source_location::current());                            \
            ::kota::zest::failure();                                                                \
            return_action;                                                                          \
        }                                                                                           \
        auto _zest_snap_pretty = ::kota::codec::json::prettify(*_zest_snap_json);                   \
        if(!_zest_snap_pretty.has_value()) {                                                        \
            std::println("[snapshot] json prettify failed");                                        \
            ::kota::zest::print_trace(std::source_location::current());                            \
            ::kota::zest::failure();                                                                \
            return_action;                                                                          \
        }                                                                                           \
        ZEST_SNAPSHOT_STR_IMPL(return_action, *_zest_snap_pretty __VA_OPT__(, __VA_ARGS__));        \
    } while(0)

#define EXPECT_SNAPSHOT_JSON(value, ...) ZEST_SNAPSHOT_JSON_IMPL((void)0, value __VA_OPT__(,) __VA_ARGS__)
#define ASSERT_SNAPSHOT_JSON(value, ...) ZEST_SNAPSHOT_JSON_IMPL(return, value __VA_OPT__(,) __VA_ARGS__)
#define CO_ASSERT_SNAPSHOT_JSON(value, ...) ZEST_SNAPSHOT_JSON_IMPL(co_return, value __VA_OPT__(,) __VA_ARGS__)
// clang-format on
