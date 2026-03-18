#pragma once

#ifndef ETD_SERDE_CONTENT_DOM_INL_INCLUDED
#define ETD_SERDE_CONTENT_DOM_INL_INCLUDED 1
#endif

#include "eventide/serde/content/dom.h"

namespace eventide::serde::content {

inline TaggedRef::TaggedRef(const yyjson_val* value) noexcept :
    tagged_handle_value(tag_handle(value, false)) {}

inline TaggedRef::TaggedRef(const yyjson_mut_val* value) noexcept :
    tagged_handle_value(tag_handle(value, true)) {}

inline TaggedRef::TaggedRef(std::uintptr_t tagged_handle) noexcept :
    tagged_handle_value(tagged_handle) {}

inline bool TaggedRef::valid() const noexcept {
    return tagged_handle_value != 0;
}

inline bool TaggedRef::mutable_ref() const noexcept {
    return (tagged_handle_value & k_mutable_bit) != 0;
}

inline std::uintptr_t TaggedRef::tagged_handle() const noexcept {
    return tagged_handle_value;
}

inline void* TaggedRef::untagged_ptr() const noexcept {
    return reinterpret_cast<void*>(tagged_handle_value & ~k_mutable_bit);
}

inline yyjson_val* TaggedRef::immutable_ptr() const noexcept {
    return reinterpret_cast<yyjson_val*>(untagged_ptr());
}

inline yyjson_mut_val* TaggedRef::mutable_ptr() const noexcept {
    if(!mutable_ref()) {
        return nullptr;
    }
    return reinterpret_cast<yyjson_mut_val*>(untagged_ptr());
}

inline std::uintptr_t TaggedRef::tag_handle(const void* pointer, bool mutable_bit) noexcept {
    if(pointer == nullptr) {
        return 0;
    }
    auto value = reinterpret_cast<std::uintptr_t>(pointer);
    return mutable_bit ? (value | k_mutable_bit) : value;
}

inline void TaggedRef::set_tagged_handle(std::uintptr_t tagged_handle) noexcept {
    tagged_handle_value = tagged_handle;
}

inline ValueRef::ValueRef(const yyjson_val* value) noexcept : TaggedRef(value) {}

inline ValueRef::ValueRef(const yyjson_mut_val* value) noexcept : TaggedRef(value) {}

inline ValueRef::ValueRef(std::uintptr_t tagged_handle) noexcept : TaggedRef(tagged_handle) {}

inline auto ValueRef::kind() const noexcept -> ValueKind {
    assert(valid());
    if(is_null()) {
        return ValueKind::null_value;
    }
    if(is_bool()) {
        return ValueKind::boolean;
    }
    if(is_int()) {
        return ValueKind::integer;
    }
    if(is_number()) {
        return ValueKind::number;
    }
    if(is_string()) {
        return ValueKind::string;
    }
    if(is_array()) {
        return ValueKind::array;
    }
    if(is_object()) {
        return ValueKind::object;
    }
    return ValueKind::invalid;
}

inline bool ValueRef::is_null() const noexcept {
    assert(valid());
    return mutable_ref() ? yyjson_mut_is_null(mutable_ptr()) : yyjson_is_null(immutable_ptr());
}

inline bool ValueRef::is_bool() const noexcept {
    assert(valid());
    return mutable_ref() ? yyjson_mut_is_bool(mutable_ptr()) : yyjson_is_bool(immutable_ptr());
}

inline bool ValueRef::is_int() const noexcept {
    assert(valid());
    return mutable_ref() ? yyjson_mut_is_int(mutable_ptr()) : yyjson_is_int(immutable_ptr());
}

inline bool ValueRef::is_number() const noexcept {
    assert(valid());
    return mutable_ref() ? yyjson_mut_is_num(mutable_ptr()) : yyjson_is_num(immutable_ptr());
}

inline bool ValueRef::is_string() const noexcept {
    assert(valid());
    return mutable_ref() ? yyjson_mut_is_str(mutable_ptr()) : yyjson_is_str(immutable_ptr());
}

inline bool ValueRef::is_array() const noexcept {
    assert(valid());
    return mutable_ref() ? yyjson_mut_is_arr(mutable_ptr()) : yyjson_is_arr(immutable_ptr());
}

inline bool ValueRef::is_object() const noexcept {
    assert(valid());
    return mutable_ref() ? yyjson_mut_is_obj(mutable_ptr()) : yyjson_is_obj(immutable_ptr());
}

inline auto ValueRef::get_bool() const noexcept -> std::optional<bool> {
    assert(valid());
    if(!is_bool()) {
        return std::nullopt;
    }
    return mutable_ref() ? yyjson_mut_get_bool(mutable_ptr()) : yyjson_get_bool(immutable_ptr());
}

inline auto ValueRef::get_int() const noexcept -> std::optional<std::int64_t> {
    assert(valid());
    if(!is_int()) {
        return std::nullopt;
    }
    return mutable_ref() ? yyjson_mut_get_sint(mutable_ptr()) : yyjson_get_sint(immutable_ptr());
}

inline auto ValueRef::get_uint() const noexcept -> std::optional<std::uint64_t> {
    assert(valid());
    if(!is_int()) {
        return std::nullopt;
    }
    return mutable_ref() ? yyjson_mut_get_uint(mutable_ptr()) : yyjson_get_uint(immutable_ptr());
}

inline auto ValueRef::get_double() const noexcept -> std::optional<double> {
    assert(valid());
    if(!is_number()) {
        return std::nullopt;
    }
    return mutable_ref() ? yyjson_mut_get_num(mutable_ptr()) : yyjson_get_num(immutable_ptr());
}

inline auto ValueRef::get_string() const noexcept -> std::optional<std::string_view> {
    assert(valid());
    if(!is_string()) {
        return std::nullopt;
    }
    const char* text =
        mutable_ref() ? yyjson_mut_get_str(mutable_ptr()) : yyjson_get_str(immutable_ptr());
    if(text == nullptr) {
        return std::string_view{};
    } else {
        const std::size_t len =
            mutable_ref() ? yyjson_mut_get_len(mutable_ptr()) : yyjson_get_len(immutable_ptr());
        return std::string_view{text, len};
    }
}

inline auto ValueRef::get_array() const noexcept -> std::optional<ArrayRef> {
    assert(valid());
    if(!is_array()) {
        return std::nullopt;
    }
    return ArrayRef(tagged_handle());
}

inline auto ValueRef::get_object() const noexcept -> std::optional<ObjectRef> {
    assert(valid());
    if(!is_object()) {
        return std::nullopt;
    }
    return ObjectRef(tagged_handle());
}

inline bool ValueRef::as_bool() const {
    auto value = get_bool();
    assert(value.has_value());
    return *value;
}

inline std::int64_t ValueRef::as_int() const {
    auto value = get_int();
    assert(value.has_value());
    return *value;
}

inline std::uint64_t ValueRef::as_uint() const {
    auto value = get_uint();
    assert(value.has_value());
    return *value;
}

inline double ValueRef::as_double() const {
    auto value = get_double();
    assert(value.has_value());
    return *value;
}

inline std::string_view ValueRef::as_string() const {
    auto value = get_string();
    assert(value.has_value());
    return *value;
}

inline ArrayRef ValueRef::as_array() const {
    auto value = get_array();
    assert(value.has_value());
    return *value;
}

inline ObjectRef ValueRef::as_object() const {
    auto value = get_object();
    assert(value.has_value());
    return *value;
}

inline void ValueRef::assert_valid() const {
    assert(valid());
}

inline void ValueRef::assert_kind(ValueKind expected) const {
    assert(kind() == expected);
}

inline ArrayRef::ArrayRef(const yyjson_val* value) noexcept : TaggedRef(value) {}

inline ArrayRef::ArrayRef(const yyjson_mut_val* value) noexcept : TaggedRef(value) {}

inline ArrayRef::ArrayRef(std::uintptr_t tagged_handle) noexcept : TaggedRef(tagged_handle) {}

inline bool ArrayRef::valid() const noexcept {
    return TaggedRef::valid() &&
           (mutable_ref() ? yyjson_mut_is_arr(mutable_ptr()) : yyjson_is_arr(immutable_ptr()));
}

inline std::size_t ArrayRef::size() const noexcept {
    assert(valid());
    return mutable_ref() ? yyjson_mut_arr_size(mutable_ptr()) : yyjson_arr_size(immutable_ptr());
}

inline bool ArrayRef::empty() const noexcept {
    return size() == 0;
}

inline auto ArrayRef::get(std::size_t index) const noexcept -> std::optional<ValueRef> {
    assert(valid());
    if(index >= size()) {
        return std::nullopt;
    }
    auto value = unchecked_get(index);
    if(!value.valid()) {
        return std::nullopt;
    }
    return value;
}

inline ValueRef ArrayRef::at(std::size_t index) const {
    auto value = get(index);
    assert(value.has_value());
    return *value;
}

inline ValueRef ArrayRef::operator[](std::size_t index) const noexcept {
    assert(valid());
    assert(index < size());
    return unchecked_get(index);
}

inline void ArrayRef::assert_valid() const {
    assert(valid());
}

inline auto ArrayRef::begin() const noexcept -> iterator {
    return iterator(this, 0);
}

inline auto ArrayRef::end() const noexcept -> iterator {
    return iterator(this, size());
}

inline ValueRef ArrayRef::unchecked_get(std::size_t index) const noexcept {
    return mutable_ref() ? ValueRef(yyjson_mut_arr_get(mutable_ptr(), index))
                         : ValueRef(yyjson_arr_get(immutable_ptr(), index));
}

inline ArrayRef::iterator::iterator(const ArrayRef* owner, std::size_t index) noexcept :
    owner(owner), index(index) {}

inline auto ArrayRef::iterator::operator*() const noexcept -> value_type {
    assert(owner != nullptr);
    assert(index < owner->size());
    return owner->unchecked_get(index);
}

inline auto ArrayRef::iterator::operator++() noexcept -> iterator& {
    if(owner != nullptr && index < owner->size()) {
        ++index;
    }
    return *this;
}

inline auto ArrayRef::iterator::operator++(int) noexcept -> iterator {
    iterator copy = *this;
    ++(*this);
    return copy;
}

inline bool ArrayRef::iterator::operator==(const iterator& other) const noexcept {
    return owner == other.owner && index == other.index;
}

inline ObjectRef::ObjectRef(const yyjson_val* value) noexcept : TaggedRef(value) {}

inline ObjectRef::ObjectRef(const yyjson_mut_val* value) noexcept : TaggedRef(value) {}

inline ObjectRef::ObjectRef(std::uintptr_t tagged_handle) noexcept : TaggedRef(tagged_handle) {}

inline bool ObjectRef::valid() const noexcept {
    return TaggedRef::valid() &&
           (mutable_ref() ? yyjson_mut_is_obj(mutable_ptr()) : yyjson_is_obj(immutable_ptr()));
}

inline std::size_t ObjectRef::size() const noexcept {
    assert(valid());
    return mutable_ref() ? yyjson_mut_obj_size(mutable_ptr()) : yyjson_obj_size(immutable_ptr());
}

inline bool ObjectRef::empty() const noexcept {
    return size() == 0;
}

inline auto ObjectRef::get(std::string_view key) const noexcept -> std::optional<ValueRef> {
    assert(valid());
    auto* value = mutable_ref() ? reinterpret_cast<const void*>(
                                      yyjson_mut_obj_getn(mutable_ptr(), key.data(), key.size()))
                                : reinterpret_cast<const void*>(
                                      yyjson_obj_getn(immutable_ptr(), key.data(), key.size()));
    if(value == nullptr) {
        return std::nullopt;
    }
    return ValueRef(tag_handle(value, mutable_ref()));
}

inline bool ObjectRef::contains(std::string_view key) const noexcept {
    return get(key).has_value();
}

inline ValueRef ObjectRef::at(std::string_view key) const {
    auto value = get(key);
    assert(value.has_value());
    return *value;
}

inline ValueRef ObjectRef::operator[](std::string_view key) const noexcept {
    assert(valid());
    auto value = get(key);
    assert(value.has_value());
    return *value;
}

inline void ObjectRef::assert_valid() const {
    assert(valid());
}

inline auto ObjectRef::begin() const noexcept -> iterator {
    return iterator(this, false);
}

inline auto ObjectRef::end() const noexcept -> iterator {
    return iterator(this, true);
}

inline ObjectRef::iterator::iterator(const ObjectRef* owner, bool end) noexcept :
    owner(owner), end_flag(end) {
    if(owner == nullptr || end || !owner->valid()) {
        return;
    } else {
        mutable_mode = owner->mutable_ref();
        if(mutable_mode) {
            mutable_iter = yyjson_mut_obj_iter_with(owner->mutable_ptr());
            mutable_key = yyjson_mut_obj_iter_next(&mutable_iter);
            end_flag = (mutable_key == nullptr);
        } else {
            immutable_iter = yyjson_obj_iter_with(owner->immutable_ptr());
            immutable_key = yyjson_obj_iter_next(&immutable_iter);
            end_flag = (immutable_key == nullptr);
        }
    }
}

inline auto ObjectRef::iterator::operator*() const noexcept -> value_type {
    assert(owner != nullptr);
    assert(!end_flag);
    assert(owner->valid());

    if(mutable_mode) {
        auto* key = mutable_key;
        auto* value = yyjson_mut_obj_iter_get_val(key);
        const char* text = yyjson_mut_get_str(key);
        if(text == nullptr) {
            return {.key = std::string_view{}, .value = ValueRef(value)};
        } else {
            return {
                .key = std::string_view{text, yyjson_mut_get_len(key)},
                .value = ValueRef(value)
            };
        }
    } else {
        auto* key = immutable_key;
        auto* value = yyjson_obj_iter_get_val(key);
        const char* text = yyjson_get_str(key);
        if(text == nullptr) {
            return {.key = std::string_view{}, .value = ValueRef(value)};
        } else {
            return {
                .key = std::string_view{text, yyjson_get_len(key)},
                .value = ValueRef(value)
            };
        }
    }
}

inline auto ObjectRef::iterator::operator++() noexcept -> iterator& {
    if(end_flag) {
        return *this;
    } else {
        if(mutable_mode) {
            mutable_key = yyjson_mut_obj_iter_next(&mutable_iter);
            end_flag = (mutable_key == nullptr);
            return *this;
        } else {
            immutable_key = yyjson_obj_iter_next(&immutable_iter);
            end_flag = (immutable_key == nullptr);
            return *this;
        }
    }
}

inline auto ObjectRef::iterator::operator++(int) noexcept -> iterator {
    iterator copy = *this;
    ++(*this);
    return copy;
}

inline bool ObjectRef::iterator::operator==(const iterator& other) const noexcept {
    if(owner != other.owner) {
        return false;
    } else if(end_flag && other.end_flag) {
        return true;
    } else if(end_flag != other.end_flag) {
        return false;
    } else if(mutable_mode != other.mutable_mode) {
        return false;
    } else if(mutable_mode) {
        return mutable_key == other.mutable_key;
    } else {
        return immutable_key == other.immutable_key;
    }
}

inline OwnedDoc::OwnedDoc(const OwnedDoc& other) noexcept :
    tagged_doc_handle(other.tagged_doc_handle), ref_count(other.ref_count),
    external_owner(other.external_owner) {
    retain();
}

inline auto OwnedDoc::operator=(const OwnedDoc& other) noexcept -> OwnedDoc& {
    if(this == &other) {
        return *this;
    }

    release();
    tagged_doc_handle = other.tagged_doc_handle;
    ref_count = other.ref_count;
    external_owner = other.external_owner;
    retain();
    return *this;
}

inline OwnedDoc::OwnedDoc(OwnedDoc&& other) noexcept :
    tagged_doc_handle(other.tagged_doc_handle), ref_count(other.ref_count),
    external_owner(other.external_owner) {
    other.tagged_doc_handle = 0;
    other.ref_count = nullptr;
    other.external_owner = false;
}

inline auto OwnedDoc::operator=(OwnedDoc&& other) noexcept -> OwnedDoc& {
    if(this == &other) {
        return *this;
    }

    release();
    tagged_doc_handle = other.tagged_doc_handle;
    ref_count = other.ref_count;
    external_owner = other.external_owner;
    other.tagged_doc_handle = 0;
    other.ref_count = nullptr;
    other.external_owner = false;
    return *this;
}

inline OwnedDoc::~OwnedDoc() {
    release();
}

inline bool OwnedDoc::has_owner() const noexcept {
    return tagged_doc_handle != 0 && ref_count != nullptr;
}

inline int OwnedDoc::use_count() const noexcept {
    if(ref_count == nullptr) {
        return 0;
    }
    return *ref_count;
}

inline bool OwnedDoc::mutable_doc() const noexcept {
    return (tagged_doc_handle & k_mutable_bit) != 0;
}

inline std::uintptr_t OwnedDoc::tag_doc(const void* pointer, bool mutable_bit) noexcept {
    if(pointer == nullptr) {
        return 0;
    } else {
        auto tagged = reinterpret_cast<std::uintptr_t>(pointer);
        return mutable_bit ? (tagged | k_mutable_bit) : tagged;
    }
}

inline yyjson_doc* OwnedDoc::immutable_doc_ptr() const noexcept {
    if(!has_owner() || mutable_doc()) {
        return nullptr;
    } else {
        return reinterpret_cast<yyjson_doc*>(tagged_doc_handle & ~k_mutable_bit);
    }
}

inline yyjson_mut_doc* OwnedDoc::mutable_doc_ptr() const noexcept {
    if(!has_owner() || !mutable_doc()) {
        return nullptr;
    } else {
        return reinterpret_cast<yyjson_mut_doc*>(tagged_doc_handle & ~k_mutable_bit);
    }
}

inline auto OwnedDoc::ensure_writable_doc_and_rebind_root(TaggedRef& ref)
    -> std::expected<yyjson_mut_val*, error_kind> {
    if(!ref.valid() || !has_owner()) {
        return std::unexpected(error_kind::invalid_state);
    } else {
        bool copied = false;
        yyjson_mut_doc* writable_doc = nullptr;

        if(!mutable_doc()) {
            auto* source_doc = immutable_doc_ptr();
            if(source_doc == nullptr) {
                return std::unexpected(error_kind::invalid_state);
            } else {
                writable_doc = yyjson_doc_mut_copy(source_doc, nullptr);
                if(writable_doc == nullptr) {
                    return std::unexpected(error_kind::allocation_failed);
                } else {
                    copied = true;
                }
            }
        } else if(use_count() > 1) {
            auto* source_doc = mutable_doc_ptr();
            if(source_doc == nullptr) {
                return std::unexpected(error_kind::invalid_state);
            } else {
                writable_doc = yyjson_mut_doc_mut_copy(source_doc, nullptr);
                if(writable_doc == nullptr) {
                    return std::unexpected(error_kind::allocation_failed);
                } else {
                    copied = true;
                }
            }
        } else {
            writable_doc = mutable_doc_ptr();
            if(writable_doc == nullptr) {
                return std::unexpected(error_kind::invalid_state);
            }
        }

        if(copied) {
            int* new_ref_count = new (std::nothrow) int(1);
            if(new_ref_count == nullptr) {
                yyjson_mut_doc_free(writable_doc);
                return std::unexpected(error_kind::allocation_failed);
            } else {
                release();
                tagged_doc_handle = tag_doc(writable_doc, true);
                ref_count = new_ref_count;
                external_owner = false;
            }
        }

        auto* root = yyjson_mut_doc_get_root(writable_doc);
        if(root == nullptr) {
            return std::unexpected(error_kind::invalid_state);
        } else {
            ref.set_tagged_handle(TaggedRef::tag_handle(root, true));
            return root;
        }
    }
}

inline OwnedDoc::OwnedDoc(std::uintptr_t tagged_doc_handle,
                          int* ref_count,
                          bool retain_owner,
                          bool external_owner) noexcept :
    tagged_doc_handle(tagged_doc_handle), ref_count(ref_count), external_owner(external_owner) {
    if(retain_owner) {
        retain();
    }
}

inline void OwnedDoc::retain() noexcept {
    if(ref_count == nullptr) {
        return;
    }
    ++(*ref_count);
}

inline void OwnedDoc::release() noexcept {
    if(ref_count == nullptr) {
        tagged_doc_handle = 0;
        return;
    }

    --(*ref_count);
    if(*ref_count == 0) {
        auto* raw_doc = reinterpret_cast<void*>(tagged_doc_handle & ~k_mutable_bit);
        if(!external_owner && raw_doc != nullptr) {
            mutable_doc() ? yyjson_mut_doc_free(reinterpret_cast<yyjson_mut_doc*>(raw_doc))
                          : yyjson_doc_free(reinterpret_cast<yyjson_doc*>(raw_doc));
        }
        delete ref_count;
    }

    tagged_doc_handle = 0;
    ref_count = nullptr;
    external_owner = false;
}

inline Value::Value(std::uintptr_t tagged_value_handle,
                    std::uintptr_t tagged_doc_handle,
                    int* ref_count,
                    bool retain_owner,
                    bool external_owner) noexcept :
    ValueRef(tagged_value_handle),
    OwnedDoc(tagged_doc_handle, ref_count, retain_owner, external_owner) {}

inline auto Value::parse(std::string_view json) -> std::expected<Value, yyjson_read_code> {
    return parse(json, parse_options{});
}

inline auto Value::parse(std::string_view json, parse_options options)
    -> std::expected<Value, yyjson_read_code> {
    auto flags = static_cast<yyjson_read_flag>(options.flags & ~YYJSON_READ_INSITU);
    yyjson_read_err err{};
    yyjson_doc* raw_doc = yyjson_read_opts(const_cast<char*>(json.data()),
                                           json.size(),
                                           flags,
                                           options.allocator,
                                           &err);
    if(raw_doc == nullptr) {
        return std::unexpected(err.code);
    } else {
        auto value = from_immutable_doc(raw_doc);
        if(!value.has_value()) {
            return std::unexpected(YYJSON_READ_ERROR_MEMORY_ALLOCATION);
        } else {
            return std::move(*value);
        }
    }
}

inline auto Value::from_immutable_doc(yyjson_doc* raw_doc) noexcept -> std::optional<Value> {
    if(raw_doc == nullptr) {
        return std::nullopt;
    }

    auto* root = yyjson_doc_get_root(raw_doc);
    if(root == nullptr) {
        yyjson_doc_free(raw_doc);
        return std::nullopt;
    }

    int* ref_count = new (std::nothrow) int(1);
    if(ref_count == nullptr) {
        yyjson_doc_free(raw_doc);
        return std::nullopt;
    }

    return Value(tag_handle(root, false), tag_doc(raw_doc, false), ref_count, false);
}

inline auto Value::from_mutable_doc(yyjson_mut_doc* raw_doc) noexcept -> std::optional<Value> {
    if(raw_doc == nullptr) {
        return std::nullopt;
    }

    auto* root = yyjson_mut_doc_get_root(raw_doc);
    if(root == nullptr) {
        yyjson_mut_doc_free(raw_doc);
        return std::nullopt;
    }

    int* ref_count = new (std::nothrow) int(1);
    if(ref_count == nullptr) {
        yyjson_mut_doc_free(raw_doc);
        return std::nullopt;
    }

    return Value(tag_handle(root, true), tag_doc(raw_doc, true), ref_count, false);
}

inline auto Value::copy_of(ValueRef source) -> std::expected<Value, error_kind> {
    if(!source.valid()) {
        return std::unexpected(error_kind::invalid_state);
    } else {
        yyjson_mut_doc* raw_doc = yyjson_mut_doc_new(nullptr);
        if(raw_doc == nullptr) {
            return std::unexpected(error_kind::allocation_failed);
        } else {
            yyjson_mut_val* copied_root =
                source.mutable_ref() ? yyjson_mut_val_mut_copy(raw_doc, source.mutable_ptr())
                                     : yyjson_val_mut_copy(raw_doc, source.immutable_ptr());
            if(copied_root == nullptr) {
                yyjson_mut_doc_free(raw_doc);
                return std::unexpected(error_kind::allocation_failed);
            } else {
                yyjson_mut_doc_set_root(raw_doc, copied_root);
                auto copied = from_mutable_doc(raw_doc);
                if(!copied.has_value()) {
                    yyjson_mut_doc_free(raw_doc);
                    return std::unexpected(error_kind::invalid_state);
                } else {
                    return std::move(*copied);
                }
            }
        }
    }
}

inline auto Value::to_json_string() const -> std::expected<std::string, yyjson_write_code> {
    if(!valid()) {
        return std::unexpected(YYJSON_WRITE_ERROR_INVALID_PARAMETER);
    } else {
        yyjson_write_err err{};
        size_t len = 0;
        char* out =
            mutable_ref()
                ? yyjson_mut_val_write_opts(mutable_ptr(), YYJSON_WRITE_NOFLAG, nullptr, &len, &err)
                : yyjson_val_write_opts(immutable_ptr(), YYJSON_WRITE_NOFLAG, nullptr, &len, &err);
        if(out == nullptr) {
            return std::unexpected(err.code);
        } else {
            std::string json(out, len);
            std::free(out);
            return json;
        }
    }
}

inline ValueRef Value::as_ref() const noexcept {
    return ValueRef(tagged_handle());
}

inline Document Value::doc() const noexcept {
    if(!valid() || !has_owner() || !mutable_doc()) {
        return Document(nullptr);
    }
    return Document(*this);
}

inline auto Value::get_array() const noexcept -> std::optional<Array> {
    assert(valid());
    assert(has_owner());
    if(!is_array()) {
        return std::nullopt;
    }
    return Array(tagged_handle(), tagged_doc_handle, ref_count, true);
}

inline auto Value::get_object() const noexcept -> std::optional<Object> {
    assert(valid());
    assert(has_owner());
    if(!is_object()) {
        return std::nullopt;
    }
    return Object(tagged_handle(), tagged_doc_handle, ref_count, true);
}

inline Array Value::as_array() const {
    auto value = get_array();
    assert(value.has_value());
    return *value;
}

inline Object Value::as_object() const {
    auto value = get_object();
    assert(value.has_value());
    return *value;
}

inline auto Value::writable_node() -> std::expected<yyjson_mut_val*, error_kind> {
    auto writable = ensure_writable_doc_and_rebind_root(static_cast<TaggedRef&>(*this));
    if(!writable.has_value()) {
        return std::unexpected(writable.error());
    } else {
        return *writable;
    }
}

template <typename T>
    requires dom_writable_value<T>
inline auto Value::make_mut_value(yyjson_mut_doc* doc, T&& value)
    -> std::expected<yyjson_mut_val*, error_kind> {
    return make_mut_value(doc, std::forward<T>(value), nullptr);
}

template <typename T>
    requires dom_writable_value<T>
inline auto Value::make_mut_value(yyjson_mut_doc* doc, T&& value, yyjson_mut_doc* source_doc)
    -> std::expected<yyjson_mut_val*, error_kind> {
    if(doc == nullptr) {
        return std::unexpected(error_kind::invalid_state);
    } else {
        using writable_type = std::remove_cvref_t<T>;
        yyjson_mut_val* result = nullptr;

        if constexpr(std::same_as<writable_type, std::nullptr_t>) {
            result = yyjson_mut_null(doc);
        } else if constexpr(std::same_as<writable_type, bool>) {
            result = yyjson_mut_bool(doc, static_cast<bool>(value));
        } else if constexpr(std::integral<writable_type> && std::is_signed_v<writable_type> &&
                            !std::same_as<writable_type, bool>) {
            result = yyjson_mut_sint(doc, static_cast<std::int64_t>(value));
        } else if constexpr(std::integral<writable_type> && std::is_unsigned_v<writable_type>) {
            result = yyjson_mut_uint(doc, static_cast<std::uint64_t>(value));
        } else if constexpr(std::floating_point<writable_type>) {
            result = yyjson_mut_real(doc, static_cast<double>(value));
        } else if constexpr(std::same_as<writable_type, std::string_view>) {
            result = yyjson_mut_strncpy(doc, value.data(), value.size());
        } else if constexpr(std::same_as<writable_type, const char*> ||
                            std::same_as<writable_type, char*>) {
            if(value == nullptr) {
                return std::unexpected(error_kind::invalid_state);
            } else {
                auto text = std::string_view(value);
                result = yyjson_mut_strncpy(doc, text.data(), text.size());
            }
        } else if constexpr(dom_writable_char_array_v<T>) {
            auto text = std::string_view(value);
            result = yyjson_mut_strncpy(doc, text.data(), text.size());
        } else if constexpr(std::same_as<writable_type, Value> ||
                            std::same_as<writable_type, Array> ||
                            std::same_as<writable_type, Object>) {
            const auto& source = value;
            if(!source.valid()) {
                return std::unexpected(error_kind::invalid_state);
            } else if(source.mutable_ref() && source_doc != nullptr &&
                      source.mutable_doc_ptr() == source_doc) {
                // Same doc: zero-copy, just reuse the pointer.
                result = source.mutable_ptr();
            } else if(source.mutable_ref()) {
                result = yyjson_mut_val_mut_copy(doc, source.mutable_ptr());
            } else {
                result = yyjson_val_mut_copy(doc, source.immutable_ptr());
            }
        } else {
            static_assert(dom_writable_value_v<T>, "unsupported dom writable value type");
        }

        if(result == nullptr) {
            return std::unexpected(error_kind::allocation_failed);
        } else {
            return result;
        }
    }
}

template <typename T>
    requires dom_writable_value<T>
inline auto Value::set(T&& value) -> status_t {
    auto root = writable_node();
    if(!root.has_value()) {
        return std::unexpected(root.error());
    } else {
        auto* doc = mutable_doc_ptr();
        if(doc == nullptr) {
            return std::unexpected(error_kind::invalid_state);
        } else {
            auto new_root = make_mut_value(doc, std::forward<T>(value), doc);
            if(!new_root.has_value()) {
                return std::unexpected(new_root.error());
            } else {
                yyjson_mut_doc_set_root(doc, *new_root);
                set_tagged_handle(tag_handle(*new_root, true));
                return {};
            }
        }
    }
}

template <typename T>
    requires (dom_writable_value<T> && !std::same_as<std::remove_cvref_t<T>, Value>)
inline auto Value::operator=(T&& value) -> Value& {
    auto status = set(std::forward<T>(value));
    assert(status.has_value());
    return *this;
}

inline Array::Array(std::uintptr_t tagged_value_handle,
                    std::uintptr_t tagged_doc_handle,
                    int* ref_count,
                    bool retain_owner,
                    bool external_owner) noexcept :
    ArrayRef(tagged_value_handle),
    OwnedDoc(tagged_doc_handle, ref_count, retain_owner, external_owner) {}

inline auto Array::from_immutable_doc(yyjson_doc* raw_doc) noexcept -> std::optional<Array> {
    auto value = Value::from_immutable_doc(raw_doc);
    if(!value.has_value()) {
        return std::nullopt;
    }
    return value->get_array();
}

inline auto Array::from_mutable_doc(yyjson_mut_doc* raw_doc) noexcept -> std::optional<Array> {
    auto value = Value::from_mutable_doc(raw_doc);
    if(!value.has_value()) {
        return std::nullopt;
    }
    return value->get_array();
}

inline auto Array::parse(std::string_view json) -> std::expected<Array, yyjson_read_code> {
    auto parsed = Value::parse(json);
    if(!parsed.has_value()) {
        return std::unexpected(parsed.error());
    } else {
        auto array = parsed->get_array();
        if(!array.has_value()) {
            return std::unexpected(YYJSON_READ_ERROR_INVALID_PARAMETER);
        } else {
            return std::move(*array);
        }
    }
}

inline auto Array::to_json_string() const -> std::expected<std::string, yyjson_write_code> {
    return as_value().to_json_string();
}

inline ArrayRef Array::as_ref() const noexcept {
    return ArrayRef(tagged_handle());
}

inline Document Array::doc() const noexcept {
    if(!valid() || !has_owner() || !mutable_doc()) {
        return Document(nullptr);
    }
    return Document(as_value());
}

inline Value Array::as_value() const noexcept {
    return Value(tagged_handle(), tagged_doc_handle, ref_count, true);
}

inline auto Array::writable_node() -> std::expected<yyjson_mut_val*, error_kind> {
    auto writable = ensure_writable_doc_and_rebind_root(static_cast<TaggedRef&>(*this));
    if(!writable.has_value()) {
        return std::unexpected(writable.error());
    } else if(!yyjson_mut_is_arr(*writable)) {
        return std::unexpected(error_kind::type_mismatch);
    } else {
        return *writable;
    }
}

template <typename T>
    requires dom_writable_value<T>
inline auto Array::push_back(T&& value) -> status_t {
    auto array = writable_node();
    if(!array.has_value()) {
        return std::unexpected(array.error());
    } else {
        auto* doc = mutable_doc_ptr();
        if(doc == nullptr) {
            return std::unexpected(error_kind::invalid_state);
        } else {
            auto mut_value = Value::make_mut_value(doc, std::forward<T>(value), doc);
            if(!mut_value.has_value()) {
                return std::unexpected(mut_value.error());
            } else if(!yyjson_mut_arr_add_val(*array, *mut_value)) {
                return std::unexpected(error_kind::write_failed);
            } else {
                return {};
            }
        }
    }
}

template <typename T>
    requires dom_writable_value<T>
inline auto Array::insert(std::size_t index, T&& value) -> status_t {
    auto array = writable_node();
    if(!array.has_value()) {
        return std::unexpected(array.error());
    } else {
        auto size = yyjson_mut_arr_size(*array);
        if(index > size) {
            return std::unexpected(error_kind::index_out_of_bounds);
        } else {
            auto* doc = mutable_doc_ptr();
            if(doc == nullptr) {
                return std::unexpected(error_kind::invalid_state);
            } else {
                auto mut_value = Value::make_mut_value(doc, std::forward<T>(value), doc);
                if(!mut_value.has_value()) {
                    return std::unexpected(mut_value.error());
                } else if(!yyjson_mut_arr_insert(*array, *mut_value, index)) {
                    return std::unexpected(error_kind::write_failed);
                } else {
                    return {};
                }
            }
        }
    }
}

inline Object::Object(std::uintptr_t tagged_value_handle,
                      std::uintptr_t tagged_doc_handle,
                      int* ref_count,
                      bool retain_owner,
                      bool external_owner) noexcept :
    ObjectRef(tagged_value_handle),
    OwnedDoc(tagged_doc_handle, ref_count, retain_owner, external_owner) {}

inline auto Object::from_immutable_doc(yyjson_doc* raw_doc) noexcept -> std::optional<Object> {
    auto value = Value::from_immutable_doc(raw_doc);
    if(!value.has_value()) {
        return std::nullopt;
    }
    return value->get_object();
}

inline auto Object::from_mutable_doc(yyjson_mut_doc* raw_doc) noexcept -> std::optional<Object> {
    auto value = Value::from_mutable_doc(raw_doc);
    if(!value.has_value()) {
        return std::nullopt;
    }
    return value->get_object();
}

inline auto Object::parse(std::string_view json) -> std::expected<Object, yyjson_read_code> {
    auto parsed = Value::parse(json);
    if(!parsed.has_value()) {
        return std::unexpected(parsed.error());
    } else {
        auto object = parsed->get_object();
        if(!object.has_value()) {
            return std::unexpected(YYJSON_READ_ERROR_INVALID_PARAMETER);
        } else {
            return std::move(*object);
        }
    }
}

inline auto Object::to_json_string() const -> std::expected<std::string, yyjson_write_code> {
    return as_value().to_json_string();
}

inline ObjectRef Object::as_ref() const noexcept {
    return ObjectRef(tagged_handle());
}

inline Document Object::doc() const noexcept {
    if(!valid() || !has_owner() || !mutable_doc()) {
        return Document(nullptr);
    }
    return Document(as_value());
}

inline Value Object::as_value() const noexcept {
    return Value(tagged_handle(), tagged_doc_handle, ref_count, true);
}

inline auto Object::writable_node() -> std::expected<yyjson_mut_val*, error_kind> {
    auto writable = ensure_writable_doc_and_rebind_root(static_cast<TaggedRef&>(*this));
    if(!writable.has_value()) {
        return std::unexpected(writable.error());
    } else if(!yyjson_mut_is_obj(*writable)) {
        return std::unexpected(error_kind::type_mismatch);
    } else {
        return *writable;
    }
}

template <typename T>
    requires dom_writable_value<T>
inline auto Object::insert(std::string_view key, T&& value) -> status_t {
    auto object = writable_node();
    if(!object.has_value()) {
        return std::unexpected(object.error());
    } else if(yyjson_mut_obj_getn(*object, key.data(), key.size()) != nullptr) {
        return std::unexpected(error_kind::already_exists);
    } else {
        auto* doc = mutable_doc_ptr();
        if(doc == nullptr) {
            return std::unexpected(error_kind::invalid_state);
        } else {
            auto* key_value = yyjson_mut_strncpy(doc, key.data(), key.size());
            if(key_value == nullptr) {
                return std::unexpected(error_kind::allocation_failed);
            } else {
                auto mut_value = Value::make_mut_value(doc, std::forward<T>(value), doc);
                if(!mut_value.has_value()) {
                    return std::unexpected(mut_value.error());
                } else if(!yyjson_mut_obj_add(*object, key_value, *mut_value)) {
                    return std::unexpected(error_kind::write_failed);
                } else {
                    return {};
                }
            }
        }
    }
}

template <typename T>
    requires dom_writable_value<T>
inline auto Object::assign(std::string_view key, T&& value) -> status_t {
    auto object = writable_node();
    if(!object.has_value()) {
        return std::unexpected(object.error());
    } else {
        auto* doc = mutable_doc_ptr();
        if(doc == nullptr) {
            return std::unexpected(error_kind::invalid_state);
        } else {
            auto* key_value = yyjson_mut_strncpy(doc, key.data(), key.size());
            if(key_value == nullptr) {
                return std::unexpected(error_kind::allocation_failed);
            } else {
                auto mut_value = Value::make_mut_value(doc, std::forward<T>(value), doc);
                if(!mut_value.has_value()) {
                    return std::unexpected(mut_value.error());
                } else if(!yyjson_mut_obj_put(*object, key_value, *mut_value)) {
                    return std::unexpected(error_kind::write_failed);
                } else {
                    return {};
                }
            }
        }
    }
}

// --- Document implementation ---

inline Document::Document() noexcept : doc(yyjson_mut_doc_new(nullptr), yyjson_mut_doc_free) {}

inline Document::Document(std::nullptr_t) noexcept {}

inline Document::Document(Value owner) noexcept : owner_root(std::move(owner)) {}

inline bool Document::valid() const noexcept {
    return raw() != nullptr;
}

inline yyjson_mut_doc* Document::raw() const noexcept {
    if(owner_root.has_value()) {
        return owner_root->mutable_doc_ptr();
    }
    return doc.get();
}

inline Array Document::make_array() {
    assert(valid());
    auto* source_doc = raw();
    assert(source_doc != nullptr);

    auto* detached_doc = yyjson_mut_doc_mut_copy(source_doc, nullptr);
    assert(detached_doc != nullptr);
    if(detached_doc == nullptr) {
        return Array();
    }

    auto* arr = yyjson_mut_arr(detached_doc);
    assert(arr != nullptr);
    if(arr == nullptr) {
        yyjson_mut_doc_free(detached_doc);
        return Array();
    }
    yyjson_mut_doc_set_root(detached_doc, arr);

    // Document keeps owning the mutable tree, wrappers reference it as external owner.
    doc = std::shared_ptr<yyjson_mut_doc>(detached_doc, yyjson_mut_doc_free);
    owner_root.reset();

    int* ref_count = new (std::nothrow) int(1);
    if(ref_count == nullptr) {
        return Array();
    }

    const auto tagged_value_handle = reinterpret_cast<std::uintptr_t>(arr) | std::uintptr_t{1};
    const auto tagged_doc_handle =
        reinterpret_cast<std::uintptr_t>(detached_doc) | std::uintptr_t{1};
    return Array(tagged_value_handle, tagged_doc_handle, ref_count, false, true);
}

inline Object Document::make_object() {
    assert(valid());
    auto* source_doc = raw();
    assert(source_doc != nullptr);

    auto* detached_doc = yyjson_mut_doc_mut_copy(source_doc, nullptr);
    assert(detached_doc != nullptr);
    if(detached_doc == nullptr) {
        return Object();
    }

    auto* obj = yyjson_mut_obj(detached_doc);
    assert(obj != nullptr);
    if(obj == nullptr) {
        yyjson_mut_doc_free(detached_doc);
        return Object();
    }
    yyjson_mut_doc_set_root(detached_doc, obj);

    // Document keeps owning the mutable tree, wrappers reference it as external owner.
    doc = std::shared_ptr<yyjson_mut_doc>(detached_doc, yyjson_mut_doc_free);
    owner_root.reset();

    int* ref_count = new (std::nothrow) int(1);
    if(ref_count == nullptr) {
        return Object();
    }

    const auto tagged_value_handle = reinterpret_cast<std::uintptr_t>(obj) | std::uintptr_t{1};
    const auto tagged_doc_handle =
        reinterpret_cast<std::uintptr_t>(detached_doc) | std::uintptr_t{1};
    return Object(tagged_value_handle, tagged_doc_handle, ref_count, false, true);
}

inline yyjson_mut_val* Document::unchecked_make_array() noexcept {
    auto* raw_doc = raw();
    return raw_doc != nullptr ? yyjson_mut_arr(raw_doc) : nullptr;
}

inline yyjson_mut_val* Document::unchecked_make_object() noexcept {
    auto* raw_doc = raw();
    return raw_doc != nullptr ? yyjson_mut_obj(raw_doc) : nullptr;
}

inline yyjson_mut_val* Document::unchecked_make_null() noexcept {
    auto* raw_doc = raw();
    return raw_doc != nullptr ? yyjson_mut_null(raw_doc) : nullptr;
}

inline yyjson_mut_val* Document::unchecked_make_bool(bool value) noexcept {
    auto* raw_doc = raw();
    return raw_doc != nullptr ? yyjson_mut_bool(raw_doc, value) : nullptr;
}

inline yyjson_mut_val* Document::unchecked_make_int(std::int64_t value) noexcept {
    auto* raw_doc = raw();
    return raw_doc != nullptr ? yyjson_mut_sint(raw_doc, value) : nullptr;
}

inline yyjson_mut_val* Document::unchecked_make_uint(std::uint64_t value) noexcept {
    auto* raw_doc = raw();
    return raw_doc != nullptr ? yyjson_mut_uint(raw_doc, value) : nullptr;
}

inline yyjson_mut_val* Document::unchecked_make_real(double value) noexcept {
    auto* raw_doc = raw();
    return raw_doc != nullptr ? yyjson_mut_real(raw_doc, value) : nullptr;
}

inline yyjson_mut_val* Document::unchecked_make_str(std::string_view value) noexcept {
    auto* raw_doc = raw();
    return raw_doc != nullptr ? yyjson_mut_strncpy(raw_doc, value.data(), value.size()) : nullptr;
}

inline void Document::unchecked_set_root(yyjson_mut_val* root) noexcept {
    auto* raw_doc = raw();
    if(raw_doc != nullptr) {
        yyjson_mut_doc_set_root(raw_doc, root);
    }
}

inline bool Document::unchecked_arr_add_val(yyjson_mut_val* arr, yyjson_mut_val* val) noexcept {
    return yyjson_mut_arr_add_val(arr, val);
}

inline bool Document::unchecked_obj_add(yyjson_mut_val* obj,
                                        yyjson_mut_val* key,
                                        yyjson_mut_val* val) noexcept {
    return yyjson_mut_obj_add(obj, key, val);
}

inline auto Document::dom_value() const -> std::expected<Value, error_kind> {
    if(!valid()) {
        return std::unexpected(error_kind::invalid_state);
    }

    auto* raw_doc = raw();
    if(raw_doc == nullptr) {
        return std::unexpected(error_kind::invalid_state);
    }

    auto* root = yyjson_mut_doc_get_root(raw_doc);
    if(root == nullptr) {
        return std::unexpected(error_kind::invalid_state);
    }

    yyjson_mut_doc* copied_doc = yyjson_mut_doc_mut_copy(raw_doc, nullptr);
    if(copied_doc == nullptr) {
        return std::unexpected(error_kind::allocation_failed);
    }

    auto value = Value::from_mutable_doc(copied_doc);
    if(!value.has_value()) {
        return std::unexpected(error_kind::invalid_state);
    }
    return std::move(*value);
}

inline auto Document::to_json_string() const -> std::expected<std::string, error_kind> {
    if(!valid()) {
        return std::unexpected(error_kind::invalid_state);
    }

    auto* raw_doc = raw();
    if(raw_doc == nullptr) {
        return std::unexpected(error_kind::invalid_state);
    }

    auto* root = yyjson_mut_doc_get_root(raw_doc);
    if(root == nullptr) {
        return std::unexpected(error_kind::invalid_state);
    }

    yyjson_write_err err{};
    size_t len = 0;
    char* out = yyjson_mut_val_write_opts(root, YYJSON_WRITE_NOFLAG, nullptr, &len, &err);
    if(out == nullptr) {
        return std::unexpected(error_kind::write_failed);
    }

    std::string json(out, len);
    std::free(out);
    return json;
}

}  // namespace eventide::serde::content
