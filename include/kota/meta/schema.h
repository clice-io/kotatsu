#pragma once

#include <tuple>
#include <type_traits>

#include "type_info.h"
#include "kota/support/type_list.h"

namespace kota::meta {

template <typename RawType, typename BehaviorAttrs = std::tuple<>>
struct field_slot {
    using raw_type = RawType;
    using attrs = BehaviorAttrs;
};

namespace detail {

template <typename T,
          std::size_t I,
          bool Skipped = field_attr_flags<T, I>::skipped,
          bool Flattened = field_attr_flags<T, I>::flattened>
struct single_field_slots {
    using field_t = meta::field_type<T, I>;
    using unwrap = unwrap_annotated<std::remove_cv_t<field_t>>;
    using raw_type = typename unwrap::raw_type;
    using attrs_t = typename unwrap::attrs;

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
struct build_slots_from_seq<T, std::index_sequence<>> {
    using type = type_list<>;
};

template <typename T>
using build_slots_t =
    typename build_slots_from_seq<T, std::make_index_sequence<meta::field_count<T>()>>::type;

template <typename T, std::size_t I>
struct single_field_slots<T, I, /*Skipped=*/false, /*Flattened=*/true> {
    using field_t = meta::field_type<T, I>;
    using inner_t = typename unwrap_annotated<field_t>::raw_type;
    using type = build_slots_t<inner_t>;
};

}  // namespace detail

template <typename T, typename Config = default_config>
    requires meta::reflectable_class<T>
struct virtual_schema {
    constexpr static std::size_t count = detail::type_instance<T, Config>::count;

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
    constexpr static bool is_trivially_copyable =
        detail::type_instance<T, Config>::is_trivially_copyable;
    constexpr static bool deny_unknown = detail::type_instance<T, Config>::deny_unknown;
    /// Every field may be absent from the input: a struct-level
    /// defaulted_fields reached T.
    constexpr static bool defaulted_fields = detail::type_instance<T, Config>::defaulted_fields;
};

}  // namespace kota::meta
