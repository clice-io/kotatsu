#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/support/config.h"
#include "kota/support/expected_try.h"
#include "kota/support/numeric.h"
#include "kota/support/swar.h"
#include "kota/codec/json/type.h"
#include "kota/codec/visit/common.h"
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

/// Sets the line and column, from 1, of each location, which holds its byte
/// offset into text, in one pass over the text for all of them.
inline void count_lines(std::string_view text, std::span<rich_error::source_location*> locations) {
    std::ranges::sort(locations, {}, [](const auto* location) { return location->byte_offset; });
    std::size_t line = 1;
    std::size_t line_start = 0;
    std::size_t at = 0;
    for(auto* location: locations) {
        for(; at < location->byte_offset; ++at) {
            if(text[at] == '\n') {
                ++line;
                line_start = at + 1;
            }
        }
        location->line = line;
        location->column = location->byte_offset - line_start + 1;
    }
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
    /// The opening quote of the key an object member's reader reads the
    /// value of; null for any other reader.
    const char* key_at = nullptr;
    constexpr static bool data_driven = true;
    constexpr static bool human_readable = true;
    using format = json::format;

    Reader(ondemand::Document& d, const char* base, std::size_t size) :
        src(d), buf_base(base), buf_size(size) {}

    Reader(ondemand::Value& v, const char* base, std::size_t size) :
        src(v), buf_base(base), buf_size(size) {}

    template <typename F>
    decltype(auto) apply(F&& f) const {
        return src.apply(std::forward<F>(f));
    }

    /// Fails with err located where the iterator stopped, by byte offset
    /// alone: from_string counts the line and column of the location it
    /// reports once the decode is over, so that the failures of untagged
    /// probes, which nobody sees, do not each count from the start.
    bool fail_located(rich_error err) {
        auto loc_result = src.apply([](auto& s) { return s.current_location(); });
        if(!loc_result.error()) {
            const char* loc = loc_result.value_unsafe();
            if(loc >= buf_base && loc <= buf_base + buf_size) {
                err.location = rich_error::source_location{
                    .byte_offset = static_cast<std::size_t>(loc - buf_base)};
            }
        }
        return scoped_context<rich_error>::fail(std::move(err));
    }

    /// Where the key this reader's value belongs to starts, by byte offset
    /// alone, as fail_located.
    std::optional<rich_error::source_location> key_location() const {
        if(!key_at) {
            return std::nullopt;
        }
        return rich_error::source_location{.byte_offset =
                                               static_cast<std::size_t>(key_at - buf_base)};
    }

    bool fail_simdjson(simdjson::error_code ec) {
        return fail_located(detail::simdjson_error(ec));
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
        auto c = char_from_utf8(r.value_unsafe());
        if(!c) {
            return fail_located(rich_error(std::string(invalid_char_message)));
        }
        out = *c;
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

    /// Where an object's members stand as they are read. simdjson inlines its
    /// ondemand iteration into every caller, so open and next hold the only
    /// copies, and the visitors only their loop and callback.
    struct ObjectCursor {
        simdjson::ondemand::object object;
        simdjson::simdjson_result<simdjson::ondemand::object_iterator> it;
        simdjson::simdjson_result<simdjson::ondemand::object_iterator> end;
        std::string_view key;
        /// The opening quote of key.
        const char* key_at = nullptr;
        ondemand::Value value;
        bool started = false;
        /// A member could not be read; the failure is reported.
        bool failed = false;
    };

    /// Where an array's elements stand as they are read, as ObjectCursor.
    struct ArrayCursor {
        simdjson::ondemand::array array;
        simdjson::simdjson_result<simdjson::ondemand::array_iterator> it;
        simdjson::simdjson_result<simdjson::ondemand::array_iterator> end;
        ondemand::Value value;
        bool started = false;
        bool failed = false;
    };

    KOTA_NOINLINE bool open(ObjectCursor& cursor) {
        auto r = src.apply([&](auto& s) { return s.get_object(); });
        if(r.error()) {
            return fail_simdjson(r.error());
        }
        cursor.object = std::move(r).value_unsafe();
        cursor.it = cursor.object.begin();
        cursor.end = cursor.object.end();
        return true;
    }

    /// Moves to the next member: false at the end, or when it cannot be read.
    KOTA_NOINLINE bool next(ObjectCursor& cursor) {
        if(cursor.started) {
            ++cursor.it;
        }
        cursor.started = true;
        if(cursor.it == cursor.end) {
            return false;
        }
        auto field_result = *cursor.it;
        if(field_result.error()) {
            cursor.failed = true;
            return fail_simdjson(field_result.error());
        }
        auto field = std::move(field_result).value_unsafe();
        // The raw key starts after its opening quote; unescaping it lets go of
        // it, so it is taken first.
        cursor.key_at = field.key().raw() - 1;
        // A key without escapes is the text itself, which outlives the decode.
        cursor.key = field.escaped_key();
        if(any_word(cursor.key, 0, [](std::uint64_t word) { return has_byte(word, '\\'); })) {
            auto key = field.unescaped_key();
            if(key.error()) {
                cursor.failed = true;
                return fail_simdjson(key.error());
            }
            cursor.key = key.value_unsafe();
        }
        cursor.value = std::move(field).value();
        return true;
    }

    KOTA_NOINLINE bool open(ArrayCursor& cursor) {
        auto r = src.apply([&](auto& s) { return s.get_array(); });
        if(r.error()) {
            return fail_simdjson(r.error());
        }
        cursor.array = std::move(r).value_unsafe();
        cursor.it = cursor.array.begin();
        cursor.end = cursor.array.end();
        return true;
    }

    KOTA_NOINLINE bool next(ArrayCursor& cursor) {
        if(cursor.started) {
            ++cursor.it;
        }
        cursor.started = true;
        if(cursor.it == cursor.end) {
            return false;
        }
        auto elem = *cursor.it;
        if(elem.error()) {
            cursor.failed = true;
            return fail_simdjson(elem.error());
        }
        cursor.value = std::move(elem).value_unsafe();
        return true;
    }

    template <typename Callback>
    bool visit_struct(Callback&& cb) {
        ObjectCursor cursor;
        if(!open(cursor)) {
            return false;
        }
        bool ok = true;
        while(ok && next(cursor)) {
            Reader sub{cursor.value, buf_base, buf_size};
            sub.key_at = cursor.key_at;
            ok = cb(cursor.key, sub);
        }
        return ok && !cursor.failed;
    }

    template <typename Callback>
    bool visit_seq(Callback&& cb) {
        ArrayCursor cursor;
        if(!open(cursor)) {
            return false;
        }
        bool ok = true;
        while(ok && next(cursor)) {
            Reader sub{cursor.value, buf_base, buf_size};
            ok = cb(sub);
        }
        return ok && !cursor.failed;
    }

    /// An object read with MapKeyReader keys.
    template <typename Callback>
    bool visit_map(Callback&& cb) {
        return visit_struct([&](std::string_view key, Reader& value) {
            MapKeyReader<format> kr{key};
            return cb(kr, value);
        });
    }

    /// A tuple is an array, read as a sequence.
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
    rich_error discard_err;
    scoped_context<rich_error> guard(discard_err);

    simdjson::ondemand::json_iterator checkpoint(src.json_iter());

    if(fn(*this))
        return true;

    src.json_iter() = checkpoint;
    return false;
}

namespace detail {

/// One parse: the parser and its document, set up, run and torn down out of
/// line, so that a decode's instance carries only the decode.
struct ParsedDocument {
    ondemand::Parser parser;
    ondemand::Document doc;
    /// Where the decode reports unknown fields, and how many it held before.
    UnknownFields* sink = scoped_context<UnknownFields>::try_current();
    std::size_t reported = sink ? sink->entries.size() : 0;

    KOTA_NOINLINE ParsedDocument() {}

    KOTA_NOINLINE ~ParsedDocument() {}

    /// Parses text and sees that it holds one value: simdjson checks what
    /// follows a scalar root, not what follows an object or array, so the
    /// root is walked first and the document must end after it, so that
    /// trailing content fails before anything reaches the output.
    KOTA_NOINLINE std::expected<void, rich_error> parse(padded_string_view text) {
        if(auto ec = parser.iterate(text).get(doc); ec != success) {
            return std::unexpected(simdjson_error(ec));
        }
        if(auto root = doc.raw_json(); root.error()) {
            return std::unexpected(simdjson_error(root.error()));
        }
        if(!doc.at_end()) {
            return std::unexpected(simdjson_error(simdjson::TRAILING_CONTENT));
        }
        doc.rewind();
        return {};
    }

    /// Gives the locations the decode of text left behind, which hold their
    /// byte offsets, their lines; one a decode nested in it (an adapter
    /// reading a JSON string) counted in its own text already has its line.
    KOTA_NOINLINE void locate(std::string_view text, std::expected<void, rich_error>& result) {
        std::vector<rich_error::source_location*> locations;
        auto uncounted = [&](std::optional<rich_error::source_location>& location) {
            if(location && location->line == 0) {
                locations.push_back(&*location);
            }
        };
        if(!result) {
            uncounted(result.error().location);
        }
        if(sink) {
            for(auto& entry: std::span(sink->entries).subspan(reported)) {
                uncounted(entry.location);
            }
        }
        count_lines(text, locations);
    }
};

}  // namespace detail

/// Decodes JSON text that has SIMDJSON_PADDING readable bytes past its end
/// into `out`, without a copy of the text (or, in the value-returning
/// overload, into a value-initialized T).
template <typename Config = void, typename T>
auto from_padded_string(padded_string_view json, T& out) -> std::expected<void, rich_error> {
    detail::ParsedDocument document;
    KOTA_EXPECTED_TRY(document.parse(json));
    Reader r{document.doc, json.data(), json.size()};
    auto result = codec::detail::run_decode<Config>(r, out);
    document.locate(json, result);
    return result;
}

template <typename T, typename Config = void>
    requires std::is_default_constructible_v<T>
auto from_padded_string(padded_string_view json) -> std::expected<T, rich_error> {
    auto value = T();
    KOTA_EXPECTED_TRY(from_padded_string<Config>(json, value));
    return value;
}

/// Decodes JSON text into `out` (or, in the value-returning overload, into a
/// value-initialized T).
template <typename Config = void, typename T>
auto from_string(std::string_view json, T& out) -> std::expected<void, rich_error> {
    padded_string padded(json);
    return from_padded_string<Config>(padded, out);
}

template <typename T, typename Config = void>
    requires std::is_default_constructible_v<T>
auto from_string(std::string_view json) -> std::expected<T, rich_error> {
    auto value = T();
    KOTA_EXPECTED_TRY(from_string<Config>(json, value));
    return value;
}

}  // namespace kota::codec::json
