#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if __has_include(<yyjson.h>)
#include <yyjson.h>
#else
#error "yyjson.h not found. Enable EVENTIDE_SERDE_ENABLE_YYJSON or add yyjson include paths."
#endif

#include "eventide/serde/json/error.h"

namespace eventide::serde::json {

class ValueRef;
class ArrayRef;
class ObjectRef;
class Value;
class Array;
class Object;
class Builder;

enum class ValueKind : std::uint8_t {
    invalid = 0,
    null_value,
    boolean,
    integer,
    number,
    string,
    array,
    object,
};

template <typename T>
constexpr inline bool dom_writable_char_array_v =
    std::is_array_v<std::remove_reference_t<T>> &&
    std::same_as<std::remove_cv_t<std::remove_extent_t<std::remove_reference_t<T>>>, char>;

template <typename T>
constexpr inline bool dom_writable_value_v =
    std::same_as<std::remove_cvref_t<T>, std::nullptr_t> ||
    std::same_as<std::remove_cvref_t<T>, bool> ||
    (std::integral<std::remove_cvref_t<T>> && std::is_signed_v<std::remove_cvref_t<T>> &&
     !std::same_as<std::remove_cvref_t<T>, bool>) ||
    (std::integral<std::remove_cvref_t<T>> && std::is_unsigned_v<std::remove_cvref_t<T>>) ||
    std::floating_point<std::remove_cvref_t<T>> ||
    std::same_as<std::remove_cvref_t<T>, std::string_view> ||
    std::same_as<std::remove_cvref_t<T>, const char*> ||
    std::same_as<std::remove_cvref_t<T>, char*> || dom_writable_char_array_v<T> ||
    std::same_as<std::remove_cvref_t<T>, Value> || std::same_as<std::remove_cvref_t<T>, Array> ||
    std::same_as<std::remove_cvref_t<T>, Object>;

template <typename T>
concept dom_writable_value = dom_writable_value_v<T>;

class TaggedRef {
public:
    TaggedRef() noexcept = default;

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] bool mutable_ref() const noexcept;

protected:
    constexpr static std::uintptr_t k_mutable_bit = std::uintptr_t{1};

    explicit TaggedRef(const yyjson_val* value) noexcept;

    explicit TaggedRef(const yyjson_mut_val* value) noexcept;

    explicit TaggedRef(std::uintptr_t tagged_handle) noexcept;

    [[nodiscard]] std::uintptr_t tagged_handle() const noexcept;

    [[nodiscard]] void* untagged_ptr() const noexcept;

    [[nodiscard]] yyjson_val* immutable_ptr() const noexcept;

    [[nodiscard]] yyjson_mut_val* mutable_ptr() const noexcept;

    [[nodiscard]] static std::uintptr_t tag_handle(const void* pointer, bool mutable_bit) noexcept;

    void set_tagged_handle(std::uintptr_t tagged_handle) noexcept;

private:
    std::uintptr_t tagged_handle_value = 0;

    friend class OwnedDoc;
};

class ValueRef : public TaggedRef {
public:
    ValueRef() noexcept = default;

    explicit ValueRef(const yyjson_val* value) noexcept;

    explicit ValueRef(const yyjson_mut_val* value) noexcept;

    [[nodiscard]] ValueKind kind() const noexcept;

    [[nodiscard]] bool is_null() const noexcept;

    [[nodiscard]] bool is_bool() const noexcept;

    [[nodiscard]] bool is_int() const noexcept;

    [[nodiscard]] bool is_number() const noexcept;

    [[nodiscard]] bool is_string() const noexcept;

    [[nodiscard]] bool is_array() const noexcept;

    [[nodiscard]] bool is_object() const noexcept;

    [[nodiscard]] std::optional<bool> get_bool() const noexcept;

    [[nodiscard]] std::optional<std::int64_t> get_int() const noexcept;

    [[nodiscard]] std::optional<std::uint64_t> get_uint() const noexcept;

    [[nodiscard]] std::optional<double> get_double() const noexcept;

    [[nodiscard]] std::optional<std::string_view> get_string() const noexcept;

    [[nodiscard]] std::optional<ArrayRef> get_array() const noexcept;

    [[nodiscard]] std::optional<ObjectRef> get_object() const noexcept;

    [[nodiscard]] bool as_bool() const;

    [[nodiscard]] std::int64_t as_int() const;

    [[nodiscard]] std::uint64_t as_uint() const;

    [[nodiscard]] double as_double() const;

    [[nodiscard]] std::string_view as_string() const;

    [[nodiscard]] ArrayRef as_array() const;

    [[nodiscard]] ObjectRef as_object() const;

    void assert_valid() const;

    void assert_kind(ValueKind expected) const;

protected:
    explicit ValueRef(std::uintptr_t tagged_handle) noexcept;

private:
    friend class ArrayRef;
    friend class ObjectRef;
    friend class Value;
};

class ArrayRef : public TaggedRef {
public:
    class iterator;

    ArrayRef() noexcept = default;

    explicit ArrayRef(const yyjson_val* value) noexcept;

    explicit ArrayRef(const yyjson_mut_val* value) noexcept;

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] std::optional<ValueRef> get(std::size_t index) const noexcept;

    [[nodiscard]] ValueRef at(std::size_t index) const;

    [[nodiscard]] ValueRef operator[](std::size_t index) const noexcept;

    void assert_valid() const;

    [[nodiscard]] iterator begin() const noexcept;

    [[nodiscard]] iterator end() const noexcept;

protected:
    explicit ArrayRef(std::uintptr_t tagged_handle) noexcept;

private:
    [[nodiscard]] ValueRef unchecked_get(std::size_t index) const noexcept;

private:
    friend class ValueRef;
    friend class Array;
};

class ArrayRef::iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = ValueRef;
    using difference_type = std::ptrdiff_t;

    iterator() = default;

    [[nodiscard]] value_type operator*() const noexcept;

    auto operator++() noexcept -> iterator&;

    auto operator++(int) noexcept -> iterator;

    [[nodiscard]] bool operator==(const iterator& other) const noexcept;

private:
    iterator(const ArrayRef* owner, std::size_t index) noexcept;

private:
    friend class ArrayRef;

    const ArrayRef* owner = nullptr;
    std::size_t index = 0;
};

class ObjectRef : public TaggedRef {
public:
    struct entry {
        std::string_view key;
        ValueRef value{};
    };

    class iterator;

    ObjectRef() noexcept = default;

    explicit ObjectRef(const yyjson_val* value) noexcept;

    explicit ObjectRef(const yyjson_mut_val* value) noexcept;

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] std::optional<ValueRef> get(std::string_view key) const noexcept;

    [[nodiscard]] bool contains(std::string_view key) const noexcept;

    [[nodiscard]] ValueRef at(std::string_view key) const;

    [[nodiscard]] ValueRef operator[](std::string_view key) const noexcept;

    void assert_valid() const;

    [[nodiscard]] iterator begin() const noexcept;

    [[nodiscard]] iterator end() const noexcept;

protected:
    explicit ObjectRef(std::uintptr_t tagged_handle) noexcept;

private:
    friend class ValueRef;
    friend class Object;
};

class ObjectRef::iterator {
public:
    using iterator_category = std::input_iterator_tag;
    using value_type = entry;
    using difference_type = std::ptrdiff_t;

    iterator() = default;

    [[nodiscard]] value_type operator*() const noexcept;

    auto operator++() noexcept -> iterator&;

    auto operator++(int) noexcept -> iterator;

    [[nodiscard]] bool operator==(const iterator& other) const noexcept;

private:
    iterator(const ObjectRef* owner, bool end) noexcept;

private:
    friend class ObjectRef;

    const ObjectRef* owner = nullptr;
    bool end_flag = true;
    bool mutable_mode = false;
    yyjson_obj_iter immutable_iter{};
    yyjson_mut_obj_iter mutable_iter{};
    yyjson_val* immutable_key = nullptr;
    yyjson_mut_val* mutable_key = nullptr;
};

class OwnedDoc {
public:
    using status_t = std::expected<void, error_kind>;

    OwnedDoc() noexcept = default;

    OwnedDoc(const OwnedDoc& other) noexcept;

    auto operator=(const OwnedDoc& other) noexcept -> OwnedDoc&;

    OwnedDoc(OwnedDoc&& other) noexcept;

    auto operator=(OwnedDoc&& other) noexcept -> OwnedDoc&;

    ~OwnedDoc();

    [[nodiscard]] bool has_owner() const noexcept;

    [[nodiscard]] int use_count() const noexcept;

    [[nodiscard]] bool mutable_doc() const noexcept;

protected:
    OwnedDoc(std::uintptr_t tagged_doc_handle, int* ref_count, bool retain_owner) noexcept;

    constexpr static std::uintptr_t k_mutable_bit = std::uintptr_t{1};
    std::uintptr_t tagged_doc_handle = 0;
    int* ref_count = nullptr;

    [[nodiscard]] static std::uintptr_t tag_doc(const void* pointer, bool mutable_bit) noexcept;

    [[nodiscard]] yyjson_doc* immutable_doc_ptr() const noexcept;

    [[nodiscard]] yyjson_mut_doc* mutable_doc_ptr() const noexcept;

    auto ensure_writable_doc_and_rebind_root(TaggedRef& ref)
        -> std::expected<yyjson_mut_val*, error_kind>;

private:
    void retain() noexcept;

    void release() noexcept;
};

class Value : public ValueRef, private OwnedDoc {
public:
    using status_t = std::expected<void, error_kind>;

    struct parse_options {
        yyjson_read_flag flags = YYJSON_READ_NOFLAG;
        yyjson_alc* allocator = nullptr;
    };

    Value() noexcept = default;

    Value(const Value&) noexcept = default;
    auto operator=(const Value&) noexcept -> Value& = default;
    Value(Value&&) noexcept = default;
    auto operator=(Value&&) noexcept -> Value& = default;
    ~Value() = default;

    static auto parse(std::string_view json) -> std::expected<Value, yyjson_read_code>;

    static auto parse(std::string_view json, parse_options options)
        -> std::expected<Value, yyjson_read_code>;

    static auto from_immutable_doc(yyjson_doc* raw_doc) noexcept -> std::optional<Value>;

    static auto from_mutable_doc(yyjson_mut_doc* raw_doc) noexcept -> std::optional<Value>;

    static auto copy_of(ValueRef source) -> std::expected<Value, error_kind>;

    [[nodiscard]] auto to_json_string() const -> std::expected<std::string, yyjson_write_code>;

    [[nodiscard]] ValueRef as_ref() const noexcept;

    using OwnedDoc::has_owner;
    using OwnedDoc::use_count;
    using OwnedDoc::mutable_doc;

    [[nodiscard]] std::optional<Array> get_array() const noexcept;

    [[nodiscard]] std::optional<Object> get_object() const noexcept;

    [[nodiscard]] Array as_array() const;

    [[nodiscard]] Object as_object() const;

    template <typename T>
        requires dom_writable_value<T>
    auto set(T&& value) -> status_t;

    template <typename T>
        requires (dom_writable_value<T> && !std::same_as<std::remove_cvref_t<T>, Value>)
    auto operator=(T&& value) -> Value&;

private:
    [[nodiscard]] auto writable_node() -> std::expected<yyjson_mut_val*, error_kind>;

    template <typename T>
        requires dom_writable_value<T>
    static auto make_mut_value(yyjson_mut_doc* doc, T&& value)
        -> std::expected<yyjson_mut_val*, error_kind>;

    Value(std::uintptr_t tagged_value_handle,
          std::uintptr_t tagged_doc_handle,
          int* ref_count,
          bool retain_owner) noexcept;

private:
    friend class Array;
    friend class Object;
    friend class Builder;
};

class Array : public ArrayRef, private OwnedDoc {
public:
    using status_t = std::expected<void, error_kind>;

    Array() noexcept = default;

    Array(const Array&) noexcept = default;
    auto operator=(const Array&) noexcept -> Array& = default;
    Array(Array&&) noexcept = default;
    auto operator=(Array&&) noexcept -> Array& = default;
    ~Array() = default;

    static auto from_immutable_doc(yyjson_doc* raw_doc) noexcept -> std::optional<Array>;

    static auto from_mutable_doc(yyjson_mut_doc* raw_doc) noexcept -> std::optional<Array>;

    static auto parse(std::string_view json) -> std::expected<Array, yyjson_read_code>;

    [[nodiscard]] auto to_json_string() const -> std::expected<std::string, yyjson_write_code>;

    [[nodiscard]] ArrayRef as_ref() const noexcept;

    using OwnedDoc::has_owner;
    using OwnedDoc::use_count;
    using OwnedDoc::mutable_doc;

    [[nodiscard]] Value as_value() const noexcept;

    template <typename T>
        requires dom_writable_value<T>
    auto push_back(T&& value) -> status_t;

    template <typename T>
        requires dom_writable_value<T>
    auto insert(std::size_t index, T&& value) -> status_t;

private:
    [[nodiscard]] auto writable_node() -> std::expected<yyjson_mut_val*, error_kind>;

    Array(std::uintptr_t tagged_value_handle,
          std::uintptr_t tagged_doc_handle,
          int* ref_count,
          bool retain_owner) noexcept;

private:
    friend class Value;
};

class Object : public ObjectRef, private OwnedDoc {
public:
    using status_t = std::expected<void, error_kind>;

    Object() noexcept = default;

    Object(const Object&) noexcept = default;
    auto operator=(const Object&) noexcept -> Object& = default;
    Object(Object&&) noexcept = default;
    auto operator=(Object&&) noexcept -> Object& = default;
    ~Object() = default;

    static auto from_immutable_doc(yyjson_doc* raw_doc) noexcept -> std::optional<Object>;

    static auto from_mutable_doc(yyjson_mut_doc* raw_doc) noexcept -> std::optional<Object>;

    static auto parse(std::string_view json) -> std::expected<Object, yyjson_read_code>;

    [[nodiscard]] auto to_json_string() const -> std::expected<std::string, yyjson_write_code>;

    [[nodiscard]] ObjectRef as_ref() const noexcept;

    using OwnedDoc::has_owner;
    using OwnedDoc::use_count;
    using OwnedDoc::mutable_doc;

    [[nodiscard]] Value as_value() const noexcept;

    template <typename T>
        requires dom_writable_value<T>
    auto insert(std::string_view key, T&& value) -> status_t;

    template <typename T>
        requires dom_writable_value<T>
    auto assign(std::string_view key, T&& value) -> status_t;

private:
    [[nodiscard]] auto writable_node() -> std::expected<yyjson_mut_val*, error_kind>;

    Object(std::uintptr_t tagged_value_handle,
           std::uintptr_t tagged_doc_handle,
           int* ref_count,
           bool retain_owner) noexcept;

private:
    friend class Value;
};

class Builder {
public:
    using status_t = std::expected<void, error_kind>;

    Builder() noexcept;

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] error_kind error() const noexcept;

    [[nodiscard]] bool complete() const noexcept;

    auto begin_object() -> status_t;

    auto end_object() -> status_t;

    auto begin_array() -> status_t;

    auto end_array() -> status_t;

    auto key(std::string_view key_name) -> status_t;

    template <typename T>
        requires dom_writable_value<T>
    auto value(T&& value) -> status_t;

    auto value_ref(ValueRef value) -> status_t;

    [[nodiscard]] auto dom_value() const -> std::expected<Value, error_kind>;

    [[nodiscard]] auto to_json_string() const -> std::expected<std::string, error_kind>;

private:
    enum class container_kind : std::uint8_t { array, object };

    struct container_frame {
        container_kind kind;
        yyjson_mut_val* value = nullptr;
        bool expect_key = true;
        std::string pending_key;
    };

    auto append_mut_value(yyjson_mut_val* value) -> status_t;

    void mark_invalid(error_kind error = error_kind::invalid_state) noexcept;

private:
    bool is_valid = true;
    bool root_is_written = false;
    error_kind last_error = error_kind::invalid_state;
    std::vector<container_frame> stack;
    std::shared_ptr<yyjson_mut_doc> doc;
};

}  // namespace eventide::serde::json

#ifndef EVENTIDE_SERDE_JSON_DOM_INL_INCLUDED
#define EVENTIDE_SERDE_JSON_DOM_INL_INCLUDED 1
#include "eventide/serde/json/dom.inl"
#endif
