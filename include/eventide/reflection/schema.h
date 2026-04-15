#pragma once

#include <tuple>

#include "type_info.h"
#include "eventide/common/type_list.h"

namespace eventide::refl {

template <typename RawType, typename WireType = RawType, typename BehaviorAttrs = std::tuple<>>
struct field_slot {
    using raw_type = RawType;
    using wire_type = WireType;
    using attrs = BehaviorAttrs;
};

namespace detail {

template <typename T,
          typename Config,
          std::size_t I,
          bool Skipped = field_attr_flags<T, I>::skipped,
          bool Flattened = field_attr_flags<T, I>::flattened>
struct single_field_slots {
    using field_t = refl::field_type<T, I>;
    using unwrap = unwrap_annotated<field_t>;
    using raw_type = typename unwrap::raw_type;
    using attrs_t = typename unwrap::attrs;

    using type = type_list<field_slot<raw_type,
                                      resolve_wire_type_t<raw_type, attrs_t>,
                                      filter_runtime_attrs_t<attrs_t>>>;
};

template <typename T, typename Config, std::size_t I, bool Flattened>
struct single_field_slots<T, Config, I, /*Skipped=*/true, Flattened> {
    using type = type_list<>;
};

template <typename T, typename Config, std::size_t I>
struct single_field_slots<T, Config, I, /*Skipped=*/false, /*Flattened=*/true>;

template <typename T, typename Config, typename Seq>
struct build_slots_from_seq;

template <typename T, typename Config, std::size_t... Is>
struct build_slots_from_seq<T, Config, std::index_sequence<Is...>> {
    using type = type_list_concat_t<typename single_field_slots<T, Config, Is>::type...>;
};

template <typename T, typename Config>
struct build_slots_from_seq<T, Config, std::index_sequence<>> {
    using type = type_list<>;
};

template <typename T, typename Config>
using build_slots_t =
    typename build_slots_from_seq<T, Config, std::make_index_sequence<refl::field_count<T>()>>::
        type;

}  // namespace detail

template <typename T, typename Config = default_config>
    requires refl::reflectable_class<T>
struct virtual_schema {
    constexpr static std::size_t count = detail::struct_info_node<T, Config>::count;
    constexpr static auto& fields = detail::struct_info_node<T, Config>::fields;
    using slots = detail::build_slots_t<T, Config>;
    constexpr static bool is_trivially_copyable =
        detail::struct_info_node<T, Config>::is_trivially_copyable;
    constexpr static bool deny_unknown = detail::struct_info_node<T, Config>::deny_unknown;
};

namespace detail {

template <typename T, typename Config, std::size_t I>
struct single_field_slots<T, Config, I, /*Skipped=*/false, /*Flattened=*/true> {
    using field_t = refl::field_type<T, I>;
    using inner_t = typename unwrap_annotated<field_t>::raw_type;
    using type = typename virtual_schema<inner_t, Config>::slots;
};

}  // namespace detail

}  // namespace eventide::refl
