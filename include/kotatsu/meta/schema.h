#pragma once

#include <tuple>
#include <type_traits>

#include "type_info.h"
#include "kotatsu/support/type_list.h"

namespace kotatsu::meta {

template <typename RawType, typename BehaviorAttrs>
struct field_slot {
    using raw_type = RawType;
    using attrs = BehaviorAttrs;
};

namespace detail {

template <typename T,
          std::size_t I,
          bool Skipped = field_spec_of<T, I>.skip,
          bool Flattened = field_spec_of<T, I>.flatten>
struct single_field_slots {
    using field_t = std::remove_cv_t<meta::field_type<T, I>>;
    using raw_type = field_t;
    using attrs_t = field_attrs_t<T, I>;

    using type = type_list<field_slot<raw_type, filter_runtime_attrs_t<attrs_t>>>;
};

template <typename T, std::size_t I, bool Flattened>
struct single_field_slots<T, I, /*Skipped=*/true, Flattened> {
    using type = type_list<>;
};

template <typename T, typename Seq>
struct build_slots_from_seq;

template <typename T, std::size_t... Is>
struct build_slots_from_seq<T, std::index_sequence<Is...>> {
    using type = type_list_concat_t<typename single_field_slots<T, Is>::type...>;
};

template <typename T>
using build_slots_t =
    typename build_slots_from_seq<T, std::make_index_sequence<meta::field_count<T>()>>::type;

template <typename T, std::size_t I>
struct single_field_slots<T, I, /*Skipped=*/false, /*Flattened=*/true> {
    using field_t = meta::field_type<T, I>;
    using inner_t = std::remove_cv_t<field_t>;
    using type = build_slots_t<inner_t>;
};

}  // namespace detail

template <typename T, typename Config = default_config>
    requires meta::reflectable_class<T>
struct virtual_schema {
    // Obtain fields from type_info_of<T>().fields (the span inside struct_type_info::value)
    // rather than referencing type_instance<T>::fields directly.  This matters for recursive
    // types: build_fields<T>() stores type_info_of<child> function pointers which
    // transitively instantiate type_instance<T>::value.  If we enter through `value` first,
    // the constexpr evaluator resolves `fields` as a sub-expression of `value` and the
    // back-reference to `value` is recognized as already-in-progress (no re-entry).
    // Entering through `fields` directly would leave `value` unevaluated, causing Clang to
    // attempt its verification and discover a circular dependency.
    constexpr static std::span<const field_info> fields =
        static_cast<const struct_type_info&>(type_info_of<T, Config>()).fields;

    using slots = detail::build_slots_t<T>;
    constexpr static bool deny_unknown = detail::type_instance<T, Config>::deny_unknown;
};

}  // namespace kotatsu::meta
