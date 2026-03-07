#pragma once

// This header must be included AFTER content/deserializer.h is fully defined.
// It provides the implementation of deserialize_internally_tagged declared in serde.h.

#include "eventide/serde/serde/serde.h"

namespace eventide::serde::detail {

template <typename E, typename D, typename... Ts, typename TagAttr>
constexpr auto deserialize_internally_tagged(D& d, std::variant<Ts...>& value, TagAttr)
    -> std::expected<void, E> {
    // Requires capture_dom_value() — buffer to content DOM, then two-pass dispatch
    auto dom_result = d.capture_dom_value();
    if(!dom_result) {
        return std::unexpected(E(dom_result.error()));
    }

    constexpr auto names = resolve_tag_names<TagAttr, Ts...>();
    constexpr std::string_view tag_field = TagAttr::field_names[0];

    auto obj_ref = dom_result->as_ref();
    auto obj = obj_ref.get_object();
    if(!obj) {
        return std::unexpected(E::type_mismatch);
    }

    // Pass 1: find tag
    std::string_view tag_value;
    bool found = false;
    for(auto entry: *obj) {
        if(entry.key == tag_field) {
            auto s = entry.value.get_string();
            if(!s) {
                return std::unexpected(E::type_mismatch);
            }
            tag_value = *s;
            found = true;
            break;
        }
    }
    if(!found) {
        return std::unexpected(E::type_mismatch);
    }

    // Pass 2: match tag -> deserialize full object as that struct type
    // The tag field will be skipped during struct deserialization (no matching struct field)
    bool matched = false;
    std::expected<void, E> status{};

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (([&] {
             if(matched) {
                 return;
             }
             if(names[I] != tag_value) {
                 return;
             }
             matched = true;

             using alt_t = std::variant_alternative_t<I, std::variant<Ts...>>;
             static_assert(refl::reflectable_class<alt_t>,
                           "internally_tagged requires struct alternatives");

             if constexpr(std::default_initializable<alt_t>) {
                 alt_t alt{};
                 content::Deserializer<> deser(obj_ref);
                 auto r = serde::deserialize(deser, alt);
                 if(!r) {
                     status = std::unexpected(E(r.error()));
                     return;
                 }
                 auto f = deser.finish();
                 if(!f) {
                     status = std::unexpected(E(f.error()));
                     return;
                 }
                 value = std::move(alt);
             } else {
                 status = std::unexpected(E::invalid_state);
             }
         }()),
         ...);
    }(std::make_index_sequence<sizeof...(Ts)>{});

    if(!matched) {
        return std::unexpected(E::type_mismatch);
    }
    return status;
}

}  // namespace eventide::serde::detail
