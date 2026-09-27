#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "kota/support/config.h"
#include "kota/codec/visit/encode.h"

namespace kota::codec::dyn {

class Value;
class Array;
class Object;
class Cursor;

enum class ValueKind : std::uint8_t {
    null_value = 0,
    boolean = 1,
    signed_int = 2,
    unsigned_int = 3,
    floating = 4,
    string = 5,
    array = 6,
    object = 7,
};

namespace detail {

inline std::string_view kind_name(ValueKind kind) noexcept {
    switch(kind) {
        case ValueKind::null_value: return "null";
        case ValueKind::boolean: return "boolean";
        case ValueKind::signed_int: return "signed_int";
        case ValueKind::unsigned_int: return "unsigned_int";
        case ValueKind::floating: return "float";
        case ValueKind::string: return "string";
        case ValueKind::array: return "array";
        case ValueKind::object: return "object";
    }
    return "unknown";
}

}  // namespace detail

class Array {
public:
    using value_type = Value;
    using size_type = std::size_t;
    using iterator = typename std::vector<Value>::iterator;
    using const_iterator = typename std::vector<Value>::const_iterator;

    Array();
    Array(const Array&);
    Array(Array&&) noexcept;
    auto operator=(const Array&) -> Array&;
    auto operator=(Array&&) noexcept -> Array&;
    ~Array();

    explicit Array(std::vector<Value> items);
    Array(std::initializer_list<Value> items);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    void clear() noexcept;
    void reserve(std::size_t n);

    [[nodiscard]] const Value& operator[](std::size_t index) const noexcept;
    [[nodiscard]] Value& operator[](std::size_t index) noexcept;

    [[nodiscard]] const Value& at(std::size_t index) const;
    [[nodiscard]] Value& at(std::size_t index);

    void push_back(Value value);

    template <typename... Args>
    auto emplace_back(Args&&... args) -> Value&;

    [[nodiscard]] iterator begin() noexcept;
    [[nodiscard]] iterator end() noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;
    [[nodiscard]] const_iterator cbegin() const noexcept;
    [[nodiscard]] const_iterator cend() const noexcept;

    bool operator==(const Array& other) const;

private:
    std::vector<Value> items;
};

class Object {
public:
    using entry = std::pair<std::string, Value>;
    using key_type = std::string;
    using mapped_type = Value;
    using container_t = std::vector<entry>;
    using iterator = typename container_t::iterator;
    using const_iterator = typename container_t::const_iterator;

    Object();
    Object(const Object&);
    Object(Object&&) noexcept;
    auto operator=(const Object&) -> Object&;
    auto operator=(Object&&) noexcept -> Object&;
    ~Object();

    Object(std::initializer_list<entry> entries);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    void clear() noexcept;
    void reserve(std::size_t n);

    [[nodiscard]] bool contains(std::string_view key) const;

    [[nodiscard]] const Value* find(std::string_view key) const;
    [[nodiscard]] Value* find(std::string_view key);

    [[nodiscard]] const Value& at(std::string_view key) const;
    [[nodiscard]] Value& at(std::string_view key);

    void insert(std::string key, Value value);
    void assign(std::string_view key, Value value);
    std::size_t remove(std::string_view key);

    [[nodiscard]] iterator begin() noexcept;
    [[nodiscard]] iterator end() noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;

    bool operator==(const Object& other) const;

private:
    void invalidate_index() noexcept;
    void ensure_index() const;

    container_t entries;
    mutable std::optional<std::unordered_map<std::string_view, std::size_t>> index;
};

class Value {
public:
    using variant_type = std::variant<std::monostate,
                                      bool,
                                      std::int64_t,
                                      std::uint64_t,
                                      double,
                                      std::string,
                                      Array,
                                      Object>;

    Value() noexcept : data(std::monostate{}) {}

    Value(const Value&) = default;
    Value(Value&&) noexcept = default;
    auto operator=(const Value&) -> Value& = default;
    auto operator=(Value&&) noexcept -> Value& = default;
    ~Value() = default;

    Value(std::nullptr_t) noexcept : data(std::monostate{}) {}

    Value(bool v) noexcept : data(v) {}

    Value(std::int64_t v) noexcept : data(v) {}

    Value(std::uint64_t v) noexcept : data(v) {}

    Value(double v) noexcept : data(v) {}

    Value(const char* v) : data(std::string(v)) {}

    Value(std::string_view v) : data(std::string(v)) {}

    Value(std::string v) : data(std::move(v)) {}

    Value(Array v) : data(std::move(v)) {}

    Value(Object v) : data(std::move(v)) {}

    Value(std::initializer_list<Object::entry> entries);

    template <std::integral T>
        requires (!std::same_as<T, bool> && !std::same_as<T, char> &&
                  !std::same_as<T, std::int64_t> && !std::same_as<T, std::uint64_t>)
    Value(T v) noexcept {
        if constexpr(std::is_signed_v<T>) {
            data = static_cast<std::int64_t>(v);
        } else {
            data = static_cast<std::uint64_t>(v);
        }
    }

    [[nodiscard]] ValueKind kind() const noexcept {
        return static_cast<ValueKind>(data.index());
    }

    [[nodiscard]] bool is_null() const noexcept {
        return std::holds_alternative<std::monostate>(data);
    }

    [[nodiscard]] bool is_bool() const noexcept {
        return std::holds_alternative<bool>(data);
    }

    [[nodiscard]] bool is_int() const noexcept {
        return std::holds_alternative<std::int64_t>(data) ||
               std::holds_alternative<std::uint64_t>(data);
    }

    [[nodiscard]] bool is_number() const noexcept {
        return is_int() || std::holds_alternative<double>(data);
    }

    [[nodiscard]] bool is_string() const noexcept {
        return std::holds_alternative<std::string>(data);
    }

    [[nodiscard]] bool is_array() const noexcept {
        return std::holds_alternative<Array>(data);
    }

    [[nodiscard]] bool is_object() const noexcept {
        return std::holds_alternative<Object>(data);
    }

    [[nodiscard]] std::optional<bool> get_bool() const noexcept {
        if(const auto* p = std::get_if<bool>(&data)) {
            return *p;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::int64_t> get_int() const noexcept {
        if(const auto* p = std::get_if<std::int64_t>(&data)) {
            return *p;
        }
        if(const auto* p = std::get_if<std::uint64_t>(&data)) {
            if(*p <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
                return static_cast<std::int64_t>(*p);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint64_t> get_uint() const noexcept {
        if(const auto* p = std::get_if<std::uint64_t>(&data)) {
            return *p;
        }
        if(const auto* p = std::get_if<std::int64_t>(&data)) {
            if(*p >= 0) {
                return static_cast<std::uint64_t>(*p);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<double> get_double() const noexcept {
        if(const auto* p = std::get_if<double>(&data)) {
            return *p;
        }
        if(const auto* p = std::get_if<std::int64_t>(&data)) {
            return static_cast<double>(*p);
        }
        if(const auto* p = std::get_if<std::uint64_t>(&data)) {
            return static_cast<double>(*p);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string_view> get_string() const noexcept {
        if(const auto* p = std::get_if<std::string>(&data)) {
            return std::string_view(*p);
        }
        return std::nullopt;
    }

    [[nodiscard]] const Array* get_array() const noexcept {
        return std::get_if<Array>(&data);
    }

    [[nodiscard]] Array* get_array() noexcept {
        return std::get_if<Array>(&data);
    }

    [[nodiscard]] const Object* get_object() const noexcept {
        return std::get_if<Object>(&data);
    }

    [[nodiscard]] Object* get_object() noexcept {
        return std::get_if<Object>(&data);
    }

    [[nodiscard]] bool as_bool() const {
        return std::get<bool>(data);
    }

    [[nodiscard]] std::int64_t as_int() const {
        return std::get<std::int64_t>(data);
    }

    [[nodiscard]] std::uint64_t as_uint() const {
        return std::get<std::uint64_t>(data);
    }

    [[nodiscard]] double as_double() const {
        return std::get<double>(data);
    }

    [[nodiscard]] std::string_view as_string() const {
        const auto& s = std::get<std::string>(data);
        return std::string_view(s);
    }

    [[nodiscard]] const Array& as_array() const {
        return std::get<Array>(data);
    }

    [[nodiscard]] Array& as_array() {
        return std::get<Array>(data);
    }

    [[nodiscard]] const Object& as_object() const {
        return std::get<Object>(data);
    }

    [[nodiscard]] Object& as_object() {
        return std::get<Object>(data);
    }

    [[nodiscard]] Cursor cursor() const noexcept;

    [[nodiscard]] Cursor operator[](std::string_view key) const;
    [[nodiscard]] Cursor operator[](std::size_t index) const;

    [[nodiscard]] const variant_type& variant() const noexcept {
        return data;
    }

    bool operator==(const Value& other) const {
        return data == other.data;
    }

private:
    variant_type data;
};

inline Value::Value(std::initializer_list<Object::entry> entries) : data(Object(entries)) {}

class Cursor {
public:
    Cursor() noexcept = default;

    explicit Cursor(const Value& value) noexcept : ptr(&value) {}

    explicit Cursor(const Value* value) noexcept : ptr(value) {}

    [[nodiscard]] bool valid() const noexcept {
        return ptr != nullptr;
    }

    explicit operator bool() const noexcept {
        return ptr != nullptr;
    }

    [[nodiscard]] bool has_error() const noexcept {
        return !message.empty();
    }

    [[nodiscard]] std::string_view error() const noexcept {
        return message;
    }

    [[nodiscard]] std::optional<ValueKind> kind() const noexcept {
        return ptr != nullptr ? std::optional{ptr->kind()} : std::nullopt;
    }

    [[nodiscard]] bool is_null() const noexcept {
        return ptr != nullptr && ptr->is_null();
    }

    [[nodiscard]] bool is_bool() const noexcept {
        return ptr != nullptr && ptr->is_bool();
    }

    [[nodiscard]] bool is_int() const noexcept {
        return ptr != nullptr && ptr->is_int();
    }

    [[nodiscard]] bool is_number() const noexcept {
        return ptr != nullptr && ptr->is_number();
    }

    [[nodiscard]] bool is_string() const noexcept {
        return ptr != nullptr && ptr->is_string();
    }

    [[nodiscard]] bool is_array() const noexcept {
        return ptr != nullptr && ptr->is_array();
    }

    [[nodiscard]] bool is_object() const noexcept {
        return ptr != nullptr && ptr->is_object();
    }

    [[nodiscard]] std::optional<bool> get_bool() const noexcept {
        return ptr != nullptr ? ptr->get_bool() : std::nullopt;
    }

    [[nodiscard]] std::optional<std::int64_t> get_int() const noexcept {
        return ptr != nullptr ? ptr->get_int() : std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint64_t> get_uint() const noexcept {
        return ptr != nullptr ? ptr->get_uint() : std::nullopt;
    }

    [[nodiscard]] std::optional<double> get_double() const noexcept {
        return ptr != nullptr ? ptr->get_double() : std::nullopt;
    }

    [[nodiscard]] std::optional<std::string_view> get_string() const noexcept {
        return ptr != nullptr ? ptr->get_string() : std::nullopt;
    }

    [[nodiscard]] const Array* get_array() const noexcept {
        return ptr != nullptr ? ptr->get_array() : nullptr;
    }

    [[nodiscard]] const Object* get_object() const noexcept {
        return ptr != nullptr ? ptr->get_object() : nullptr;
    }

    [[nodiscard]] bool as_bool() const {
        assert_valid();
        return ptr->as_bool();
    }

    [[nodiscard]] std::int64_t as_int() const {
        assert_valid();
        return ptr->as_int();
    }

    [[nodiscard]] std::uint64_t as_uint() const {
        assert_valid();
        return ptr->as_uint();
    }

    [[nodiscard]] double as_double() const {
        assert_valid();
        return ptr->as_double();
    }

    [[nodiscard]] std::string_view as_string() const {
        assert_valid();
        return ptr->as_string();
    }

    [[nodiscard]] const Array& as_array() const {
        assert_valid();
        return ptr->as_array();
    }

    [[nodiscard]] const Object& as_object() const {
        assert_valid();
        return ptr->as_object();
    }

    [[nodiscard]] Cursor operator[](std::string_view key) const {
        if(ptr != nullptr) {
            return (*ptr)[key];
        }
        return make_error(message.empty() ? std::format(R"(["{}"])", key)
                                          : std::format(R"({} -> ["{}"])", message, key));
    }

    [[nodiscard]] Cursor operator[](std::size_t index) const {
        if(ptr != nullptr) {
            return (*ptr)[index];
        }
        return make_error(message.empty() ? std::format("[{}]", index)
                                          : std::format("{} -> [{}]", message, index));
    }

    void assert_valid() const {
        assert(ptr != nullptr);
    }

private:
    friend class Value;

    static Cursor make_error(std::string text) noexcept {
        Cursor c;
        c.message = std::move(text);
        return c;
    }

    const Value* ptr = nullptr;
    std::string message;
};

inline Cursor Value::cursor() const noexcept {
    return Cursor(*this);
}

inline Cursor Value::operator[](std::string_view key) const {
    if(const Object* obj = get_object()) {
        if(const Value* v = obj->find(key)) {
            return Cursor(*v);
        }
        return Cursor::make_error(std::format(R"(missing key "{}")", key));
    }
    return Cursor::make_error(std::format("expected object, got {}", detail::kind_name(kind())));
}

inline Cursor Value::operator[](std::size_t index) const {
    if(const Array* arr = get_array()) {
        if(index < arr->size()) {
            return Cursor((*arr)[index]);
        }
        return Cursor::make_error(
            std::format("index {} out of range (size {})", index, arr->size()));
    }
    return Cursor::make_error(std::format("expected array, got {}", detail::kind_name(kind())));
}

inline Array::Array() = default;
inline Array::Array(const Array&) = default;
inline Array::Array(Array&&) noexcept = default;
inline auto Array::operator=(const Array&) -> Array& = default;
inline auto Array::operator=(Array&&) noexcept -> Array& = default;
inline Array::~Array() = default;

inline Array::Array(std::vector<Value> items) : items(std::move(items)) {}

inline Array::Array(std::initializer_list<Value> items) : items(items) {}

inline std::size_t Array::size() const noexcept {
    return items.size();
}

inline bool Array::empty() const noexcept {
    return items.empty();
}

inline void Array::clear() noexcept {
    items.clear();
}

inline void Array::reserve(std::size_t n) {
    items.reserve(n);
}

const inline Value& Array::operator[](std::size_t index) const noexcept {
    return items[index];
}

inline Value& Array::operator[](std::size_t index) noexcept {
    return items[index];
}

const inline Value& Array::at(std::size_t index) const {
    return items.at(index);
}

inline Value& Array::at(std::size_t index) {
    return items.at(index);
}

inline void Array::push_back(Value value) {
    items.push_back(std::move(value));
}

template <typename... Args>
inline auto Array::emplace_back(Args&&... args) -> Value& {
    return items.emplace_back(std::forward<Args>(args)...);
}

inline auto Array::begin() noexcept -> iterator {
    return items.begin();
}

inline auto Array::end() noexcept -> iterator {
    return items.end();
}

inline auto Array::begin() const noexcept -> const_iterator {
    return items.begin();
}

inline auto Array::end() const noexcept -> const_iterator {
    return items.end();
}

inline auto Array::cbegin() const noexcept -> const_iterator {
    return items.cbegin();
}

inline auto Array::cend() const noexcept -> const_iterator {
    return items.cend();
}

inline bool Array::operator==(const Array& other) const {
    return items == other.items;
}

inline Object::Object() = default;

inline Object::Object(const Object& other) : entries(other.entries), index(std::nullopt) {}

inline Object::Object(Object&& other) noexcept :
    entries(std::move(other.entries)), index(std::nullopt) {
    other.index.reset();
}

inline auto Object::operator=(const Object& other) -> Object& {
    if(this != &other) {
        entries = other.entries;
        index.reset();
    }
    return *this;
}

inline auto Object::operator=(Object&& other) noexcept -> Object& {
    if(this != &other) {
        entries = std::move(other.entries);
        index.reset();
        other.index.reset();
    }
    return *this;
}

inline Object::~Object() = default;

inline Object::Object(std::initializer_list<entry> entries) : entries(entries) {}

inline std::size_t Object::size() const noexcept {
    return entries.size();
}

inline bool Object::empty() const noexcept {
    return entries.empty();
}

inline void Object::clear() noexcept {
    entries.clear();
    invalidate_index();
}

inline void Object::reserve(std::size_t n) {
    entries.reserve(n);
    invalidate_index();
}

inline void Object::invalidate_index() noexcept {
    index.reset();
}

inline void Object::ensure_index() const {
    if(index.has_value()) {
        return;
    }
    // Small objects: linear scan is faster than hash table overhead.
    if(entries.size() <= 16) {
        return;
    }
    index.emplace();
    index->reserve(entries.size());
    for(std::size_t i = 0; i < entries.size(); ++i) {
        (*index)[std::string_view(entries[i].first)] = i;
    }
}

inline bool Object::contains(std::string_view key) const {
    return find(key) != nullptr;
}

const inline Value* Object::find(std::string_view key) const {
    ensure_index();
    if(index.has_value()) {
        auto it = index->find(key);
        return it != index->end() ? &entries[it->second].second : nullptr;
    }
    // Linear scan for small objects (index not built).
    for(auto it = entries.rbegin(); it != entries.rend(); ++it) {
        if(it->first == key) {
            return &it->second;
        }
    }
    return nullptr;
}

inline Value* Object::find(std::string_view key) {
    return const_cast<Value*>(std::as_const(*this).find(key));
}

const inline Value& Object::at(std::string_view key) const {
    const Value* v = find(key);
    if(v == nullptr) {
        KOTA_THROW(std::out_of_range("kota::codec::dyn::Object::at: missing key"));
    }
    return *v;
}

inline Value& Object::at(std::string_view key) {
    Value* v = find(key);
    if(v == nullptr) {
        KOTA_THROW(std::out_of_range("kota::codec::dyn::Object::at: missing key"));
    }
    return *v;
}

inline void Object::insert(std::string key, Value value) {
    entries.emplace_back(std::move(key), std::move(value));
    invalidate_index();
}

inline void Object::assign(std::string_view key, Value value) {
    if(Value* existing = find(key)) {
        *existing = std::move(value);
        return;
    }
    entries.emplace_back(std::string(key), std::move(value));
    invalidate_index();
}

inline std::size_t Object::remove(std::string_view key) {
    auto before = entries.size();
    std::erase_if(entries, [&](const entry& e) { return e.first == key; });
    auto removed = before - entries.size();
    if(removed != 0) {
        invalidate_index();
    }
    return removed;
}

inline auto Object::begin() noexcept -> iterator {
    return entries.begin();
}

inline auto Object::end() noexcept -> iterator {
    return entries.end();
}

inline auto Object::begin() const noexcept -> const_iterator {
    return entries.begin();
}

inline auto Object::end() const noexcept -> const_iterator {
    return entries.end();
}

inline bool Object::operator==(const Object& other) const {
    return entries.size() == other.entries.size() &&
           std::ranges::is_permutation(entries, other.entries);
}

}  // namespace kota::codec::dyn

namespace kota::codec {

/// Any visitor encodes a tree as the value it holds. Declared with the type,
/// so that every translation unit that sees a Value also sees how it encodes:
/// one that did not would print it as opaque, and differ from the others.
template <typename Vis, typename Config>
struct serialize_visit<Vis, dyn::Value, Config> {
    static bool visit(Vis& vis, const dyn::Value& value) {
        return std::visit(
            [&](const auto& stored) -> bool { return encode_value<Config>(vis, stored); },
            value.variant());
    }
};

}  // namespace kota::codec
