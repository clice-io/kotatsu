#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "kota/support/numeric.h"
#include "kota/codec/json/type.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/decode.h"
#include "kota/codec/visit/map_key.h"

namespace kota::codec::json {

namespace detail {

// Friend injection to access simdjson's protected `iter` members.
//
// simdjson's ondemand API stores parser state in a `json_iterator` that tracks the
// token cursor position, string buffer write pointer, and parse depth. These are held
// in protected `iter` fields of `document` and `value`. We need direct access to
// save/restore this state when `try_read` fails during untagged variant decoding —
// without this, the string buffer consumed by a failed alternative is never reclaimed,
// causing heap-buffer-overflow on large payloads.
//
// Technique: explicit template instantiation bypasses access control on the member
// pointer argument. The friend function `get(Tag)` is injected into the enclosing
// namespace; declaring it also as a friend of the tag struct makes it discoverable
// via ADL at the call site.

using doc_iter_ptr = simdjson::ondemand::json_iterator simdjson::ondemand::document::*;
using val_iter_ptr = simdjson::ondemand::value_iterator simdjson::ondemand::value::*;

struct doc_iter_tag {
    friend constexpr doc_iter_ptr get(doc_iter_tag);
};

struct val_iter_tag {
    friend constexpr val_iter_ptr get(val_iter_tag);
};

template <typename Tag, typename T, T MemPtr>
struct steal {
    friend constexpr T get(Tag) {
        return MemPtr;
    }
};

// Access control is not checked for explicit instantiation template arguments,
// allowing us to form pointers to the protected members.
template struct steal<doc_iter_tag, doc_iter_ptr, &simdjson::ondemand::document::iter>;
template struct steal<val_iter_tag, val_iter_ptr, &simdjson::ondemand::value::iter>;

inline char32_t decode_first_codepoint(std::string_view sv) {
    if(sv.empty())
        return 0xFFFFFFFF;
    auto b0 = static_cast<unsigned char>(sv[0]);
    if(b0 < 0x80)
        return static_cast<char32_t>(b0);
    std::size_t len;
    char32_t cp;
    if((b0 & 0xE0) == 0xC0) {
        len = 2;
        cp = b0 & 0x1F;
    } else if((b0 & 0xF0) == 0xE0) {
        len = 3;
        cp = b0 & 0x0F;
    } else if((b0 & 0xF8) == 0xF0) {
        len = 4;
        cp = b0 & 0x07;
    } else {
        return 0xFFFFFFFF;
    }
    if(sv.size() < len)
        return 0xFFFFFFFF;
    for(std::size_t i = 1; i < len; ++i) {
        auto b = static_cast<unsigned char>(sv[i]);
        if((b & 0xC0) != 0x80)
            return 0xFFFFFFFF;
        cp = (cp << 6) | (b & 0x3F);
    }
    return cp;
}

}  // namespace detail

struct Source {
    explicit Source(ondemand::Value& v) : ptr(reinterpret_cast<std::uintptr_t>(&v)) {}

    explicit Source(ondemand::Document& d) : ptr(reinterpret_cast<std::uintptr_t>(&d) | tag) {}

    bool is_document() const {
        return (ptr & tag) != 0;
    }

    ondemand::Document& doc() const {
        return *reinterpret_cast<ondemand::Document*>(ptr & ~tag);
    }

    ondemand::Value& value() const {
        return *reinterpret_cast<ondemand::Value*>(ptr);
    }

    template <typename F>
    decltype(auto) apply(F&& f) const {
        if(is_document())
            return f(doc());
        return f(value());
    }

    // Access the underlying json_iterator via friend-injected member pointers.
    // For document: directly access the json_iterator member.
    // For value: access the value_iterator member, then its public json_iter() method.
    simdjson::ondemand::json_iterator& json_iter() const {
        if(is_document())
            return doc().*get(detail::doc_iter_tag{});
        return (value().*get(detail::val_iter_tag{})).json_iter();
    }

private:
    std::uintptr_t ptr;
    constexpr static std::uintptr_t tag = 1;
};

struct Reader {
    Source src;
    const char* buf_base;
    std::size_t buf_size;
    constexpr static bool data_driven = true;
    constexpr static bool human_readable = true;
    using error_type = rich_error;
    using format = json::format;

    Reader(ondemand::Document& d, const char* base, std::size_t size) :
        src(d), buf_base(base), buf_size(size) {}

    Reader(ondemand::Value& v, const char* base, std::size_t size) :
        src(v), buf_base(base), buf_size(size) {}

    template <typename F>
    decltype(auto) apply(F&& f) const {
        return src.apply(std::forward<F>(f));
    }

    bool fail_located(rich_error err) {
        auto loc_result = src.apply([](auto& s) { return s.current_location(); });
        if(!loc_result.error()) {
            const char* loc = loc_result.value_unsafe();
            if(loc >= buf_base && loc <= buf_base + buf_size) {
                auto offset = static_cast<std::size_t>(loc - buf_base);
                std::size_t line = 1, col = 1;
                for(std::size_t i = 0; i < offset; ++i) {
                    if(buf_base[i] == '\n') {
                        ++line;
                        col = 1;
                    } else {
                        ++col;
                    }
                }
                err.set_location({line, col, offset});
            }
        }
        return scoped_context<rich_error>::fail(std::move(err));
    }

    bool fail_simdjson(simdjson::error_code ec) {
        return fail_located(rich_error(std::string(simdjson::error_message(ec))));
    }

    template <typename F>
    bool try_read(F&& fn);

    bool visit_bool(bool& out) {
        auto r = src.apply([&](auto& s) { return s.get_bool(); });
        if(r.error())
            return fail_simdjson(r.error());
        out = r.value_unsafe();
        return true;
    }

    template <typename T>
    bool visit_int(T& out) {
        auto r = src.apply([&](auto& s) { return s.get_int64(); });
        if(r.error())
            return fail_simdjson(r.error());
        if(!kota::narrow_int(r.value_unsafe(), out)) {
            return fail_located(rich_error("number out of range"));
        }
        return true;
    }

    template <typename T>
    bool visit_uint(T& out) {
        auto r = src.apply([&](auto& s) { return s.get_uint64(); });
        if(r.error())
            return fail_simdjson(r.error());
        if(!kota::narrow_int(r.value_unsafe(), out)) {
            return fail_located(rich_error("number out of range"));
        }
        return true;
    }

    template <typename T>
    bool visit_float(T& out) {
        auto r = src.apply([&](auto& s) { return s.get_double(); });
        if(r.error())
            return fail_simdjson(r.error());
        out = static_cast<T>(r.value_unsafe());
        return true;
    }

    template <typename T>
    bool visit_str(T& out) {
        auto r = src.apply([&](auto& s) { return s.get_string(); });
        if(r.error())
            return fail_simdjson(r.error());
        out = T(r.value_unsafe());
        return true;
    }

    template <typename T>
    bool visit_char(T& out) {
        auto r = src.apply([&](auto& s) { return s.get_string(); });
        if(r.error())
            return fail_simdjson(r.error());
        auto sv = r.value_unsafe();
        char32_t cp = detail::decode_first_codepoint(sv);
        if(cp == 0xFFFFFFFF) {
            return fail_located(rich_error::invalid_type("single character", "multi-char string"));
        }
        std::size_t cp_len = (cp < 0x80) ? 1 : (cp < 0x800) ? 2 : (cp < 0x10000) ? 3 : 4;
        if(sv.size() != cp_len) {
            return fail_located(rich_error::invalid_type("single character", "multi-char string"));
        }
        constexpr auto max_cp =
            static_cast<char32_t>((std::numeric_limits<std::make_unsigned_t<T>>::max)());
        if(cp > max_cp) {
            return fail_located(
                rich_error("character codepoint does not fit the target character type"));
        }
        out = static_cast<T>(cp);
        return true;
    }

    template <typename T>
    bool visit_bytes(T& out) {
        auto r = src.apply([&](auto& s) { return s.get_array(); });
        if(r.error())
            return fail_simdjson(r.error());
        out.clear();
        for(auto elem: r.value_unsafe()) {
            auto byte_r = elem.get_uint64();
            if(byte_r.error())
                return fail_simdjson(byte_r.error());
            if(byte_r.value_unsafe() > 255)
                return fail_located(rich_error("byte value out of range"));
            out.push_back(static_cast<typename T::value_type>(
                static_cast<std::uint8_t>(byte_r.value_unsafe())));
        }
        return true;
    }

    bool peek_null() {
        auto r = src.apply([&](auto& s) { return s.is_null(); });
        return !r.error() && r.value_unsafe();
    }

    bool visit_null() {
        auto r = src.apply([&](auto& s) { return s.is_null(); });
        if(r.error())
            return fail_simdjson(r.error());
        if(!r.value_unsafe())
            return fail_simdjson(simdjson::INCORRECT_TYPE);
        return true;
    }

    meta::type_kind peek_kind() {
        using namespace ondemand;
        using meta::type_kind;

        return src.apply([&](auto& s) -> type_kind {
            auto r = s.is_null();
            if(!r.error() && r.value_unsafe())
                return type_kind::null;

            auto t = s.type();
            if(t.error())
                return type_kind::unknown;

            switch(t.value_unsafe()) {
                case Type::null: return type_kind::null;
                case Type::boolean: return type_kind::boolean;
                case Type::number: {
                    auto nt = s.get_number_type();
                    if(nt.error())
                        return type_kind::unknown;
                    switch(nt.value_unsafe()) {
                        case NumberType::signed_integer: return type_kind::int64;
                        case NumberType::unsigned_integer: return type_kind::uint64;
                        case NumberType::floating_point_number: return type_kind::float64;
                        // Beyond 64 bits only a double holds it.
                        case NumberType::big_integer: return type_kind::float64;
                    }
                    return type_kind::unknown;
                }
                case Type::string: return type_kind::string;
                case Type::array: return type_kind::array;
                case Type::object: return type_kind::structure;
                default: return type_kind::unknown;
            }
        });
    }

    bool visit_skip() {
        return true;
    }

    template <typename Callback>
    bool visit_struct(Callback&& cb) {
        auto r = src.apply([&](auto& s) { return s.get_object(); });
        if(r.error())
            return fail_simdjson(r.error());
        auto& obj = r.value_unsafe();
        bool ok = true;
        for(auto field_result: obj) {
            if(field_result.error()) {
                ok = fail_simdjson(field_result.error());
                break;
            }
            auto field = std::move(field_result).value_unsafe();
            auto key = field.unescaped_key();
            if(key.error()) {
                ok = fail_simdjson(key.error());
                break;
            }
            auto fv = std::move(field).value();
            Reader sub{fv, buf_base, buf_size};
            if(!cb(key.value_unsafe(), sub)) {
                ok = false;
                break;
            }
        }
        obj.reset();
        return ok;
    }

    template <typename Callback>
    bool visit_seq(Callback&& cb) {
        auto r = src.apply([&](auto& s) { return s.get_array(); });
        if(r.error())
            return fail_simdjson(r.error());
        auto& arr = r.value_unsafe();
        bool ok = true;
        for(auto elem: arr) {
            if(elem.error()) {
                ok = fail_simdjson(elem.error());
                break;
            }
            auto val = std::move(elem).value_unsafe();
            Reader sub{val, buf_base, buf_size};
            if(!cb(sub)) {
                ok = false;
                break;
            }
        }
        arr.reset();
        return ok;
    }

    template <typename Callback>
    bool visit_map(Callback&& cb) {
        auto r = src.apply([&](auto& s) { return s.get_object(); });
        if(r.error())
            return fail_simdjson(r.error());
        auto& obj = r.value_unsafe();
        bool ok = true;
        for(auto field_result: obj) {
            if(field_result.error()) {
                ok = fail_simdjson(field_result.error());
                break;
            }
            auto field = std::move(field_result).value_unsafe();
            auto key = field.unescaped_key();
            if(key.error()) {
                ok = fail_simdjson(key.error());
                break;
            }
            auto fv = std::move(field).value();
            MapKeyReader<format> kr{key.value_unsafe()};
            Reader vr{fv, buf_base, buf_size};
            if(!cb(kr, vr)) {
                ok = false;
                break;
            }
        }
        obj.reset();
        return ok;
    }

    template <typename Callback>
    bool visit_tuple(Callback&& cb) {
        return visit_seq(std::forward<Callback>(cb));
    }
};

/// Attempt to decode using `fn`. On failure, restore the full simdjson parser state.
///
/// simdjson's json_iterator holds: token cursor position (into the pre-built structural
/// index), string buffer write pointer (_string_buf_loc, where unescaped strings are
/// written), and parse depth. All three must be restored atomically on failure.
///
/// Safety: the checkpoint copy is safe because on failure, all decoded outputs from `fn`
/// are destroyed (e.g. variant's emplace destroys the failed alternative), so no live
/// string_view references the reclaimed string buffer region.
///
/// Note: json_iterator's copy constructor is explicit (to prevent accidental copies in
/// simdjson internals), so we use direct-initialization. The copy assignment operator
/// is defaulted and safe — unlike the move assignment which nulls the parser pointer.
template <typename F>
bool Reader::try_read(F&& fn) {
    error_type discard_err;
    scoped_context<error_type> guard(discard_err);

    simdjson::ondemand::json_iterator checkpoint(src.json_iter());

    if(fn(*this))
        return true;

    src.json_iter() = checkpoint;
    return false;
}

/// Decodes JSON text into `out` (or, in the value-returning overload, into a
/// default-constructed T).
template <typename Config = void, typename T>
auto from_string(std::string_view json, T& out) -> std::expected<void, rich_error> {
    padded_string padded(json);
    ondemand::Parser parser;
    ondemand::Document doc;

    auto ec = parser.iterate(padded).get(doc);
    if(ec != success) {
        return std::unexpected(rich_error(std::string(simdjson::error_message(ec))));
    }

    rich_error guard_error;
    scoped_context<rich_error> guard(guard_error);

    Reader r{doc, padded.data(), padded.size()};
    if(!decode_value<default_config<Config>>(r, out)) {
        return std::unexpected(std::move(guard_error));
    }
    return {};
}

template <typename T, typename Config = void>
    requires std::default_initializable<T>
auto from_string(std::string_view json) -> std::expected<T, rich_error> {
    T value{};
    auto result = from_string<Config>(json, value);
    if(!result) {
        return std::unexpected(std::move(result).error());
    }
    return value;
}

}  // namespace kota::codec::json
