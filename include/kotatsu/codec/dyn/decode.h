#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "kotatsu/support/expected_try.h"
#include "kotatsu/support/numeric.h"
#include "kotatsu/meta/type_kind.h"
#include "kotatsu/codec/context.h"
#include "kotatsu/codec/decode.h"
#include "kotatsu/codec/dyn/document.h"
#include "kotatsu/codec/format.h"
#include "kotatsu/codec/repr.h"

namespace kotatsu::codec::dyn {

namespace detail {

/// Fails with the type error for a node that is not an `expected`.
inline bool fail_type(const Value& node, std::string_view expected) {
    return scoped_context<rich_error>::fail(
        rich_error::invalid_type(expected, kind_name(node.kind())));
}

}  // namespace detail

template <typename Format>
struct ValueReader {
    const Value& node;
    constexpr static bool data_driven = true;
    using format = Format;

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
            fits = kotatsu::narrow_int(*signed_value, out);
        } else if(const auto* unsigned_value = std::get_if<std::uint64_t>(&node.variant())) {
            fits = kotatsu::narrow_int(*unsigned_value, out);
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
        rich_error discard_err;
        scoped_context<rich_error> guard(discard_err);
        ValueReader<Format> fork{node};
        return fn(fork);
    }

    template <typename Callback>
    bool visit_struct(Callback&& cb) {
        const auto* obj = node.get_object();
        if(!obj) {
            return fail_type("object");
        }
        for(const auto& [k, v]: *obj) {
            ValueReader<Format> sub{v};
            KOTATSU_CODEC_TRY(cb(std::string_view(k), sub));
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
            ValueReader<Format> sub{element};
            KOTATSU_CODEC_TRY(cb(sub));
        }
        return true;
    }

    /// A tuple is an array, read as a sequence.
    template <typename Callback>
    bool visit_tuple(Callback&& cb) {
        return visit_seq(std::forward<Callback>(cb));
    }

    /// An object read with MapKeyReader keys.
    template <typename Callback>
    bool visit_map(Callback&& cb) {
        return visit_struct([&](std::string_view key, ValueReader& value) {
            MapKeyReader<Format> kr{key};
            return cb(kr, value);
        });
    }

private:
    bool fail_type(std::string_view expected) {
        return detail::fail_type(node, expected);
    }
};

/// Decodes a dyn::Value DOM tree into `out` (or, in the value-returning
/// overload, into a value-initialized T) under the format F.
template <typename F = codec::format, typename T>
    requires std::derived_from<F, codec::format>
auto from_dyn(const Value& value, T& out, const decode_options& options = {})
    -> std::expected<void, rich_error> {
    ValueReader<F> vis{value};
    return codec::detail::run_decode<F>(vis, out, options);
}

template <typename T, typename F = codec::format>
    requires std::is_default_constructible_v<T>
auto from_dyn(const Value& value, const decode_options& options = {})
    -> std::expected<T, rich_error> {
    auto out = T();
    KOTATSU_EXPECTED_TRY(from_dyn<F>(value, out, options));
    return out;
}

}  // namespace kotatsu::codec::dyn

namespace kotatsu::codec {

template <typename Format>
struct deserialize_visit<dyn::ValueReader<Format>, dyn::Value> {
    static bool visit(dyn::ValueReader<Format>& vis, dyn::Value& value) {
        value = vis.node;
        return true;
    }
};

template <typename Format>
struct deserialize_visit<dyn::ValueReader<Format>, dyn::Array> {
    static bool visit(dyn::ValueReader<Format>& vis, dyn::Array& value) {
        const auto* arr = vis.node.get_array();
        if(!arr) {
            return dyn::detail::fail_type(vis.node, "array");
        }
        value = *arr;
        return true;
    }
};

template <typename Format>
struct deserialize_visit<dyn::ValueReader<Format>, dyn::Object> {
    static bool visit(dyn::ValueReader<Format>& vis, dyn::Object& value) {
        const auto* obj = vis.node.get_object();
        if(!obj) {
            return dyn::detail::fail_type(vis.node, "object");
        }
        value = *obj;
        return true;
    }
};

template <typename Vis>
    requires detail::has_peek_kind<Vis>
struct deserialize_visit<Vis, dyn::Value> {
    static bool visit(Vis& vis, dyn::Value& value) {
        auto kind = vis.peek_kind();
        switch(kind) {
            case meta::type_kind::null: {
                KOTATSU_CODEC_TRY(vis.visit_null());
                value = dyn::Value(nullptr);
                return true;
            }
            case meta::type_kind::boolean: {
                bool v = false;
                KOTATSU_CODEC_TRY(vis.visit_bool(v));
                value = dyn::Value(v);
                return true;
            }
            case meta::type_kind::int64: {
                std::int64_t v = 0;
                KOTATSU_CODEC_TRY(vis.visit_int(v));
                value = dyn::Value(v);
                return true;
            }
            case meta::type_kind::uint64: {
                std::uint64_t v = 0;
                KOTATSU_CODEC_TRY(vis.visit_uint(v));
                value = dyn::Value(v);
                return true;
            }
            case meta::type_kind::float64: {
                double v = 0.0;
                KOTATSU_CODEC_TRY(vis.visit_float(v));
                value = dyn::Value(v);
                return true;
            }
            case meta::type_kind::string: {
                std::string v;
                KOTATSU_CODEC_TRY(vis.visit_str(v));
                value = dyn::Value(std::move(v));
                return true;
            }
            case meta::type_kind::array: {
                dyn::Array arr;
                KOTATSU_CODEC_TRY((deserialize_visit<Vis, dyn::Array>::visit(vis, arr)));
                value = dyn::Value(std::move(arr));
                return true;
            }
            case meta::type_kind::structure: {
                dyn::Object obj;
                KOTATSU_CODEC_TRY((deserialize_visit<Vis, dyn::Object>::visit(vis, obj)));
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

template <typename Vis>
    requires detail::has_peek_kind<Vis>
struct deserialize_visit<Vis, dyn::Array> {
    static bool visit(Vis& vis, dyn::Array& value) {
        value = dyn::Array{};
        return vis.visit_seq([&](auto& ev) -> bool {
            dyn::Value elem;
            KOTATSU_CODEC_TRY(decode_value(ev, elem));
            value.push_back(std::move(elem));
            return true;
        });
    }
};

template <typename Vis>
    requires detail::has_peek_kind<Vis>
struct deserialize_visit<Vis, dyn::Object> {
    static bool visit(Vis& vis, dyn::Object& value) {
        value = dyn::Object{};
        return vis.visit_struct([&](std::string_view key, auto& fv) -> bool {
            dyn::Value field_val;
            KOTATSU_CODEC_TRY(decode_value(fv, field_val));
            value.insert(std::string(key), std::move(field_val));
            return true;
        });
    }
};

}  // namespace kotatsu::codec
