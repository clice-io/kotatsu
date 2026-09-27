#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kota/support/numeric.h"
#include "kota/meta/type_kind.h"
#include "kota/codec/dyn/document.h"
#include "kota/codec/visit/common.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/decode.h"
#include "kota/codec/visit/map_key.h"

namespace kota::codec::dyn {

struct ValueReader {
    const Value& node;
    constexpr static bool data_driven = true;
    constexpr static bool human_readable = true;
    using error_type = rich_error;

    bool visit_bool(bool& out) {
        auto val = node.get_bool();
        if(!val) {
            return fail_type("boolean");
        }
        out = *val;
        return true;
    }

    /// Either kind of integer node reads into a signed or an unsigned `out`;
    /// only a value outside `out`'s range fails.
    template <typename T>
    bool visit_int(T& out) {
        bool fits = false;
        if(const auto* signed_value = std::get_if<std::int64_t>(&node.variant())) {
            fits = kota::narrow_int(*signed_value, out);
        } else if(const auto* unsigned_value = std::get_if<std::uint64_t>(&node.variant())) {
            fits = kota::narrow_int(*unsigned_value, out);
        } else {
            return fail_type("integer");
        }
        if(!fits) {
            return scoped_context<rich_error>::fail(rich_error("integer value out of range"));
        }
        return true;
    }

    template <typename T>
    bool visit_uint(T& out) {
        return visit_int(out);
    }

    template <typename T>
    bool visit_float(T& out) {
        auto val = node.get_double();
        if(!val) {
            return fail_type("float");
        }
        out = static_cast<T>(*val);
        return true;
    }

    template <typename T>
    bool visit_str(T& out) {
        auto val = node.get_string();
        if(!val) {
            return fail_type("string");
        }
        out = T(*val);
        return true;
    }

    template <typename T>
    bool visit_char(T& out) {
        auto val = node.get_string();
        if(!val) {
            return fail_type("string");
        }
        auto c = char_from_utf8(*val);
        if(!c) {
            return scoped_context<rich_error>::fail(rich_error(std::string(invalid_char_message)));
        }
        out = *c;
        return true;
    }

    template <typename T>
    bool visit_bytes(T& out) {
        const auto* arr = node.get_array();
        if(!arr) {
            return fail_type("array");
        }
        auto size = arr->size();
        out.clear();
        out.reserve(size);
        for(std::size_t i = 0; i < size; ++i) {
            const auto& elem = (*arr)[i];
            auto val = elem.get_uint();
            if(!val || *val > 255) {
                return scoped_context<rich_error>::fail(
                    rich_error("byte array element out of range [0, 255]"));
            }
            out.push_back(static_cast<typename T::value_type>(static_cast<std::uint8_t>(*val)));
        }
        return true;
    }

    bool peek_null() {
        return node.is_null();
    }

    bool visit_null() {
        if(peek_null()) {
            return true;
        }
        return fail_type("null");
    }

    meta::type_kind peek_kind() {
        switch(node.kind()) {
            case ValueKind::null_value: return meta::type_kind::null;
            case ValueKind::boolean: return meta::type_kind::boolean;
            case ValueKind::signed_int: return meta::type_kind::int64;
            case ValueKind::unsigned_int: return meta::type_kind::uint64;
            case ValueKind::floating: return meta::type_kind::float64;
            case ValueKind::string: return meta::type_kind::string;
            case ValueKind::array: return meta::type_kind::array;
            case ValueKind::object: return meta::type_kind::structure;
        }
        return meta::type_kind::unknown;
    }

    template <typename F>
    bool try_read(F&& fn) {
        error_type discard_err;
        scoped_context<error_type> guard(discard_err);
        ValueReader fork{node};
        return fn(fork);
    }

    bool visit_skip() {
        return true;
    }

    template <typename Callback>
    bool visit_struct(Callback&& cb) {
        const auto* obj = node.get_object();
        if(!obj) {
            return fail_type("object");
        }
        for(const auto& [k, v]: *obj) {
            ValueReader sub{v};
            KOTA_CODEC_TRY(cb(std::string_view(k), sub));
        }
        return true;
    }

    template <typename Callback>
    bool visit_seq(Callback&& cb) {
        const auto* arr = node.get_array();
        if(!arr) {
            return fail_type("array");
        }
        for(const auto& element: *arr) {
            ValueReader sub{element};
            KOTA_CODEC_TRY(cb(sub));
        }
        return true;
    }

    /// A tuple is an array, read as a sequence.
    template <typename Callback>
    bool visit_tuple(Callback&& cb) {
        return visit_seq(std::forward<Callback>(cb));
    }

    template <typename Callback>
    bool visit_map(Callback&& cb) {
        const auto* obj = node.get_object();
        if(!obj) {
            return fail_type("object");
        }
        for(const auto& [k, v]: *obj) {
            MapKeyReader<> kr{std::string_view(k)};
            ValueReader vr{v};
            KOTA_CODEC_TRY(cb(kr, vr));
        }
        return true;
    }

private:
    bool fail_type(std::string_view expected) {
        return scoped_context<rich_error>::fail(
            rich_error::invalid_type(expected, dyn::detail::kind_name(node.kind())));
    }
};

/// Decodes a dyn::Value DOM tree into `out` (or, in the value-returning
/// overload, into a default-constructed T).
template <typename Config = void, typename T>
auto from_dyn(const Value& value, T& out) -> std::expected<void, rich_error> {
    rich_error err;
    scoped_context<rich_error> guard(err);
    ValueReader vis{value};
    if(!decode_value<default_config<Config>>(vis, out)) {
        return std::unexpected(std::move(err));
    }
    return {};
}

template <typename T, typename Config = void>
    requires std::default_initializable<T>
auto from_dyn(const Value& value) -> std::expected<T, rich_error> {
    T out{};
    auto result = from_dyn<Config>(value, out);
    if(!result) {
        return std::unexpected(std::move(result).error());
    }
    return out;
}

}  // namespace kota::codec::dyn

namespace kota::codec {

template <typename Config>
struct deserialize_visit<dyn::ValueReader, dyn::Value, Config> {
    static bool visit(dyn::ValueReader& vis, dyn::Value& value) {
        value = vis.node;
        return true;
    }
};

template <typename Config>
struct deserialize_visit<dyn::ValueReader, dyn::Array, Config> {
    static bool visit(dyn::ValueReader& vis, dyn::Array& value) {
        const auto* arr = vis.node.get_array();
        if(!arr) {
            return scoped_context<rich_error>::fail(
                rich_error::invalid_type("array", dyn::detail::kind_name(vis.node.kind())));
        }
        value = *arr;
        return true;
    }
};

template <typename Config>
struct deserialize_visit<dyn::ValueReader, dyn::Object, Config> {
    static bool visit(dyn::ValueReader& vis, dyn::Object& value) {
        const auto* obj = vis.node.get_object();
        if(!obj) {
            return scoped_context<rich_error>::fail(
                rich_error::invalid_type("object", dyn::detail::kind_name(vis.node.kind())));
        }
        value = *obj;
        return true;
    }
};

template <typename Vis, typename Config>
struct deserialize_visit<
    Vis,
    dyn::Value,
    Config,
    std::enable_if_t<detail::has_peek_kind<Vis> && !std::is_same_v<Vis, dyn::ValueReader>>> {
    static bool visit(Vis& vis, dyn::Value& value) {
        auto kind = vis.peek_kind();
        switch(kind) {
            case meta::type_kind::null: {
                KOTA_CODEC_TRY(vis.visit_null());
                value = dyn::Value(nullptr);
                return true;
            }
            case meta::type_kind::boolean: {
                bool v = false;
                KOTA_CODEC_TRY(vis.visit_bool(v));
                value = dyn::Value(v);
                return true;
            }
            case meta::type_kind::int64: {
                std::int64_t v = 0;
                KOTA_CODEC_TRY(vis.visit_int(v));
                value = dyn::Value(v);
                return true;
            }
            case meta::type_kind::uint64: {
                std::uint64_t v = 0;
                KOTA_CODEC_TRY(vis.visit_uint(v));
                value = dyn::Value(v);
                return true;
            }
            case meta::type_kind::float64: {
                double v = 0.0;
                KOTA_CODEC_TRY(vis.visit_float(v));
                value = dyn::Value(v);
                return true;
            }
            case meta::type_kind::string: {
                std::string v;
                KOTA_CODEC_TRY(vis.visit_str(v));
                value = dyn::Value(std::move(v));
                return true;
            }
            case meta::type_kind::array: {
                dyn::Array arr;
                KOTA_CODEC_TRY(vis.visit_seq([&](auto& ev) -> bool {
                    dyn::Value elem;
                    KOTA_CODEC_TRY(decode_value<Config>(ev, elem));
                    arr.push_back(std::move(elem));
                    return true;
                }));
                value = dyn::Value(std::move(arr));
                return true;
            }
            case meta::type_kind::structure: {
                dyn::Object obj;
                KOTA_CODEC_TRY(vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
                    dyn::Value field_val;
                    KOTA_CODEC_TRY(decode_value<Config>(fv, field_val));
                    obj.insert(std::string(key), std::move(field_val));
                    return true;
                }));
                value = dyn::Value(std::move(obj));
                return true;
            }
            default: {
                return scoped_context<rich_error>::fail(
                    rich_error("cannot convert unknown source type to dyn::Value"));
            }
        }
    }
};

template <typename Vis, typename Config>
struct deserialize_visit<
    Vis,
    dyn::Array,
    Config,
    std::enable_if_t<detail::has_peek_kind<Vis> && !std::is_same_v<Vis, dyn::ValueReader>>> {
    static bool visit(Vis& vis, dyn::Array& value) {
        value = dyn::Array{};
        return vis.visit_seq([&](auto& ev) -> bool {
            dyn::Value elem;
            KOTA_CODEC_TRY(decode_value<Config>(ev, elem));
            value.push_back(std::move(elem));
            return true;
        });
    }
};

template <typename Vis, typename Config>
struct deserialize_visit<
    Vis,
    dyn::Object,
    Config,
    std::enable_if_t<detail::has_peek_kind<Vis> && !std::is_same_v<Vis, dyn::ValueReader>>> {
    static bool visit(Vis& vis, dyn::Object& value) {
        value = dyn::Object{};
        return vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
            dyn::Value field_val;
            KOTA_CODEC_TRY(decode_value<Config>(fv, field_val));
            value.insert(std::string(key), std::move(field_val));
            return true;
        });
    }
};

}  // namespace kota::codec
