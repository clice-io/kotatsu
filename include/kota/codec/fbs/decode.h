#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "kota/codec/fbs/proxy.h"
#include "kota/codec/fbs/type.h"
#include "kota/codec/visit/config.h"
#include "kota/codec/visit/context.h"
#include "kota/codec/visit/decode.h"

namespace kota::codec::fbs {

namespace decode_detail {

// Readers bounds-check every raw buffer access against the verifier before
// performing it, so decoding cannot read outside the buffer no matter how
// the dispatch drives the visit (reprs, adapters, any Config). Table
// descents pair VerifyTableStart/EndTable, which also caps nesting depth —
// cyclic offsets terminate instead of recursing forever.

inline bool fail_verify(std::string_view what) {
    return scoped_context<rich_error>::fail(
        rich_error(std::format("buffer verification failed: {}", what)));
}

template <typename T>
struct ScalarReader : detail::VisitorBase {
    T value;

    bool visit_bool(bool& out) {
        out = static_cast<bool>(value);
        return true;
    }

    template <typename U>
    bool visit_int(U& out) {
        out = static_cast<U>(value);
        return true;
    }

    template <typename U>
    bool visit_uint(U& out) {
        out = static_cast<U>(value);
        return true;
    }

    template <typename U>
    bool visit_float(U& out) {
        out = static_cast<U>(value);
        return true;
    }

    template <typename U>
    bool visit_char(U& out) {
        out = static_cast<U>(value);
        return true;
    }

    template <typename U, typename Body>
    bool visit_struct(U& out, Body&&) {
        static_assert(std::is_same_v<U, T>);
        out = value;
        return true;
    }
};

struct StringReader : detail::VisitorBase {
    std::string_view value;

    template <typename T>
    bool visit_str(T& out) {
        if constexpr(std::same_as<T, std::string>) {
            out.assign(value.data(), value.size());
        } else if constexpr(std::same_as<T, std::string_view>) {
            out = value;
        } else if constexpr(std::constructible_from<T, const char*, std::size_t>) {
            out = T(value.data(), value.size());
        } else {
            out = T(value);
        }
        return true;
    }
};

struct TableFieldReader : detail::VisitorBase {
    const Table* tbl;
    verifier_t* verifier;
    std::size_t idx = 0;

    template <typename Idx, typename F>
    bool visit_field(Idx, std::string_view, F&& reader);

    template <typename F>
    bool visit_element(F&& reader);
};

template <typename E>
struct VecReader {
    using element_t = std::remove_cvref_t<E>;

    // An element travels as its resolved representation (behavior attrs, then
    // chained reprs); the reader choice mirrors the encode side through the
    // shared element_layout classification.
    using repr_t = proxy_detail::apply_repr_t<element_t>;

    constexpr static auto layout = proxy_detail::element_layout_of<element_t>();

    using vec_ptr_t = proxy_detail::element_vector_ptr_t<element_t>;

    vec_ptr_t vec;
    verifier_t* verifier;
    uoffset_t idx = 0;

    bool has_element() {
        return vec != nullptr && idx < vec->size();
    }

    template <typename F>
    bool visit_element(F&& reader);
};

struct MapReader {
    const Vector<table_offset_t>* vec;
    verifier_t* verifier;
    uoffset_t idx = 0;

    bool has_entry() {
        return vec != nullptr && idx < vec->size();
    }

    template <typename KF, typename VF>
    bool visit_entry(KF&& key_fn, VF&& val_fn);
};

// (table*, slot) reader: slot > 0 reads from that field slot,
// slot == 0 means the table itself IS the value (e.g. vector element).
struct FieldReader : detail::VisitorBase {
    const Table* tbl;
    voffset_t slot;
    verifier_t* verifier;

    bool peek_null() {
        return tbl->GetOptionalFieldOffset(slot) == 0;
    }

    bool visit_null() {
        return true;
    }

    bool visit_bool(bool& out) {
        return read_cell<std::uint8_t>(out, "bool field");
    }

    template <typename T>
    bool visit_int(T& out) {
        return read_cell<T>(out, "integer field");
    }

    template <typename T>
    bool visit_uint(T& out) {
        return read_cell<T>(out, "integer field");
    }

    template <typename T>
    bool visit_float(T& out) {
        if constexpr(std::same_as<T, float> || std::same_as<T, double>) {
            return read_cell<T>(out, "float field");
        } else {
            return read_cell<double>(out, "float field");
        }
    }

    template <typename T>
    bool visit_str(T& out) {
        if(!tbl->VerifyOffset(*verifier, slot))
            return fail_verify("string field offset");
        const auto* text = tbl->GetPointer<const String*>(slot);
        if(!verifier->VerifyString(text))
            return fail_verify("string field");
        if(text == nullptr) {
            if constexpr(std::same_as<T, std::string>) {
                out.clear();
            }
            return true;
        }
        if constexpr(std::same_as<T, std::string>) {
            out.assign(text->data(), text->size());
        } else if constexpr(std::same_as<T, std::string_view>) {
            out = std::string_view(text->data(), text->size());
        } else if constexpr(std::constructible_from<T, const char*, std::size_t>) {
            out = T(text->data(), text->size());
        } else {
            std::string_view sv(text->data(), text->size());
            out = T(sv);
        }
        return true;
    }

    template <typename T>
    bool visit_char(T& out) {
        return read_cell<std::int8_t>(out, "char field");
    }

    template <typename T>
    bool visit_bytes(T& out) {
        if(!tbl->VerifyOffset(*verifier, slot))
            return fail_verify("bytes field offset");
        const auto* vec = tbl->GetPointer<const Vector<std::uint8_t>*>(slot);
        if(!verifier->VerifyVector(vec))
            return fail_verify("bytes field");
        if(vec == nullptr)
            return true;
        if constexpr(std::same_as<T, std::vector<std::byte>>) {
            out.resize(vec->size());
            for(std::size_t i = 0; i < vec->size(); ++i) {
                out[i] = static_cast<std::byte>(vec->Get(static_cast<uoffset_t>(i)));
            }
        } else if constexpr(std::same_as<T, std::span<const std::byte>>) {
            out = std::span<const std::byte>(reinterpret_cast<const std::byte*>(vec->data()),
                                             vec->size());
        } else {
            auto data = reinterpret_cast<const std::byte*>(vec->data());
            out = T(data, data + vec->size());
        }
        return true;
    }

    template <typename T, typename Body>
    bool visit_struct(T& out, Body&& body);

    template <typename T, typename Body>
    bool visit_seq(T& out, Body&& body);

    template <typename T, typename Body>
    bool visit_tuple(T& out, Body&& body);

    template <typename T, typename Body>
    bool visit_map(T& out, Body&& body);

    template <typename Body>
    bool visit_variant(Body&& body);

private:
    // Reads one fixed-width cell after bounds-checking it; an absent slot
    // reads as the zero cell, like every scalar access in this backend.
    template <typename Cell, typename T>
    bool read_cell(T& out, std::string_view what) {
        if(!tbl->VerifyField<Cell>(*verifier, slot, alignof(Cell)))
            return fail_verify(what);
        out = static_cast<T>(tbl->GetField<Cell>(slot, Cell{}));
        return true;
    }

    // Follows the table this reader designates into `out`. slot == 0
    // designates tbl itself (vector/map elements), whose start the producing
    // reader already verified; a field slot is offset-checked and the child
    // entered (VerifyTableStart), with `entered` telling the caller to
    // balance with EndTable(). Returns false only on verification failure;
    // a null `out` means the field is absent.
    bool follow_table(const Table*& out, bool& entered, std::string_view what) const {
        out = nullptr;
        entered = false;
        if(slot == 0) {
            out = tbl;
            return true;
        }
        if(!tbl->VerifyOffset(*verifier, slot))
            return fail_verify(what);
        const auto* child = tbl->GetPointer<const Table*>(slot);
        if(child == nullptr)
            return true;
        if(!child->VerifyTableStart(*verifier))
            return fail_verify(what);
        out = child;
        entered = true;
        return true;
    }
};

template <typename Idx, typename F>
bool TableFieldReader::visit_field(Idx, std::string_view, F&& reader) {
    const voffset_t vid = detail::first_field + detail::field_step * static_cast<voffset_t>(Idx{});
    FieldReader fr{.tbl = tbl, .slot = vid, .verifier = verifier};
    return reader(fr);
}

template <typename F>
bool TableFieldReader::visit_element(F&& reader) {
    const voffset_t vid = detail::first_field + detail::field_step * static_cast<voffset_t>(idx);
    FieldReader fr{.tbl = tbl, .slot = vid, .verifier = verifier};
    ++idx;
    return reader(fr);
}

struct RootReader : FieldReader {
    RootReader(const Table* root, verifier_t* verifier) :
        FieldReader{.tbl = root, .slot = detail::first_field, .verifier = verifier} {}

    template <typename U, typename Body>
    bool visit_struct(U&, Body&& body) {
        detail::assert_fields_reflected<std::remove_const_t<U>>();
        TableFieldReader tfr{.tbl = tbl, .verifier = verifier};
        return body(tfr);
    }

    template <typename U, typename Body>
    bool visit_tuple(U&, Body&& body) {
        TableFieldReader tfr{.tbl = tbl, .verifier = verifier};
        return body(tfr);
    }

    template <typename Body>
    bool visit_variant(Body&& body) {
        if(!tbl->VerifyField<std::uint32_t>(*verifier, detail::first_field, alignof(std::uint32_t)))
            return fail_verify("variant tag");
        auto tag = tbl->GetField<std::uint32_t>(detail::first_field, 0);
        auto index = static_cast<std::size_t>(tag);
        // A hostile tag can wrap this cast; safe because construct_and_visit
        // rejects any out-of-range index before the payload reader is used.
        const voffset_t payload_slot = static_cast<voffset_t>(
            detail::first_field + detail::field_step * static_cast<voffset_t>(index + 1));
        FieldReader pv{.tbl = tbl, .slot = payload_slot, .verifier = verifier};
        return body(index, pv);
    }
};

template <typename T, typename Body>
bool FieldReader::visit_struct(T& out, Body&& body) {
    using V = std::remove_const_t<T>;
    if constexpr(can_inline_struct_v<V>) {
        if(!tbl->VerifyField<V>(*verifier, slot, alignof(V)))
            return fail_verify("inline struct field");
        const auto* ptr = tbl->GetStruct<const V*>(slot);
        if(!proxy_detail::valid_inline_struct_bytes(ptr))
            return fail_verify("inline struct bool byte");
        if(ptr != nullptr)
            out = *ptr;
        return true;
    } else {
        detail::assert_fields_reflected<V>();
        const Table* child = nullptr;
        bool entered = false;
        if(!follow_table(child, entered, "struct field"))
            return false;
        if(child == nullptr)
            return true;
        TableFieldReader tfr{.tbl = child, .verifier = verifier};
        const bool ok = body(tfr);
        if(entered)
            verifier->EndTable();
        return ok;
    }
}

template <typename T, typename Body>
bool FieldReader::visit_seq([[maybe_unused]] T& out, Body&& body) {
    using V = std::remove_const_t<T>;
    using E = std::ranges::range_value_t<V>;
    const auto effective_slot = (slot == 0) ? detail::first_field : slot;
    using vr_t = VecReader<E>;
    if(!tbl->VerifyOffset(*verifier, effective_slot))
        return fail_verify("vector field offset");
    const auto* vec = tbl->GetPointer<typename vr_t::vec_ptr_t>(effective_slot);
    if(!verifier->VerifyVector(vec))
        return fail_verify("vector field");
    vr_t vr{.vec = vec, .verifier = verifier};
    return body(vr);
}

template <typename T, typename Body>
bool FieldReader::visit_tuple(T&, Body&& body) {
    const Table* child = nullptr;
    bool entered = false;
    if(!follow_table(child, entered, "tuple field"))
        return false;
    if(child == nullptr)
        return true;
    TableFieldReader tfr{.tbl = child, .verifier = verifier};
    const bool ok = body(tfr);
    if(entered)
        verifier->EndTable();
    return ok;
}

template <typename T, typename Body>
bool FieldReader::visit_map(T&, Body&& body) {
    const auto effective_slot = (slot == 0) ? detail::first_field : slot;
    if(!tbl->VerifyOffset(*verifier, effective_slot))
        return fail_verify("map field offset");
    const auto* vec = tbl->GetPointer<const Vector<table_offset_t>*>(effective_slot);
    if(!verifier->VerifyVector(vec))
        return fail_verify("map field");
    MapReader mr{.vec = vec, .verifier = verifier};
    return body(mr);
}

template <typename Body>
bool FieldReader::visit_variant(Body&& body) {
    const Table* var_table = nullptr;
    bool entered = false;
    if(!follow_table(var_table, entered, "variant field"))
        return false;
    if(var_table == nullptr) {
        return scoped_context<rich_error>::fail(rich_error("null variant table"));
    }
    const bool ok = [&] {
        if(!var_table->VerifyField<std::uint32_t>(*verifier,
                                                  detail::first_field,
                                                  alignof(std::uint32_t)))
            return fail_verify("variant tag");
        auto tag = var_table->GetField<std::uint32_t>(detail::first_field, 0);
        auto index = static_cast<std::size_t>(tag);
        // A hostile tag can wrap this cast; safe because construct_and_visit
        // rejects any out-of-range index before the payload reader is used.
        const voffset_t payload_slot = static_cast<voffset_t>(
            detail::first_field + detail::field_step * static_cast<voffset_t>(index + 1));
        FieldReader pv{.tbl = var_table, .slot = payload_slot, .verifier = verifier};
        return body(index, pv);
    }();
    if(entered)
        verifier->EndTable();
    return ok;
}

template <typename E>
template <typename F>
bool VecReader<E>::visit_element(F&& reader) {
    using enum proxy_detail::element_layout;

    // The built-in dispatch loops on has_element(), but an imperative
    // adapter drives this reader directly — reject the cursor here so it
    // cannot read past the verified vector.
    if(!has_element())
        return fail_verify("vector element out of range");

    if constexpr(layout == boxed) {
        const auto* wrapper = vec->template GetAs<Table>(idx);
        ++idx;
        if(!wrapper->VerifyTableStart(*verifier))
            return fail_verify("vector element");
        FieldReader fr{.tbl = wrapper, .slot = detail::first_field, .verifier = verifier};
        const bool ok = reader(fr);
        verifier->EndTable();
        return ok;
    } else if constexpr(layout == scalar) {
        auto val = vec->Get(idx);
        ++idx;
        ScalarReader<decltype(val)> sr{.value = val};
        return reader(sr);
    } else if constexpr(layout == string) {
        const auto* text = vec->GetAsString(static_cast<uoffset_t>(idx));
        ++idx;
        if(!verifier->VerifyString(text))
            return fail_verify("string vector element");
        std::string_view sv;
        if(text != nullptr) {
            sv = std::string_view(text->data(), text->size());
        }
        StringReader sr{.value = sv};
        return reader(sr);
    } else if constexpr(layout == inline_struct) {
        const auto* ptr = vec->Get(static_cast<uoffset_t>(idx));
        ++idx;
        if(!proxy_detail::valid_inline_struct_bytes(ptr))
            return fail_verify("inline struct element bool byte");
        ScalarReader<repr_t> sr{.value = ptr ? *ptr : repr_t{}};
        return reader(sr);
    } else {
        const auto* child = vec->template GetAs<Table>(static_cast<uoffset_t>(idx));
        ++idx;
        if(!child->VerifyTableStart(*verifier))
            return fail_verify("vector element");
        FieldReader fr{.tbl = child, .slot = 0, .verifier = verifier};
        const bool ok = reader(fr);
        verifier->EndTable();
        return ok;
    }
}

template <typename KF, typename VF>
bool MapReader::visit_entry(KF&& key_fn, VF&& val_fn) {
    if(!has_entry())
        return fail_verify("map entry out of range");
    const auto* entry = vec->template GetAs<Table>(idx);
    ++idx;
    if(!entry->VerifyTableStart(*verifier))
        return fail_verify("map entry");
    const bool ok = [&] {
        FieldReader kr{.tbl = entry, .slot = detail::first_field, .verifier = verifier};
        KOTA_CODEC_TRY(key_fn(kr));
        FieldReader vr{.tbl = entry,
                       .slot = detail::first_field + detail::field_step,
                       .verifier = verifier};
        return val_fn(vr);
    }();
    verifier->EndTable();
    return ok;
}

}  // namespace decode_detail

/// Decodes a buffer produced by to_bytes with the same T and Config, after
/// checking the "EVTO" identifier. The buffer is fully verified while it is
/// read: every scalar, offset, string, vector, and nested table access is
/// bounds-checked before it happens, so corrupt, truncated, or malicious
/// input fails with an error instead of reading out of bounds. Table nesting
/// deeper than the flatbuffers default of 64 is rejected (which also
/// terminates cyclic offsets), as are buffers at or above flatbuffers'
/// maximum buffer size (just under 2 GiB).
/// Overloads: std::byte / uint8_t spans, into an out-param or returning T.
template <typename Config = void, typename T>
auto from_bytes(std::span<const std::byte> buf, T& out) -> std::expected<void, rich_error> {
    detail::assert_config_layout_stable<Config>();

    // Root uoffset plus the 4-byte identifier: the smallest well-formed buffer.
    if(buf.size() < 2 * sizeof(uoffset_t)) {
        return std::unexpected(rich_error("buffer too small"));
    }
    if(buf.size() >= FLATBUFFERS_MAX_BUFFER_SIZE) {
        return std::unexpected(rich_error("buffer too large"));
    }

    const auto* data = reinterpret_cast<const std::uint8_t*>(buf.data());
    auto size = buf.size();

    if(!::flatbuffers::BufferHasIdentifier(data, detail::buffer_identifier)) {
        return std::unexpected(rich_error("invalid buffer identifier"));
    }

    auto verifier = detail::make_verifier(data, size);
    if(verifier.VerifyOffset(0) == 0) {
        return std::unexpected(rich_error("buffer verification failed: root offset"));
    }
    const auto* root = ::flatbuffers::GetRoot<Table>(data);
    if(!root->VerifyTableStart(verifier)) {
        return std::unexpected(rich_error("buffer verification failed: root table"));
    }

    rich_error err;
    scoped_context<rich_error> guard(err);

    decode_detail::RootReader vis(root, &verifier);
    if(!decode_value<default_config<Config>>(vis, out)) {
        return std::unexpected(std::move(err));
    }
    return {};
}

template <typename Config = void, typename T>
auto from_bytes(std::span<const std::uint8_t> buf, T& out) -> std::expected<void, rich_error> {
    auto bytes =
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(buf.data()), buf.size());
    return from_bytes<Config>(bytes, out);
}

template <typename T, typename Config = void>
    requires std::default_initializable<T>
auto from_bytes(std::span<const std::uint8_t> buf) -> std::expected<T, rich_error> {
    T value{};
    auto result = from_bytes<Config>(buf, value);
    if(!result) {
        return std::unexpected(result.error());
    }
    return value;
}

template <typename T, typename Config = void>
    requires std::default_initializable<T>
auto from_bytes(std::span<const std::byte> buf) -> std::expected<T, rich_error> {
    T value{};
    auto result = from_bytes<Config>(buf, value);
    if(!result) {
        return std::unexpected(result.error());
    }
    return value;
}

}  // namespace kota::codec::fbs
