#pragma once

// The params and results the ipc tests send, with their method traits.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "kota/ipc/protocol.h"
#include "kota/meta/repr.h"
#include "kota/codec/visit/context.h"

namespace kota::test {

/// test/add: answered with the sum.
struct AddParams {
    std::int64_t a = 0;
    std::int64_t b = 0;
};

struct AddResult {
    std::int64_t sum = 0;
};

/// test/note.
struct NoteParams {
    std::string text;
};

/// Params with no fields, which any codec writes as nothing or as an empty
/// object.
struct EmptyParams {};

/// Params of a request and a notification that take none: their traits say
/// so, and they are sent without params.
struct NoParams {};

/// A value no codec can write: encoding it fails.
struct Unwritable {};

}  // namespace kota::test

namespace kota::ipc::protocol {

template <>
struct RequestTraits<test::AddParams> {
    using Result = test::AddResult;
    constexpr inline static std::string_view method = "test/add";
};

template <>
struct NotificationTraits<test::NoteParams> {
    constexpr inline static std::string_view method = "test/note";
};

template <>
struct RequestTraits<test::NoParams> {
    using Result = std::nullptr_t;
    constexpr inline static std::string_view method = "test/none";
    constexpr inline static bool takes_params = false;
};

template <>
struct NotificationTraits<test::NoParams> {
    constexpr inline static std::string_view method = "test/none";
    constexpr inline static bool takes_params = false;
};

}  // namespace kota::ipc::protocol

namespace kota::meta {

template <>
struct repr<test::Unwritable> {
    using type = std::string;

    template <typename Config>
    static bool serialize(auto& /*vis*/, const test::Unwritable& /*value*/) {
        return codec::scoped_context<codec::rich_error>::fail(codec::rich_error("unwritable"));
    }
};

}  // namespace kota::meta
