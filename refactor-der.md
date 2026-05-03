# Codec Deserializer Redesign: Zero-Overhead Visitor Architecture

## 1. Motivation

The current `Deserializer` class uses a stateful design where a `current_value` pointer bridges iteration and value reading. This adds per-element overhead (branch + pointer store/load + `std::expected` wrapping) that accounts for the 1.76x performance gap vs handwritten simdjson code.

The new design eliminates this by passing values directly as parameters (no state), using the backend's native error type on the hot path, and letting the backend drive traversal via a visitor pattern.

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│  Public API: from_json<T>(json) -> std::expected<T, error>   │
│  On error: re-parse with diagnostic visitor to collect path   │
└───────────────────────┬──────────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────────┐
│  deserialize<Backend>(source, T& out) -> Backend::error_type │
│  Pure type dispatch via if constexpr                          │
│  Handles: primitives, string, optional, vector, array,        │
│           tuple, map, variant, struct                          │
└───────────────────────┬──────────────────────────────────────┘
                        │ (for structs)
┌───────────────────────▼──────────────────────────────────────┐
│  Backend::deserialize_object(source, visitor)                 │
│  Backend drives: iterates fields, calls visitor per entry     │
└───────────────────────┬──────────────────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────────────────┐
│  struct_visitor<Backend, T, Config>                            │
│  - string_match_invoke for field lookup                        │
│  - Attribute dispatch (skip_if, with/as, default, etc.)       │
│  - Calls deserialize<Backend>(val, field_ref) recursively     │
│  - Tracks seen_fields bitmask for required field validation   │
└──────────────────────────────────────────────────────────────┘
```

## 3. Backend Trait

Each backend (JSON/simdjson, TOML, content, bincode, streaming JSON, etc.) implements this trait as a struct with static methods.

```cpp
namespace kota::codec {

// Concept: what a backend must provide
template <typename B>
concept deserialize_backend = requires {
    typename B::value_type;   // The "source" — a DOM node OR a stream cursor
    typename B::error_type;   // Native error (simdjson::error_code, toml_error, etc.)
    { B::success } -> std::convertible_to<typename B::error_type>;
};

}  // namespace kota::codec
```

### 3.1 Required Static Methods

```cpp
struct example_backend {
    using value_type  = ...;  // simdjson::ondemand::value / stream_reader& / etc.
    using error_type  = ...;  // simdjson::error_code / custom enum / etc.
    static constexpr error_type success = ...;

    // === Primitive reads ===
    static error_type read_bool(value_type& src, bool& out);
    static error_type read_int64(value_type& src, std::int64_t& out);
    static error_type read_uint64(value_type& src, std::uint64_t& out);
    static error_type read_double(value_type& src, double& out);
    static error_type read_string(value_type& src, std::string_view& out);
    // read_string returns a view; caller copies if needed (e.g., into std::string)

    // === Null check ===
    static error_type read_is_null(value_type& src, bool& is_null);

    // === Compound traversal (backend drives) ===
    template <typename Visitor>
    static error_type visit_object(value_type& src, Visitor&& vis);
    // Contract: calls vis.visit_field(std::string_view key, value_type& val) per field
    // Returns first non-success error from visitor, or iteration error

    template <typename Visitor>
    static error_type visit_array(value_type& src, Visitor&& vis);
    // Contract: calls vis.visit_element(value_type& val) per element
    // Returns first non-success error from visitor, or iteration error

    // === Type introspection (for variant scoring) ===
    static meta::type_kind kind_of(value_type& src);
    // O(1) peek at the value's type without consuming it

    // === Lightweight scoring traversal (for variant selection) ===
    template <typename Visitor>
    static error_type visit_object_keys(value_type& src, Visitor&& vis);
    // Contract: calls vis.on_field(std::string_view key, meta::type_kind kind, value_type& val)
    // per field. Visitor only needs key+kind for scoring; val is available for recursive
    // scoring but need not be consumed. Resets cursor after traversal.
    // For lazy-parse backends: kind_of is a peek (O(1)), values not consumed are auto-skipped.

    template <typename Visitor>
    static error_type visit_array_keys(value_type& src, Visitor&& vis);
    // Contract: calls vis.on_element(std::size_t idx, meta::type_kind kind, value_type& val)

    // === Optional: positional struct access (bincode-like) ===
    // template <typename Visitor>
    // static error_type visit_struct_positional(value_type& src, Visitor&& vis);
    // Contract: calls vis.visit_positional(value_type& src)
};
```

### 3.2 simdjson Backend Implementation

```cpp
namespace kota::codec::json {

struct simdjson_backend {
    using value_type = simdjson::ondemand::value;
    using error_type = simdjson::error_code;
    static constexpr error_type success = simdjson::SUCCESS;

    static error_type read_bool(value_type& v, bool& out) {
        return v.get_bool().get(out);
    }
    static error_type read_int64(value_type& v, std::int64_t& out) {
        return v.get_int64().get(out);
    }
    static error_type read_uint64(value_type& v, std::uint64_t& out) {
        return v.get_uint64().get(out);
    }
    static error_type read_double(value_type& v, double& out) {
        return v.get_double().get(out);
    }
    static error_type read_string(value_type& v, std::string_view& out) {
        return v.get_string().get(out);
    }
    static error_type read_is_null(value_type& v, bool& is_null) {
        is_null = v.is_null();
        return simdjson::SUCCESS;
    }

    template <typename Visitor>
    static error_type visit_object(value_type& src, Visitor&& vis) {
        simdjson::ondemand::object obj;
        auto err = src.get_object().get(obj);
        if(err != simdjson::SUCCESS) [[unlikely]] return err;
        for(auto field_result: obj) {
            simdjson::ondemand::field field;
            err = std::move(field_result).get(field);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
            std::string_view key;
            err = field.unescaped_key().get(key);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
            value_type val = field.value();
            err = vis.visit_field(key, val);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
        }
        return simdjson::SUCCESS;
    }

    template <typename Visitor>
    static error_type visit_array(value_type& src, Visitor&& vis) {
        simdjson::ondemand::array arr;
        auto err = src.get_array().get(arr);
        if(err != simdjson::SUCCESS) [[unlikely]] return err;
        for(auto elem_result: arr) {
            value_type elem;
            err = std::move(elem_result).get(elem);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
            err = vis.visit_element(elem);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
        }
        return simdjson::SUCCESS;
    }

    // === Type introspection ===
    static meta::type_kind kind_of(value_type& src) {
        // type() is O(1) peek — does NOT consume the value
        switch(src.type()) {
            case simdjson::ondemand::json_type::object:  return meta::type_kind::object;
            case simdjson::ondemand::json_type::array:   return meta::type_kind::array;
            case simdjson::ondemand::json_type::string:  return meta::type_kind::string;
            case simdjson::ondemand::json_type::number:  return meta::type_kind::number;
            case simdjson::ondemand::json_type::boolean: return meta::type_kind::boolean;
            case simdjson::ondemand::json_type::null:    return meta::type_kind::null;
        }
    }

    // === Lightweight scoring traversal ===
    template <typename Visitor>
    static error_type visit_object_keys(value_type& src, Visitor&& vis) {
        simdjson::ondemand::object obj;
        auto err = src.get_object().get(obj);
        if(err != simdjson::SUCCESS) [[unlikely]] return err;
        for(auto field_result: obj) {
            simdjson::ondemand::field field;
            err = std::move(field_result).get(field);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
            std::string_view key;
            err = field.unescaped_key().get(key);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
            value_type val = field.value();
            meta::type_kind kind = kind_of(val);
            err = vis.on_field(key, kind, val);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
            // val not consumed by visitor → simdjson auto-skips on next iteration
        }
        // Reset so the value can be re-traversed for actual deserialization
        return obj.reset();
    }

    template <typename Visitor>
    static error_type visit_array_keys(value_type& src, Visitor&& vis) {
        simdjson::ondemand::array arr;
        auto err = src.get_array().get(arr);
        if(err != simdjson::SUCCESS) [[unlikely]] return err;
        std::size_t i = 0;
        for(auto elem_result: arr) {
            value_type elem;
            err = std::move(elem_result).get(elem);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
            meta::type_kind kind = kind_of(elem);
            err = vis.on_element(i, kind, elem);
            if(err != simdjson::SUCCESS) [[unlikely]] return err;
            ++i;
        }
        return simdjson::SUCCESS;
    }
};

}  // namespace kota::codec::json
```

### 3.3 Stream Backend (future: hand-written JSON parser)

```cpp
struct json_stream_backend {
    using value_type = json_stream_reader&;  // Mutable reference to cursor
    using error_type = parse_error;
    static constexpr error_type success = parse_error::ok;

    static error_type read_double(value_type reader, double& out) {
        return reader.consume_number(out);
    }

    template <typename Visitor>
    static error_type visit_object(value_type reader, Visitor&& vis) {
        auto err = reader.expect('{');
        if(err != success) return err;
        while(!reader.peek('}')) {
            std::string_view key;
            err = reader.consume_string(key);
            if(err != success) return err;
            err = reader.expect(':');
            if(err != success) return err;
            err = vis.visit_field(key, reader);  // Same reader, now at value position
            if(err != success) return err;
            reader.skip_comma();
        }
        return reader.expect('}');
    }
};
```

The visitor receives the same `reader` reference — after `deserialize<Backend>(reader, field)` returns, the reader has advanced past the value. **Same visitor code works for both DOM and stream backends.**

### 3.4 Positional Backend (bincode)

```cpp
struct bincode_backend {
    using value_type = bincode_reader&;
    using error_type = bincode_error;
    static constexpr error_type success = bincode_error::ok;

    // No visit_object — bincode has no keys
    // Instead: visit_struct_positional
    template <typename Visitor>
    static error_type visit_struct_positional(value_type reader, Visitor&& vis) {
        return vis.visit_positional(reader);
    }

    template <typename Visitor>
    static error_type visit_array(value_type reader, Visitor&& vis) {
        std::uint64_t len;
        auto err = read_uint64(reader, len);
        if(err != success) return err;
        for(std::uint64_t i = 0; i < len; ++i) {
            err = vis.visit_element(reader);
            if(err != success) return err;
        }
        return success;
    }
};
```

## 4. Core Dispatch: `deserialize<Backend>(source, T& out)`

```cpp
namespace kota::codec {

template <typename Backend, typename T>
auto deserialize(typename Backend::value_type& src, T& out)
    -> typename Backend::error_type;

}  // namespace kota::codec
```

### 4.1 Type Dispatch Logic

```cpp
template <typename Backend, typename T>
auto deserialize(typename Backend::value_type& src, T& out)
    -> typename Backend::error_type
{
    using U = std::remove_cvref_t<T>;
    using E = typename Backend::error_type;

    // 1. Check for user-provided specialization
    if constexpr(has_custom_deserialize<Backend, U>) {
        return custom_deserialize<Backend, U>::read(src, out);
    }
    // 2. Bool
    else if constexpr(std::same_as<U, bool>) {
        return Backend::read_bool(src, out);
    }
    // 3. Signed integers
    else if constexpr(std::signed_integral<U> && !std::same_as<U, bool>) {
        std::int64_t v;
        auto err = Backend::read_int64(src, v);
        if(err != Backend::success) return err;
        // Range check omitted on fast path; can add as policy
        out = static_cast<U>(v);
        return Backend::success;
    }
    // 4. Unsigned integers
    else if constexpr(std::unsigned_integral<U> && !std::same_as<U, bool>) {
        std::uint64_t v;
        auto err = Backend::read_uint64(src, v);
        if(err != Backend::success) return err;
        out = static_cast<U>(v);
        return Backend::success;
    }
    // 5. Floating point
    else if constexpr(std::floating_point<U>) {
        double d;
        auto err = Backend::read_double(src, d);
        if(err != Backend::success) return err;
        out = static_cast<U>(d);
        return Backend::success;
    }
    // 6. String
    else if constexpr(std::same_as<U, std::string>) {
        std::string_view sv;
        auto err = Backend::read_string(src, sv);
        if(err != Backend::success) return err;
        out.assign(sv.data(), sv.size());
        return Backend::success;
    }
    // 7. Optional
    else if constexpr(is_specialization_of<std::optional, U>) {
        bool is_null = false;
        auto err = Backend::read_is_null(src, is_null);
        if(err != Backend::success) return err;
        if(is_null) { out.reset(); return Backend::success; }
        using inner_t = typename U::value_type;
        out.emplace();
        return deserialize<Backend>(src, *out);
    }
    // 8. Vector / sequence containers
    else if constexpr(is_vector_like<U>) {
        out.clear();
        seq_visitor<Backend, typename U::value_type, U> vis{out};
        return Backend::visit_array(src, vis);
    }
    // 9. Fixed-size array (std::array<T,N>)
    else if constexpr(is_std_array<U>) {
        array_visitor<Backend, U> vis{out};
        return Backend::visit_array(src, vis);
    }
    // 10. Tuple
    else if constexpr(tuple_like<U>) {
        tuple_visitor<Backend, U> vis{out};
        return Backend::visit_array(src, vis);
    }
    // 11. Map
    else if constexpr(is_map_like<U>) {
        out.clear();
        map_visitor<Backend, U> vis{out};
        return Backend::visit_object(src, vis);
    }
    // 12. Enum (as integer)
    else if constexpr(std::is_enum_v<U>) {
        // Default: read as underlying integer
        using underlying_t = std::underlying_type_t<U>;
        underlying_t v;
        auto err = deserialize<Backend>(src, v);
        if(err != Backend::success) return err;
        out = static_cast<U>(v);
        return Backend::success;
    }
    // 13. Variant (untagged — heuristic dispatch)
    else if constexpr(is_specialization_of<std::variant, U>) {
        return deserialize_variant_untagged<Backend>(src, out);
    }
    // 14. Reflectable struct
    else if constexpr(meta::reflectable_class<U>) {
        return deserialize_struct<Backend, U>(src, out);
    }
    else {
        static_assert(dependent_false<T>, "unsupported type for deserialization");
    }
}
```

### 4.2 Notes

- `has_custom_deserialize<Backend, T>` checks if user provided a specialization point (replaces `deserialize_traits`)
- Range checking for integer narrowing is intentionally omitted from the happy path. The slow error-path re-parse can catch these.
- For `float`: just cast from double. No range check needed (IEEE semantics).
- The function is NOT marked `KOTA_ALWAYS_INLINE` — let the compiler decide per type.

## 5. Struct Deserialization

### 5.1 Entry Point

```cpp
template <typename Backend, typename T, typename Config = meta::default_config>
auto deserialize_struct(typename Backend::value_type& src, T& out)
    -> typename Backend::error_type
{
    if constexpr(has_visit_object<Backend>) {
        // Named access (JSON, TOML, stream parsers)
        struct_visitor<Backend, T, Config> vis{out};
        auto err = Backend::visit_object(src, vis);
        if(err != Backend::success) return err;
        return vis.finish();
    } else {
        // Positional access (bincode)
        struct_visitor<Backend, T, Config> vis{out};
        auto err = Backend::visit_struct_positional(src, vis);
        if(err != Backend::success) return err;
        return vis.finish();
    }
}
```

### 5.2 struct_visitor

```cpp
template <typename Backend, typename T, typename Config>
struct struct_visitor {
    using E = typename Backend::error_type;
    using schema = meta::virtual_schema<T, Config>;
    using table = detail::field_name_table<T, Config>;
    using slots = typename schema::slots;

    T& out;
    std::uint64_t seen_fields = 0;

    // Called by backend for each field (named access)
    E visit_field(std::string_view key, typename Backend::value_type& val) {
        E err = Backend::success;
        bool matched = string_match_invoke<table::names>(key, [&](auto I_const) {
            constexpr std::size_t name_idx = I_const.value;
            constexpr std::size_t slot_idx = table::slot_map[name_idx];
            err = dispatch_slot<slot_idx>(val);
            seen_fields |= (std::uint64_t(1) << slot_idx);
        });

        if(!matched) {
            if constexpr(schema::deny_unknown || config_deny_unknown_v<Config>) {
                return /* error: unknown field */;
            }
            // For DOM backends, the value is already consumed by iteration.
            // For stream backends, we need to skip the value:
            if constexpr(requires { Backend::skip_value(val); }) {
                return Backend::skip_value(val);
            }
        }
        return err;
    }

    // Called by backend for positional access (bincode)
    E visit_positional(typename Backend::value_type& src) {
        return [&]<std::size_t... Is>(std::index_sequence<Is...>) -> E {
            E err = Backend::success;
            ((err = dispatch_slot<Is>(src), err == Backend::success) && ...);
            return err;
        }(std::make_index_sequence<type_list_size_v<slots>>{});
    }

    // Post-iteration: check required fields
    E finish() {
        constexpr std::uint64_t required = schema_required_field_mask<T, Config>();
        if constexpr(required != 0) {
            if((seen_fields & required) != required) [[unlikely]] {
                return /* error: missing required field */;
            }
        }
        return Backend::success;
    }

private:
    template <std::size_t I>
    E dispatch_slot(typename Backend::value_type& val) {
        using slot_t = type_list_element_t<I, slots>;
        using raw_t = std::remove_cv_t<typename slot_t::raw_type>;
        using attrs_t = typename slot_t::attrs;

        constexpr std::size_t offset = schema::fields[I].offset;
        auto* base = reinterpret_cast<std::byte*>(std::addressof(out));
        auto& field_ref = *reinterpret_cast<raw_t*>(base + offset);

        return deserialize_field<Backend, raw_t, attrs_t>(val, field_ref);
    }
};
```

### 5.3 Field-Level Attribute Dispatch

```cpp
// Generic field deserialize with attribute handling
template <typename Backend, typename RawT, typename Attrs>
auto deserialize_field(typename Backend::value_type& val, RawT& field_ref)
    -> typename Backend::error_type
{
    using E = typename Backend::error_type;

    // 1. skip_if (for deserialization: skip reading if predicate says so)
    if constexpr(tuple_has_spec_v<Attrs, meta::behavior::skip_if>) {
        using pred = typename tuple_find_spec_t<Attrs, meta::behavior::skip_if>::predicate;
        if(meta::evaluate_skip_predicate<pred>(field_ref, false)) {
            // Skip this field's value without reading
            if constexpr(requires { Backend::skip_value(val); }) {
                return Backend::skip_value(val);
            }
            return Backend::success;
        }
    }

    // 2. behavior::with<Adapter> — custom adapter
    if constexpr(has_with_adapter<Attrs>) {
        using Adapter = get_with_adapter_t<Attrs>;
        if constexpr(requires { Adapter::deserialize; }) {
            // Adapter provides full custom deserialize
            return Adapter::template deserialize<Backend>(val, field_ref);
        } else if constexpr(requires { Adapter::from_wire; }) {
            // Adapter uses wire_type intermediate
            using wire_t = typename Adapter::wire_type;
            wire_t wire{};
            auto err = deserialize<Backend>(val, wire);
            if(err != Backend::success) return err;
            field_ref = Adapter::from_wire(std::move(wire));
            return Backend::success;
        }
    }
    // 3. behavior::as<Target> — read as different type, construct from it
    else if constexpr(has_as_adapter<Attrs>) {
        using Target = get_as_target_t<Attrs>;
        Target temp{};
        auto err = deserialize<Backend>(val, temp);
        if(err != Backend::success) return err;
        field_ref = RawT(std::move(temp));
        return Backend::success;
    }
    // 4. behavior::enum_string<Policy> — read string, map to enum
    else if constexpr(has_enum_string<Attrs>) {
        using Policy = get_enum_string_policy_t<Attrs>;
        std::string_view text;
        auto err = Backend::read_string(val, text);
        if(err != Backend::success) return err;
        auto mapped = spelling::map_string_to_enum<RawT, Policy>(text);
        if(!mapped) return /* error: invalid enum string */;
        field_ref = *mapped;
        return Backend::success;
    }
    // 5. tagged variant
    else if constexpr(is_specialization_of<std::variant, RawT> &&
                      tuple_any_of_v<Attrs, meta::is_tagged_attr>) {
        return deserialize_tagged_variant<Backend, Attrs>(val, field_ref);
    }
    // 6. Default: recursive deserialize
    else {
        return deserialize<Backend>(val, field_ref);
    }
}
```

## 6. Sequence Visitors

### 6.1 Vector Visitor

```cpp
template <typename Backend, typename ElemT, typename Container>
struct seq_visitor {
    using E = typename Backend::error_type;
    Container& out;

    E visit_element(typename Backend::value_type& val) {
        ElemT element{};
        auto err = deserialize<Backend>(val, element);
        if(err != Backend::success) [[unlikely]] return err;
        out.push_back(std::move(element));
        return Backend::success;
    }
};
```

### 6.2 Fixed Array Visitor

```cpp
template <typename Backend, typename ArrayT>
struct array_visitor {
    using E = typename Backend::error_type;
    using ElemT = typename ArrayT::value_type;
    static constexpr std::size_t N = std::tuple_size_v<ArrayT>;

    ArrayT& out;
    std::size_t index = 0;

    E visit_element(typename Backend::value_type& val) {
        if(index < N) {
            auto err = deserialize<Backend>(val, out[index]);
            if(err != Backend::success) return err;
        }
        ++index;
        return Backend::success;
    }
};
```

### 6.3 Map Visitor

```cpp
template <typename Backend, typename MapT>
struct map_visitor {
    using E = typename Backend::error_type;
    using key_t = typename MapT::key_type;
    using mapped_t = typename MapT::mapped_type;

    MapT& out;

    E visit_field(std::string_view key, typename Backend::value_type& val) {
        auto parsed_key = spelling::parse_map_key<key_t>(key);
        if(!parsed_key) return /* error */;
        mapped_t mapped{};
        auto err = deserialize<Backend>(val, mapped);
        if(err != Backend::success) return err;
        out.emplace(std::move(*parsed_key), std::move(mapped));
        return Backend::success;
    }
};
```

## 7. Tagged Variant Deserialization

### 7.1 External Tagging

Object with one field whose key is the variant tag name:
```json
{"Circle": {"radius": 5.0}}
```

```cpp
template <typename Backend, typename Attrs, typename... Ts>
auto deserialize_externally_tagged(typename Backend::value_type& src,
                                    std::variant<Ts...>& out) -> E {
    struct external_tag_visitor {
        std::variant<Ts...>& out;
        E err = Backend::success;
        bool found = false;

        E visit_field(std::string_view key, typename Backend::value_type& val) {
            if(found) return /* error: multiple fields */;
            found = true;
            err = match_and_deserialize_alt<Backend, Ts...>(key, val, out);
            return err;
        }
    };
    external_tag_visitor vis{out};
    auto err = Backend::visit_object(src, vis);
    if(err != Backend::success) return err;
    if(!vis.found) return /* error: empty object */;
    return vis.err;
}
```

### 7.2 Internal Tagging

Object where one field is the tag, rest are the struct fields:
```json
{"type": "Circle", "radius": 5.0}
```

Requires backend to support scanning for a specific field. Two strategies:
- If backend supports `scan_field`: peek at tag, then deserialize entire object as the matched struct
- Otherwise: buffer the object and replay

```cpp
template <typename Backend, typename Attrs, typename... Ts>
auto deserialize_internally_tagged(...) -> E {
    // Strategy depends on backend capability
    if constexpr(requires { Backend::scan_field(src, key); }) {
        // Peek tag field without consuming the object
        auto tag_value = Backend::scan_field(src, tag_field_name);
        // Match tag, then deserialize full struct
    } else {
        // Read all fields, find tag, then dispatch
        // (Requires backend buffering or two-pass)
    }
}
```

### 7.3 Adjacent Tagging

Two named fields: one for tag, one for content:
```json
{"type": "Circle", "data": {"radius": 5.0}}
```

**关键改进**：新架构下不再需要 buffer raw JSON string。Backend 推 field 时 content 的 `value_type` 还未被消费，可以直接暂存 `value_type` 本身（对 simdjson 来说是一个轻量 cursor），等 tag 到了再 visit。

```cpp
template <typename Backend, typename Attrs, typename... Ts>
auto deserialize_adjacently_tagged(typename Backend::value_type& src,
                                    std::variant<Ts...>& out) -> E {
    using value_type = typename Backend::value_type;
    using tag_attr = /* extracted from Attrs */;
    constexpr std::string_view tag_field = tag_attr::tag_field_name;
    constexpr std::string_view content_field = tag_attr::content_field_name;

    struct adjacent_visitor {
        std::variant<Ts...>& out;
        E err = Backend::success;

        std::string_view tag_value{};
        bool has_tag = false;
        bool has_content = false;

        // For out-of-order: hold the content value_type
        // (NOT raw JSON string — just the cursor/node itself)
        std::optional<value_type> deferred_content{};

        E visit_field(std::string_view key, value_type& val) {
            if(key == tag_field) {
                std::string_view sv;
                auto e = Backend::read_string(val, sv);
                if(e != Backend::success) return e;
                tag_value = sv;
                has_tag = true;

                // If content was deferred (appeared before tag), process now
                if(has_content && deferred_content) {
                    err = match_and_deserialize_alt<Backend, Ts...>(
                        tag_value, *deferred_content, out);
                    return err;
                }
            } else if(key == content_field) {
                has_content = true;
                if(has_tag) {
                    // Tag already known — deserialize immediately
                    err = match_and_deserialize_alt<Backend, Ts...>(
                        tag_value, val, out);
                    return err;
                } else {
                    // Tag not yet seen — defer the content value_type
                    // For DOM backends: just store the value (lightweight cursor)
                    // For stream backends: need Backend::buffer_value() support
                    if constexpr(requires { Backend::buffer_value(val); }) {
                        deferred_content.emplace(Backend::buffer_value(val));
                    } else {
                        deferred_content.emplace(val);
                    }
                }
            }
            return Backend::success;
        }
    };
    adjacent_visitor vis{out};
    auto err = Backend::visit_object(src, vis);
    if(err != Backend::success) return err;
    if(!vis.has_tag || !vis.has_content) return /* error: missing tag/content */;
    return vis.err;
}
```

**与旧架构对比：**
- 旧：content 先于 tag 出现 → `buffer_raw_field_value()` 缓存 raw JSON string → 创建子 deserializer `replay_buffered_field()` 重新 parse
- 新：暂存 `value_type` 本身（simdjson ondemand value 是轻量 cursor，~16 bytes）→ tag 到了直接 visit，零重新解析

### 7.4 simdjson Backend 的 `scan_field` 支持

```cpp
// For internal tagging: peek a specific field without consuming the object
static error_type scan_field(value_type& src, std::string_view field_name,
                             std::string_view& out) {
    simdjson::ondemand::object obj;
    auto err = src.get_object().get(obj);
    if(err != simdjson::SUCCESS) return err;
    simdjson::ondemand::value val;
    err = obj.find_field(field_name).get(val);
    if(err != simdjson::SUCCESS) return err;
    err = val.get_string().get(out);
    if(err != simdjson::SUCCESS) return err;
    // Reset object for subsequent full traversal
    err = obj.reset();
    return err;
}
```

## 7B. Untagged Variant Selection (Multi-Score)

### 7B.1 设计原则：source_adapter 被 Backend 吸收

旧架构维护一套独立的 `source_adapter` 抽象（`simdjson_source_adapter`、`toml_source_adapter`、`content_source_adapter`），专门给 variant 评分用。新架构中 **source_adapter 被完全消除**，其功能被 Backend trait 自然吸收：

| source_adapter 接口 | 新架构中的对应 |
|---|---|
| `Adapter::kind_of(node)` | `Backend::kind_of(value_type&)` — Backend trait 新增方法 |
| `Adapter::for_each_field(node, cb)` | `Backend::visit_object_keys(value_type&, visitor)` — 轻量评分遍历 |
| `Adapter::for_each_element(node, cb)` | `Backend::visit_array_keys(value_type&, visitor)` — 轻量评分遍历 |

**为什么能合并：** Backend 本来就需要实现 `visit_object` / `visit_array` 给反序列化用，评分只是用同一套遍历能力 + 更轻量的接口。不需要维护平行的 adapter 层。

### 7B.2 Backend Trait 新增：评分支持方法

```cpp
struct simdjson_backend {
    // ... 已有方法 ...

    // === 新增：类型判断（用于 variant kind 过滤）===
    static meta::type_kind kind_of(value_type& src) {
        // simdjson: type() 是 O(1) peek，不消费内容
        switch(src.type()) {
            case simdjson::ondemand::json_type::object:  return meta::type_kind::object;
            case simdjson::ondemand::json_type::array:   return meta::type_kind::array;
            case simdjson::ondemand::json_type::string:  return meta::type_kind::string;
            case simdjson::ondemand::json_type::number:  return meta::type_kind::number;
            case simdjson::ondemand::json_type::boolean: return meta::type_kind::boolean;
            case simdjson::ondemand::json_type::null:    return meta::type_kind::null;
        }
    }

    // === 新增：轻量评分遍历 ===
    // 只推 (key, kind) 对，不要求 visitor 消费 value
    // 对 lazy backend：kind_of 是 peek（O(1)），value 自动 skip
    template <typename Visitor>
    static error_type visit_object_keys(value_type& src, Visitor&& vis) {
        simdjson::ondemand::object obj;
        auto err = src.get_object().get(obj);
        if(err != simdjson::SUCCESS) return err;
        for(auto field_result: obj) {
            simdjson::ondemand::field field;
            err = std::move(field_result).get(field);
            if(err != simdjson::SUCCESS) return err;
            std::string_view key;
            err = field.unescaped_key().get(key);
            if(err != simdjson::SUCCESS) return err;
            value_type val = field.value();
            meta::type_kind kind = kind_of(val);
            // Visitor 只收到 key + kind + val
            // val 用于递归评分时深入子对象；不递归时不消费
            err = vis.on_field(key, kind, val);
            if(err != simdjson::SUCCESS) return err;
        }
        // Reset object so subsequent deserialization can re-iterate
        return obj.reset();
    }

    // 数组版：推 (index, kind) 对
    template <typename Visitor>
    static error_type visit_array_keys(value_type& src, Visitor&& vis) {
        simdjson::ondemand::array arr;
        auto err = src.get_array().get(arr);
        if(err != simdjson::SUCCESS) return err;
        std::size_t i = 0;
        for(auto elem_result: arr) {
            value_type elem;
            err = std::move(elem_result).get(elem);
            if(err != simdjson::SUCCESS) return err;
            meta::type_kind kind = kind_of(elem);
            err = vis.on_element(i, kind, elem);
            if(err != simdjson::SUCCESS) return err;
            ++i;
        }
        return simdjson::SUCCESS;  // array doesn't need reset for scoring
    }

    // === 新增：cursor reset（评分后重新遍历用）===
    static error_type reset_value(value_type& src);  // 对 simdjson: 内部 reset
};
```

**关键设计：`visit_object_keys` 推 `(key, kind, value_type&)`，而非仅 `(key, kind)`。**

原因：评分 visitor 在大部分情况下只看 `key` 和 `kind`（O(1) 操作），但需要递归评分嵌套结构体时，可以对 `val` 调用 `Backend::visit_object_keys(val, sub_scorer)` 深入子对象。不递归时 `val` 不被消费，Backend 迭代自动跳过它。

**对 lazy parse backend 的保证：**
- `kind_of(val)` 对 simdjson 是 `val.type()` — 纯 peek，O(1)
- 不递归的字段：visitor 收到 `val` 但不调用任何方法，Backend 自动 advance past
- 递归的字段：调 `visit_object_keys(val, sub)` 进入子对象评分，同样只 peek

### 7B.3 Scoring Visitor

```cpp
template <typename Backend, typename... Ts>
struct scoring_visitor {
    using E = typename Backend::error_type;
    using value_type = typename Backend::value_type;

    std::array<int, sizeof...(Ts)>& scores;
    std::uint64_t live_mask;  // bit i = 1 means alternative i is still viable

    E on_field(std::string_view key, meta::type_kind kind, value_type& val) {
        // For each live candidate:
        //   - If candidate is a struct with a field matching `key`: +1
        //   - If that field's expected kind matches `kind`: +1
        //   - If kind == object and candidate's field is also a struct:
        //     recurse with sub-scorer on `val`
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ((score_field_for_alt<Is>(key, kind, val)), ...);
        }(std::index_sequence_for<Ts...>{});
        return Backend::success;
    }

    E on_element(std::size_t idx, meta::type_kind kind, value_type& val) {
        // Array scoring: check if candidates expect array element of this kind
        return Backend::success;
    }

private:
    template <std::size_t I>
    void score_field_for_alt(std::string_view key, meta::type_kind kind, value_type& val) {
        if(!(live_mask & (std::uint64_t(1) << I))) return;

        using alt_t = type_list_element_t<I, type_list<Ts...>>;
        if constexpr(meta::reflectable_class<alt_t>) {
            using schema = meta::virtual_schema<alt_t, meta::default_config>;

            // Check if this struct has a field with name `key`
            auto field_idx = schema_lookup_field<alt_t, meta::default_config>(key);
            if(field_idx) {
                scores[I] += 1;  // field name match

                // Bonus: check kind compatibility
                // (Could compare expected field type's kind with actual `kind`)

                // Recursive scoring for nested structs:
                if(kind == meta::type_kind::object) {
                    using field_type = /* raw_type of field at *field_idx */;
                    if constexpr(meta::reflectable_class<field_type>) {
                        // 递归：用 sub-scorer 深入子对象
                        std::array<int, 1> sub_scores{};
                        scoring_visitor<Backend, field_type> sub{sub_scores, 0x1};
                        Backend::visit_object_keys(val, sub);
                        scores[I] += sub_scores[0];
                    }
                }
            } else {
                // Unknown field — penalize if deny_unknown
                if constexpr(schema::deny_unknown) {
                    live_mask &= ~(std::uint64_t(1) << I);
                }
            }
        }
    }
};
```

### 7B.4 Variant Selection Entry Point

```cpp
template <typename Backend, typename... Ts>
auto select_variant_index(typename Backend::value_type& src)
    -> std::optional<std::size_t>
{
    using E = typename Backend::error_type;

    // Step 1: Kind-based filtering (O(1))
    meta::type_kind kind = Backend::kind_of(src);
    std::uint64_t live_mask = accepts_kind_mask<Ts...>(kind);
    std::size_t pop = __builtin_popcountll(live_mask);
    if(pop == 0) return std::nullopt;
    if(pop == 1) return std::optional<std::size_t>(__builtin_ctzll(live_mask));

    // Step 2: Structure-based scoring (for object/array types)
    std::array<int, sizeof...(Ts)> scores{};
    if(kind == meta::type_kind::object) {
        scoring_visitor<Backend, Ts...> sv{scores, live_mask};
        Backend::visit_object_keys(src, sv);
        // After scoring, src cursor is reset (visit_object_keys calls reset internally)
    }

    // Step 3: Pick best
    std::size_t best = 0;
    int best_score = -1;
    for(std::size_t i = 0; i < sizeof...(Ts); ++i) {
        if((live_mask & (std::uint64_t(1) << i)) && scores[i] > best_score) {
            best_score = scores[i];
            best = i;
        }
    }
    return best;
}
```

### 7B.5 Untagged Variant Deserialization

```cpp
template <typename Backend, typename... Ts>
auto deserialize_variant_untagged(typename Backend::value_type& src,
                                   std::variant<Ts...>& out) -> E {
    auto idx = select_variant_index<Backend, Ts...>(src);
    if(!idx) return /* error: no matching variant alternative */;

    // Deserialize into the selected alternative
    return deserialize_variant_at<Backend>(*idx, src, out);
}

template <typename Backend, typename... Ts>
auto deserialize_variant_at(std::size_t idx,
                            typename Backend::value_type& src,
                            std::variant<Ts...>& out) -> E {
    E err = /* error: type_mismatch */;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        ((Is == idx ? (err = [&] {
            using alt_t = type_list_element_t<Is, type_list<Ts...>>;
            alt_t alt{};
            auto e = deserialize<Backend>(src, alt);
            if(e != Backend::success) return e;
            out = std::move(alt);
            return Backend::success;
        }(), true) : false) || ...);
    }(std::index_sequence_for<Ts...>{});
    return err;
}
```

### 7B.6 旧 source_adapter 的完整消除清单

| 删除的内容 | 原因 |
|---|---|
| `source_adapter` concept | 被 `Backend::kind_of` + `Backend::visit_object_keys` 替代 |
| `simdjson_source_adapter` struct | 功能合入 `simdjson_backend` |
| `toml_source_adapter` struct | 功能合入 `toml_backend` |
| `content_source_adapter` struct | 功能合入 `content_backend` |
| `Adapter::for_each_field(node, cb)` | → `Backend::visit_object_keys(src, visitor)` |
| `Adapter::for_each_element(node, cb)` | → `Backend::visit_array_keys(src, visitor)` |
| `Adapter::kind_of(node)` | → `Backend::kind_of(src)` |
| `buffer_raw_field_value()` on deserializer | adjacent tagging 改用暂存 `value_type` |
| `replay_buffered_field()` on deserializer | 不再需要重新 parse raw JSON |
| `can_buffer_raw_field<D>` concept | 删除，统一路径 |
| `pending_object` / `pending_array` 全局状态 | visitor 架构无全局状态 |

**net 效果：variant 相关代码从 ~800 行（variant_dispatch.h）缩减到 ~300 行，且逻辑更清晰。**

## 8. Error Handling

### 8.1 Happy Path

The entire deserialization chain returns `Backend::error_type` directly. For simdjson this is `simdjson::error_code` — a simple integer enum. **Zero heap allocation, zero string formatting on success.**

### 8.2 Error Path (Option C: re-parse for diagnostics)

```cpp
namespace kota::codec::json {

template <typename T, typename Config = meta::default_config>
auto from_json(simdjson::padded_string_view json, T& out)
    -> std::expected<void, serde_error<error_kind>>
{
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    auto doc_err = parser.iterate(json).get(doc);
    if(doc_err != simdjson::SUCCESS) {
        return std::unexpected(make_parse_error(doc_err));
    }

    simdjson::ondemand::value root_val;
    // ... extract root value from doc ...

    auto err = deserialize<simdjson_backend>(root_val, out);
    if(err == simdjson::SUCCESS) return {};

    // === Slow path: re-parse to locate error ===
    return std::unexpected(diagnose_error<T, Config>(json, err));
}

}  // namespace kota::codec::json
```

The `diagnose_error` function re-parses the JSON with a diagnostic visitor that tracks the full field path (e.g., `"another_object.nested_object.v3s[2][1]"`) and produces a rich `serde_error`.

### 8.3 Alternative: Inline Error Context (compile-time opt-in)

For users who want errors without re-parsing, provide a `Backend` wrapper that tracks context:

```cpp
template <typename InnerBackend>
struct diagnostic_backend {
    using value_type = typename InnerBackend::value_type;
    using error_type = serde_error<typename InnerBackend::error_type>;
    // Wraps all calls, prepends field/index on error
};
```

This is opt-in and has overhead — default is the fast path.

## 9. Document Root Handling

`simdjson::ondemand::document` and `simdjson::ondemand::value` have different types. The entry point handles this:

```cpp
template <typename T, typename Config = meta::default_config>
auto from_json(simdjson::padded_string_view json, T& out) -> ... {
    // ... parse document ...
    if constexpr(meta::reflectable_class<T>) {
        // For structs: get_object on document, use visit_map directly
        simdjson::ondemand::object obj;
        auto err = doc.get_object().get(obj);
        if(err != simdjson::SUCCESS) return ...;
        struct_visitor<simdjson_backend, T, Config> vis{out};
        // Call the object iteration directly (not through visit_object)
        // because we have an object, not a value
        err = simdjson_backend::visit_object_from_obj(obj, vis);
        ...
    } else {
        // For primitives/arrays: use document methods directly
        // This is a rare case; handle with a document_adapter
    }
}
```

Alternatively, add `visit_object_from_document` to the backend:
```cpp
static error_type visit_object_from_document(
    simdjson::ondemand::document& doc, Visitor&& vis);
```

## 10. Flatten Attribute

The `flatten` attribute causes a nested struct's fields to appear in the parent's wire format. This is already handled by `virtual_schema` — flattened fields appear as slots in the parent with correct offsets. The `field_name_table` includes them. So `struct_visitor` handles flatten **transparently** — no special code needed.

## 11. Integration with Existing Reflection

The new system uses exactly the same reflection infrastructure:
- `meta::virtual_schema<T, Config>` for field metadata
- `meta::reflectable_class<T>` concept
- `detail::field_name_table<T, Config>` for name lookup tables
- `string_match_invoke<Names>(key, fn)` for O(1) field dispatch
- `schema_required_field_mask<T, Config>()` for required field checking

**No changes needed to the reflection system.**

## 12. User Extension Point

Replace `deserialize_traits<D, T>` with a simpler trait:

```cpp
namespace kota::codec {

// User specializes this to provide custom deserialization
template <typename Backend, typename T>
struct custom_deserialize;
// Must provide: static error_type read(value_type& src, T& out);

template <typename Backend, typename T>
concept has_custom_deserialize = requires(typename Backend::value_type& v, T& out) {
    { custom_deserialize<Backend, T>::read(v, out) } -> std::same_as<typename Backend::error_type>;
};

}  // namespace kota::codec
```

## 13. File Layout

```
include/kota/codec/
├── backend.h                    # Backend concept + kind_of/visit_object_keys (NEW)
├── deserialize.h                # Core deserialize<Backend>() dispatch (NEW)
├── visitors/
│   ├── struct_visitor.h         # struct_visitor + deserialize_field (NEW)
│   ├── seq_visitor.h            # seq_visitor, array_visitor, tuple_visitor (NEW)
│   ├── map_visitor.h            # map_visitor (NEW)
│   ├── variant_visitor.h       # Tagged variant visitors (external/adjacent/internal) (NEW)
│   └── variant_scoring.h       # scoring_visitor + select_variant_index (NEW)
├── json/
│   ├── backend.h               # simdjson_backend (NEW)
│   ├── deserializer.h          # DELETED (replaced by backend.h + deserialize.h)
│   ├── error.h                 # Keep as-is
│   ├── json.h                  # Public API (update from_json to use new path)
│   └── serializer.h            # Keep as-is (serialization unchanged)
├── detail/
│   ├── deser_dispatch.h        # DELETED (replaced by deserialize.h)
│   ├── struct_deserialize.h    # DELETED (replaced by visitors/struct_visitor.h)
│   ├── variant_dispatch.h      # DELETED (replaced by visitors/variant_visitor.h + variant_scoring.h)
│   └── ...                     # Keep other detail headers
```

---

## 14. Migration Plan

### Phase 1: New Core (no breaking changes)

1. **Create `include/kota/codec/deserialize.h`** — the new `deserialize<Backend>()` dispatch function
2. **Create `include/kota/codec/json/backend.h`** — `simdjson_backend` implementation
3. **Create `include/kota/codec/visitors/struct_visitor.h`** — struct visitor with attribute handling
4. **Create `include/kota/codec/visitors/seq_visitor.h`** — vector/array/tuple visitors
5. **Create `include/kota/codec/visitors/map_visitor.h`** — map visitor
6. **Add `fast_from_json<T>()` entry point** that uses the new path

### Phase 2: Benchmark Validation

7. **Update `benchmarks/json_benchmark.cpp`** — add `BM_read_new` benchmark using `fast_from_json`
8. **Run benchmarks** — verify new path matches handwritten performance (target: <1.1x ratio)
9. **Run all existing tests** — verify correctness via test that compares old vs new output

### Phase 3: Variant Support

10. **Create `include/kota/codec/visitors/variant_scoring.h`** — `scoring_visitor` + `select_variant_index` (reuses `Backend::visit_object_keys` + `Backend::kind_of`)
11. **Create `include/kota/codec/visitors/variant_visitor.h`** — external/adjacent/internal tagging visitors
12. **Add `kind_of`, `visit_object_keys`, `visit_array_keys` to simdjson_backend**
13. **Add `scan_field` capability to simdjson_backend** for internal tagging
14. **Test with existing variant test cases** — verify all 3 tagged strategies + untagged scoring

### Phase 4: Switch Over

15. **Update `from_json()` to use new path** — replace old Deserializer usage
16. **Update other backends** (content, TOML) to implement `deserialize_backend` trait (including `kind_of` + `visit_object_keys`)
17. **Delete old files**: `detail/deser_dispatch.h`, `detail/struct_deserialize.h`, `detail/variant_dispatch.h`, old Deserializer class
18. **Delete source_adapter code**: `simdjson_source_adapter`, `toml_source_adapter`, `content_source_adapter`, `can_buffer_raw_field` concept
19. **Update `codec::deserialize(d, v)` public API** — either remove or redirect to new path

### Phase 5: Cleanup

20. **Remove unused code**: `StreamingDeserCtx`, `has_read_object` concept, `field_mode` from old backend, `buffer_raw_field_value`/`replay_buffered_field`
21. **Update documentation**
22. **Final benchmark** — full suite comparison

---

## 15. Verification Checklist

For each phase, verify:

- [ ] All 1170 existing unit tests pass
- [ ] `BM_read_new` performance matches `BM_simdjson_handwritten` (target: <1.1x)
- [ ] Attribute combinations work:
  - [ ] `rename` + `alias`
  - [ ] `skip_if` (deserialize: skip reading)
  - [ ] `default_value` (missing field OK)
  - [ ] `with<Adapter>` (custom wire type)
  - [ ] `as<Target>` (type conversion)
  - [ ] `enum_string` (string ↔ enum)
  - [ ] `flatten` (nested fields inlined)
  - [ ] `deny_unknown_fields` (reject unknown keys)
  - [ ] `tagged` variants (all 3 strategies)
  - [ ] `literal` (field must match exact string)
- [ ] Edge cases:
  - [ ] Empty struct
  - [ ] Struct with >64 fields (bitmask overflow)
  - [ ] Recursive types (struct containing vector of itself)
  - [ ] `std::optional` fields (null handling)
  - [ ] `std::unique_ptr` / `std::shared_ptr`
  - [ ] Map with non-string keys (parse from string)
  - [ ] Deeply nested structures (5+ levels)

---

## 16. Key Invariants

1. **Zero state**: No mutable state in the deserialization path except the output value and local variables
2. **Value passed down**: The backend's `value_type` is always a parameter, never stored in a member variable
3. **Backend drives**: The backend controls iteration; visitors receive data passively
4. **Error type = backend native**: The hot path never constructs `serde_error`; only the top-level entry point does (and only on failure)
5. **Each struct = separate function**: `struct_visitor<Backend, T>` is a unique type per struct, generating a distinct function. No monolithic dispatch fold.
6. **Attributes are generic**: `deserialize_field<Backend, T, Attrs>()` handles all attributes independent of backend
7. **No parallel abstractions**: Backend provides traversal for BOTH deserialization (`visit_object`) AND variant scoring (`visit_object_keys`). No separate `source_adapter` layer.
8. **Scoring is lightweight**: `visit_object_keys` only provides `(key, kind)` to visitor; `value_type` is available for optional recursion but need not be consumed. Lazy backends pay O(1) per field for peek.

## 17. Expected Performance

The generated code for a struct with fields `{name: string, age: int, score: double}`:

```cpp
// What the compiler sees after inlining struct_visitor::visit_field:
for(auto field_result: obj) {
    field = ...;  key = ...;  val = field.value();
    if(key.size() == 4 && key == "name") {
        err = val.get_string().get(sv); out.name = sv;
    } else if(key.size() == 3 && key == "age") {
        err = val.get_int64().get(v); out.age = v;
    } else if(key.size() == 5 && key == "score") {
        err = val.get_double().get(d); out.score = d;
    }
    if(err) return err;
}
```

This is **structurally identical** to handwritten code. The only possible overhead is the `string_match_invoke` dispatch mechanism vs a hand-tuned if-else chain, which benchmarks show is equal or faster (length-based grouping).
