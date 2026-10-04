#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "fixtures/attrs.h"
#include "fixtures/containers.h"
#include "fixtures/enums.h"
#include "fixtures/recursive.h"
#include "fixtures/structs.h"
#include "fixtures/tagged.h"
#include "kota/zest/zest.h"
#include "kota/meta/schema.h"

namespace kota::meta {

namespace {

namespace fx = ::kota::test;

ZEST_SUITE(meta_schema_type_info) {

ZEST_CASE(scalar_helpers) {
    // int32: signed integer, numeric, scalar
    constexpr auto int_info = type_info_of<int, default_config>();
    ZSTATIC_EXPECT(int_info.kind == type_kind::int32);
    ZSTATIC_EXPECT(int_info.is_integer());
    ZSTATIC_EXPECT(int_info.is_signed_integer());
    ZSTATIC_EXPECT(!int_info.is_unsigned_integer());
    ZSTATIC_EXPECT(!int_info.is_floating());
    ZSTATIC_EXPECT(int_info.is_numeric());
    ZSTATIC_EXPECT(int_info.is_scalar());

    // uint64: unsigned integer, numeric, scalar
    constexpr auto u64_info = type_info_of<std::uint64_t, default_config>();
    ZSTATIC_EXPECT(u64_info.is_integer());
    ZSTATIC_EXPECT(!u64_info.is_signed_integer());
    ZSTATIC_EXPECT(u64_info.is_unsigned_integer());
    ZSTATIC_EXPECT(u64_info.is_numeric());
    ZSTATIC_EXPECT(u64_info.is_scalar());

    // double: floating, numeric, scalar
    constexpr auto dbl_info = type_info_of<double, default_config>();
    ZSTATIC_EXPECT(!dbl_info.is_integer());
    ZSTATIC_EXPECT(dbl_info.is_floating());
    ZSTATIC_EXPECT(dbl_info.is_numeric());
    ZSTATIC_EXPECT(dbl_info.is_scalar());

    // bool: scalar but not numeric
    constexpr auto bool_info = type_info_of<bool, default_config>();
    ZSTATIC_EXPECT(bool_info.is_scalar());
    ZSTATIC_EXPECT(!bool_info.is_numeric());
    ZSTATIC_EXPECT(!bool_info.is_integer());
    ZSTATIC_EXPECT(!bool_info.is_floating());

    // string: scalar but not numeric
    constexpr auto str_info = type_info_of<std::string, default_config>();
    ZSTATIC_EXPECT(str_info.kind == type_kind::string);
    ZSTATIC_EXPECT(str_info.is_scalar());
    ZSTATIC_EXPECT(!str_info.is_numeric());

    // enum: scalar but not numeric
    constexpr auto enum_info = type_info_of<fx::Color, default_config>();
    ZSTATIC_EXPECT(enum_info.kind == type_kind::enumeration);
    ZSTATIC_EXPECT(enum_info.is_scalar());
    ZSTATIC_EXPECT(!enum_info.is_numeric());
}

ZEST_CASE(compound_types) {
    // vector<int> -> array with int32 element
    {
        constexpr auto arr =
            static_cast<const array_type_info&>(type_info_of<std::vector<int>, default_config>());
        ZSTATIC_EXPECT(arr.kind == type_kind::array);
        ZSTATIC_EXPECT(!arr.is_scalar());
        constexpr auto elem = arr.element();
        ZSTATIC_EXPECT(elem.kind == type_kind::int32);
    }

    // set<int> -> set with int32 element
    {
        constexpr auto s =
            static_cast<const array_type_info&>(type_info_of<std::set<int>, default_config>());
        ZSTATIC_EXPECT(s.kind == type_kind::set);
        ZSTATIC_EXPECT(!s.is_scalar());
        constexpr auto elem = s.element();
        ZSTATIC_EXPECT(elem.kind == type_kind::int32);
    }

    // map<string, int> -> map with string key, int32 value
    {
        constexpr auto m = static_cast<const map_type_info&>(
            type_info_of<std::map<std::string, int>, default_config>());
        ZSTATIC_EXPECT(m.kind == type_kind::map);
        ZSTATIC_EXPECT(!m.is_scalar());
        constexpr auto k = m.key();
        constexpr auto v = m.value();
        ZSTATIC_EXPECT(k.kind == type_kind::string);
        ZSTATIC_EXPECT(v.kind == type_kind::int32);
    }

    // optional<int> -> optional with int32 inner
    {
        constexpr auto opt = static_cast<const optional_type_info&>(
            type_info_of<std::optional<int>, default_config>());
        ZSTATIC_EXPECT(opt.kind == type_kind::optional);
        constexpr auto inner = opt.inner();
        ZSTATIC_EXPECT(inner.kind == type_kind::int32);
    }

    // unique_ptr<int> -> pointer with int32 inner
    {
        constexpr auto ptr = static_cast<const optional_type_info&>(
            type_info_of<std::unique_ptr<int>, default_config>());
        ZSTATIC_EXPECT(ptr.kind == type_kind::pointer);
        constexpr auto inner = ptr.inner();
        ZSTATIC_EXPECT(inner.kind == type_kind::int32);
    }

    // shared_ptr<string> -> pointer with string inner
    {
        constexpr auto ptr = static_cast<const optional_type_info&>(
            type_info_of<std::shared_ptr<std::string>, default_config>());
        ZSTATIC_EXPECT(ptr.kind == type_kind::pointer);
        constexpr auto inner = ptr.inner();
        ZSTATIC_EXPECT(inner.kind == type_kind::string);
    }

    // variant<int, string> -> variant with 2 alternatives
    {
        constexpr auto var = static_cast<const variant_type_info&>(
            type_info_of<std::variant<int, std::string>, default_config>());
        ZSTATIC_EXPECT(var.kind == type_kind::variant);
        ZSTATIC_EXPECT(var.alternatives.size() == 2U);
        constexpr auto alt0 = var.alternatives[0]();
        constexpr auto alt1 = var.alternatives[1]();
        ZSTATIC_EXPECT(alt0.kind == type_kind::int32);
        ZSTATIC_EXPECT(alt1.kind == type_kind::string);
    }

    // pair<int, float> -> tuple with 2 elements
    {
        constexpr auto tup = static_cast<const tuple_type_info&>(
            type_info_of<std::pair<int, float>, default_config>());
        ZSTATIC_EXPECT(tup.kind == type_kind::tuple);
        ZSTATIC_EXPECT(tup.elements.size() == 2U);
        constexpr auto e0 = tup.elements[0]();
        constexpr auto e1 = tup.elements[1]();
        ZSTATIC_EXPECT(e0.kind == type_kind::int32);
        ZSTATIC_EXPECT(e1.kind == type_kind::float32);
    }

    // tuple<int, double, string> -> tuple with 3 elements
    {
        constexpr auto tup = static_cast<const tuple_type_info&>(
            type_info_of<std::tuple<int, double, std::string>, default_config>());
        ZSTATIC_EXPECT(tup.kind == type_kind::tuple);
        ZSTATIC_EXPECT(tup.elements.size() == 3U);
        constexpr auto e0 = tup.elements[0]();
        constexpr auto e1 = tup.elements[1]();
        constexpr auto e2 = tup.elements[2]();
        ZSTATIC_EXPECT(e0.kind == type_kind::int32);
        ZSTATIC_EXPECT(e1.kind == type_kind::float64);
        ZSTATIC_EXPECT(e2.kind == type_kind::string);
    }

    // SimpleStruct -> structure
    {
        constexpr auto info = type_info_of<fx::SimpleStruct, default_config>();
        ZSTATIC_EXPECT(info.kind == type_kind::structure);
        ZSTATIC_EXPECT(!info.is_scalar());
    }
}

ZEST_CASE(nested_type_info) {
    // vector<optional<int>> -> array -> optional -> int32
    {
        constexpr auto arr = static_cast<const array_type_info&>(
            type_info_of<std::vector<std::optional<int>>, default_config>());
        ZSTATIC_EXPECT(arr.kind == type_kind::array);
        constexpr auto elem_base = arr.element();
        ZSTATIC_EXPECT(elem_base.kind == type_kind::optional);
        constexpr auto opt = static_cast<const optional_type_info&>(arr.element());
        constexpr auto inner = opt.inner();
        ZSTATIC_EXPECT(inner.kind == type_kind::int32);
    }

    // map<string, vector<int>> -> map -> string key, array value -> int32 element
    {
        constexpr auto m = static_cast<const map_type_info&>(
            type_info_of<std::map<std::string, std::vector<int>>, default_config>());
        ZSTATIC_EXPECT(m.kind == type_kind::map);
        constexpr auto k = m.key();
        ZSTATIC_EXPECT(k.kind == type_kind::string);
        constexpr auto v_base = m.value();
        ZSTATIC_EXPECT(v_base.kind == type_kind::array);
        constexpr auto inner_arr = static_cast<const array_type_info&>(m.value());
        constexpr auto elem = inner_arr.element();
        ZSTATIC_EXPECT(elem.kind == type_kind::int32);
    }

    // set<vector<string>> -> set -> array -> string
    {
        constexpr auto s = static_cast<const array_type_info&>(
            type_info_of<std::set<std::vector<std::string>>, default_config>());
        ZSTATIC_EXPECT(s.kind == type_kind::set);
        constexpr auto elem_base = s.element();
        ZSTATIC_EXPECT(elem_base.kind == type_kind::array);
        constexpr auto inner_arr = static_cast<const array_type_info&>(s.element());
        constexpr auto elem = inner_arr.element();
        ZSTATIC_EXPECT(elem.kind == type_kind::string);
    }

    // unique_ptr<vector<int>> -> pointer -> array -> int32
    {
        constexpr auto ptr = static_cast<const optional_type_info&>(
            type_info_of<std::unique_ptr<std::vector<int>>, default_config>());
        ZSTATIC_EXPECT(ptr.kind == type_kind::pointer);
        constexpr auto inner_base = ptr.inner();
        ZSTATIC_EXPECT(inner_base.kind == type_kind::array);
        constexpr auto arr = static_cast<const array_type_info&>(ptr.inner());
        constexpr auto elem = arr.element();
        ZSTATIC_EXPECT(elem.kind == type_kind::int32);
    }
}

ZEST_CASE(annotated_type_info) {
    {
        constexpr auto si =
            static_cast<const struct_type_info&>(type_info_of<fx::RenamedRoot, default_config>());
        ZSTATIC_EXPECT(si.kind == type_kind::structure);
        ZSTATIC_EXPECT(si.fields.size() == 2U);
        ZSTATIC_EXPECT(si.fields[0].name == "userName");
        ZSTATIC_EXPECT(si.fields[1].name == "displayName");
        ZSTATIC_EXPECT(!si.deny_unknown);
    }

    {
        constexpr auto si =
            static_cast<const struct_type_info&>(type_info_of<fx::StrictRoot, default_config>());
        ZSTATIC_EXPECT(si.kind == type_kind::structure);
        ZSTATIC_EXPECT(si.fields.size() == 2U);
        ZSTATIC_EXPECT(si.deny_unknown);
    }

    {
        constexpr auto var =
            static_cast<const variant_type_info&>(type_info_of<fx::TaggedRoot, default_config>());
        ZSTATIC_EXPECT(var.kind == type_kind::variant);
        ZSTATIC_EXPECT(var.tagging == tag_mode::internal);
        ZSTATIC_EXPECT(var.alternatives.size() == 2U);
        constexpr auto alt0 = var.alternatives[0]();
        constexpr auto alt1 = var.alternatives[1]();
        ZSTATIC_EXPECT(alt0.kind == type_kind::structure);
        ZSTATIC_EXPECT(alt1.kind == type_kind::structure);
        ZSTATIC_EXPECT(var.tag_field == "kind");
        ZSTATIC_EXPECT(var.content_field == "");
        ZSTATIC_EXPECT(var.alt_names.size() == 2U);
        ZSTATIC_EXPECT(var.alt_names[0] == "circle");
        ZSTATIC_EXPECT(var.alt_names[1] == "rect");
    }

    {
        constexpr auto ei = static_cast<const enum_type_info&>(
            type_info_of<fx::HugeUnsignedEnum, default_config>());
        ZSTATIC_EXPECT(ei.kind == type_kind::enumeration);
        ZSTATIC_EXPECT(ei.underlying_kind == type_kind::uint64);
        ZSTATIC_EXPECT(ei.member_names.size() == 2U);
        const auto* u64_vals = static_cast<const std::uint64_t*>(ei.member_values);
        const std::span<const std::uint64_t> values{u64_vals, ei.member_names.size()};
        ZEXPECT(((values[0] == 0U && values[1] == std::numeric_limits<std::uint64_t>::max()) ||
                 (values[1] == 0U && values[0] == std::numeric_limits<std::uint64_t>::max())));
    }
}

ZEST_CASE(recursive_tree_node) {
    constexpr auto si =
        static_cast<const struct_type_info&>(type_info_of<fx::TreeNode, default_config>());
    ZSTATIC_EXPECT(si.kind == type_kind::structure);
    ZSTATIC_EXPECT(si.fields.size() == 2U);

    constexpr auto f0_type = si.fields[0].type();
    ZSTATIC_EXPECT(si.fields[0].name == "value");
    ZSTATIC_EXPECT(f0_type.kind == type_kind::string);

    constexpr auto f1_type = si.fields[1].type();
    ZSTATIC_EXPECT(si.fields[1].name == "children");
    ZSTATIC_EXPECT(f1_type.kind == type_kind::array);

    constexpr auto arr = static_cast<const array_type_info&>(si.fields[1].type());
    constexpr auto arr_elem = arr.element();
    ZSTATIC_EXPECT(arr_elem.kind == type_kind::structure);
    ZSTATIC_EXPECT(arr_elem.type_name == si.type_name);
}

ZEST_CASE(recursive_linked_list_unique_ptr) {
    constexpr auto si =
        static_cast<const struct_type_info&>(type_info_of<fx::LinkedNode, default_config>());
    ZSTATIC_EXPECT(si.kind == type_kind::structure);
    ZSTATIC_EXPECT(si.fields.size() == 2U);

    constexpr auto f0_type = si.fields[0].type();
    ZSTATIC_EXPECT(si.fields[0].name == "data");
    ZSTATIC_EXPECT(f0_type.kind == type_kind::int32);

    constexpr auto f1_type = si.fields[1].type();
    ZSTATIC_EXPECT(si.fields[1].name == "next");
    ZSTATIC_EXPECT(f1_type.kind == type_kind::pointer);

    constexpr auto ptr = static_cast<const optional_type_info&>(si.fields[1].type());
    constexpr auto ptr_inner = ptr.inner();
    ZSTATIC_EXPECT(ptr_inner.kind == type_kind::structure);
    ZSTATIC_EXPECT(ptr_inner.type_name == si.type_name);
}

ZEST_CASE(recursive_shared_ptr_with_vector) {
    constexpr auto si =
        static_cast<const struct_type_info&>(type_info_of<fx::SharedNode, default_config>());
    ZSTATIC_EXPECT(si.kind == type_kind::structure);
    ZSTATIC_EXPECT(si.fields.size() == 3U);

    constexpr auto f0_type = si.fields[0].type();
    ZSTATIC_EXPECT(si.fields[0].name == "label");
    ZSTATIC_EXPECT(f0_type.kind == type_kind::string);

    constexpr auto f1_type = si.fields[1].type();
    ZSTATIC_EXPECT(si.fields[1].name == "parent");
    ZSTATIC_EXPECT(f1_type.kind == type_kind::pointer);
    constexpr auto parent_ptr = static_cast<const optional_type_info&>(si.fields[1].type());
    constexpr auto parent_inner = parent_ptr.inner();
    ZSTATIC_EXPECT(parent_inner.kind == type_kind::structure);
    ZSTATIC_EXPECT(parent_inner.type_name == si.type_name);

    constexpr auto f2_type = si.fields[2].type();
    ZSTATIC_EXPECT(si.fields[2].name == "children");
    ZSTATIC_EXPECT(f2_type.kind == type_kind::array);
    constexpr auto arr = static_cast<const array_type_info&>(si.fields[2].type());
    constexpr auto arr_elem = arr.element();
    ZSTATIC_EXPECT(arr_elem.kind == type_kind::pointer);
    constexpr auto child_ptr = static_cast<const optional_type_info&>(arr.element());
    constexpr auto child_inner = child_ptr.inner();
    ZSTATIC_EXPECT(child_inner.kind == type_kind::structure);
    ZSTATIC_EXPECT(child_inner.type_name == si.type_name);
}

ZEST_CASE(recursive_optional_vector) {
    constexpr auto si =
        static_cast<const struct_type_info&>(type_info_of<fx::OptionalRecursive, default_config>());
    ZSTATIC_EXPECT(si.kind == type_kind::structure);
    ZSTATIC_EXPECT(si.fields.size() == 2U);

    constexpr auto f0_type = si.fields[0].type();
    ZSTATIC_EXPECT(si.fields[0].name == "id");
    ZSTATIC_EXPECT(f0_type.kind == type_kind::int32);

    constexpr auto f1_type = si.fields[1].type();
    ZSTATIC_EXPECT(si.fields[1].name == "sub_items");
    ZSTATIC_EXPECT(f1_type.kind == type_kind::optional);
    constexpr auto opt = static_cast<const optional_type_info&>(si.fields[1].type());
    constexpr auto opt_inner = opt.inner();
    ZSTATIC_EXPECT(opt_inner.kind == type_kind::array);
    constexpr auto arr = static_cast<const array_type_info&>(opt.inner());
    constexpr auto arr_elem = arr.element();
    ZSTATIC_EXPECT(arr_elem.kind == type_kind::structure);
    ZSTATIC_EXPECT(arr_elem.type_name == si.type_name);
}

ZEST_CASE(recursive_map_values) {
    constexpr auto si =
        static_cast<const struct_type_info&>(type_info_of<fx::MapRecursive, default_config>());
    ZSTATIC_EXPECT(si.kind == type_kind::structure);
    ZSTATIC_EXPECT(si.fields.size() == 2U);

    constexpr auto f0_type = si.fields[0].type();
    ZSTATIC_EXPECT(si.fields[0].name == "name");
    ZSTATIC_EXPECT(f0_type.kind == type_kind::string);

    constexpr auto f1_type = si.fields[1].type();
    ZSTATIC_EXPECT(si.fields[1].name == "nested");
    ZSTATIC_EXPECT(f1_type.kind == type_kind::map);
    constexpr auto m = static_cast<const map_type_info&>(si.fields[1].type());
    constexpr auto k = m.key();
    constexpr auto v = m.value();
    ZSTATIC_EXPECT(k.kind == type_kind::string);
    ZSTATIC_EXPECT(v.kind == type_kind::structure);
    ZSTATIC_EXPECT(v.type_name == si.type_name);
}

ZEST_CASE(recursive_variant_tree) {
    constexpr auto si =
        static_cast<const struct_type_info&>(type_info_of<fx::VariantBranch, default_config>());
    ZSTATIC_EXPECT(si.kind == type_kind::structure);
    ZSTATIC_EXPECT(si.fields.size() == 1U);

    constexpr auto f0_type = si.fields[0].type();
    ZSTATIC_EXPECT(si.fields[0].name == "nodes");
    ZSTATIC_EXPECT(f0_type.kind == type_kind::array);

    constexpr auto arr = static_cast<const array_type_info&>(si.fields[0].type());
    constexpr auto arr_elem = arr.element();
    ZSTATIC_EXPECT(arr_elem.kind == type_kind::variant);

    constexpr auto var = static_cast<const variant_type_info&>(arr.element());
    ZSTATIC_EXPECT(var.alternatives.size() == 2U);

    constexpr auto leaf_info = type_info_of<fx::VariantLeaf, default_config>();
    constexpr auto alt0 = var.alternatives[0]();
    constexpr auto alt1 = var.alternatives[1]();
    ZSTATIC_EXPECT(alt0.kind == type_kind::structure);
    ZSTATIC_EXPECT(alt0.type_name == leaf_info.type_name);
    ZSTATIC_EXPECT(alt1.kind == type_kind::structure);
    ZSTATIC_EXPECT(alt1.type_name == si.type_name);
}

ZEST_CASE(recursive_mixed_deep) {
    constexpr auto si =
        static_cast<const struct_type_info&>(type_info_of<fx::MixedRecursive, default_config>());
    ZSTATIC_EXPECT(si.kind == type_kind::structure);
    ZSTATIC_EXPECT(si.fields.size() == 3U);

    // tag: string
    constexpr auto f0_type = si.fields[0].type();
    ZSTATIC_EXPECT(si.fields[0].name == "tag");
    ZSTATIC_EXPECT(f0_type.kind == type_kind::string);

    // deep: optional -> pointer -> structure (self)
    constexpr auto f1_type = si.fields[1].type();
    ZSTATIC_EXPECT(si.fields[1].name == "deep");
    ZSTATIC_EXPECT(f1_type.kind == type_kind::optional);
    constexpr auto opt = static_cast<const optional_type_info&>(si.fields[1].type());
    constexpr auto opt_inner = opt.inner();
    ZSTATIC_EXPECT(opt_inner.kind == type_kind::pointer);
    constexpr auto ptr = static_cast<const optional_type_info&>(opt.inner());
    constexpr auto ptr_inner = ptr.inner();
    ZSTATIC_EXPECT(ptr_inner.kind == type_kind::structure);
    ZSTATIC_EXPECT(ptr_inner.type_name == si.type_name);

    // grouped: map<string, vector<self>>
    constexpr auto f2_type = si.fields[2].type();
    ZSTATIC_EXPECT(si.fields[2].name == "grouped");
    ZSTATIC_EXPECT(f2_type.kind == type_kind::map);
    constexpr auto m = static_cast<const map_type_info&>(si.fields[2].type());
    constexpr auto k = m.key();
    constexpr auto v = m.value();
    ZSTATIC_EXPECT(k.kind == type_kind::string);
    ZSTATIC_EXPECT(v.kind == type_kind::array);
    constexpr auto arr = static_cast<const array_type_info&>(m.value());
    constexpr auto arr_elem = arr.element();
    ZSTATIC_EXPECT(arr_elem.kind == type_kind::structure);
    ZSTATIC_EXPECT(arr_elem.type_name == si.type_name);
}

ZEST_CASE(disabled_range_type_info_is_unknown) {
    ZSTATIC_EXPECT(kind_of<fx::DisabledRange>() == type_kind::unknown);

    constexpr auto info = type_info_of<fx::DisabledRange, default_config>();
    ZSTATIC_EXPECT(info.kind == type_kind::unknown);
}

ZEST_CASE(cv_canonicalization_all_kinds) {
    // scalars
    static_assert(&type_info_of<int>() == &type_info_of<const int>());
    static_assert(&type_info_of<int>() == &type_info_of<volatile int>());
    static_assert(&type_info_of<int>() == &type_info_of<const volatile int>());
    static_assert(&type_info_of<std::string>() == &type_info_of<const std::string>());
    static_assert(&type_info_of<bool>() == &type_info_of<const bool>());

    // enum
    static_assert(&type_info_of<fx::Color>() == &type_info_of<const fx::Color>());

    // containers: array / set / map / optional / pointer
    static_assert(&type_info_of<std::vector<int>>() == &type_info_of<const std::vector<int>>());
    static_assert(&type_info_of<std::set<int>>() == &type_info_of<const std::set<int>>());
    static_assert(&type_info_of<std::map<std::string, int>>() ==
                  &type_info_of<const std::map<std::string, int>>());
    static_assert(&type_info_of<std::optional<int>>() == &type_info_of<const std::optional<int>>());
    static_assert(&type_info_of<std::unique_ptr<int>>() ==
                  &type_info_of<const std::unique_ptr<int>>());

    // structure (including recursive)
    static_assert(&type_info_of<fx::SimpleStruct>() == &type_info_of<const fx::SimpleStruct>());
    static_assert(&type_info_of<fx::TreeNode>() == &type_info_of<const fx::TreeNode>());
    static_assert(&type_info_of<fx::MixedRecursive>() == &type_info_of<const fx::MixedRecursive>());

    // variant / tuple
    using V = std::variant<int, std::string>;
    static_assert(&type_info_of<V>() == &type_info_of<const V>());
    using TupT = std::tuple<int, std::string, double>;
    static_assert(&type_info_of<TupT>() == &type_info_of<const TupT>());
}

ZEST_CASE(value_copy_all_kinds) {
    // Full value copies of each kind's type_info subclass must succeed during
    // constant initialization — and at EVERY level of nested access. This is
    // the core guarantee the function-pointer trampoline delivers: clang used
    // to bail out on recursive structures here.
    //
    // Every check below first binds a `constexpr auto v = ...` value copy,
    // then asserts on that local. This proves value-copy semantics work at
    // each level, not just at the top.

    // array
    {
        constexpr auto arr =
            static_cast<const array_type_info&>(type_info_of<std::vector<int>, default_config>());
        static_assert(arr.kind == type_kind::array);
        constexpr auto elem = arr.element();
        static_assert(elem.kind == type_kind::int32);
    }

    // map
    {
        constexpr auto m = static_cast<const map_type_info&>(
            type_info_of<std::map<std::string, int>, default_config>());
        static_assert(m.kind == type_kind::map);
        constexpr auto k = m.key();
        constexpr auto v = m.value();
        static_assert(k.kind == type_kind::string);
        static_assert(v.kind == type_kind::int32);
    }

    // optional
    {
        constexpr auto opt = static_cast<const optional_type_info&>(
            type_info_of<std::optional<int>, default_config>());
        static_assert(opt.kind == type_kind::optional);
        constexpr auto inner = opt.inner();
        static_assert(inner.kind == type_kind::int32);
    }

    // tuple
    {
        constexpr auto tup = static_cast<const tuple_type_info&>(
            type_info_of<std::tuple<int, std::string>, default_config>());
        static_assert(tup.kind == type_kind::tuple);
        static_assert(tup.elements.size() == 2U);
        constexpr auto e0 = tup.elements[0]();
        constexpr auto e1 = tup.elements[1]();
        static_assert(e0.kind == type_kind::int32);
        static_assert(e1.kind == type_kind::string);
    }

    // variant
    {
        constexpr auto var = static_cast<const variant_type_info&>(
            type_info_of<std::variant<int, std::string>, default_config>());
        static_assert(var.kind == type_kind::variant);
        static_assert(var.alternatives.size() == 2U);
        constexpr auto alt0 = var.alternatives[0]();
        constexpr auto alt1 = var.alternatives[1]();
        static_assert(alt0.kind == type_kind::int32);
        static_assert(alt1.kind == type_kind::string);
    }

    // plain structure
    {
        constexpr auto s =
            static_cast<const struct_type_info&>(type_info_of<fx::SimpleStruct, default_config>());
        static_assert(s.kind == type_kind::structure);
        static_assert(s.fields.size() == 3U);
        constexpr auto f0_type = s.fields[0].type();
        static_assert(f0_type.kind == type_kind::int32);
        constexpr auto f1_type = s.fields[1].type();
        static_assert(f1_type.kind == type_kind::string);
        constexpr auto f2_type = s.fields[2].type();
        static_assert(f2_type.kind == type_kind::float32);
    }

    // recursive structure — this is the root motivation for the refactor.
    // Before the function-pointer trampoline, clang could not constant-evaluate
    // a value copy of a self-referential struct_type_info. Here we walk every
    // level, copying into a local constexpr at each step.
    {
        constexpr auto tree =
            static_cast<const struct_type_info&>(type_info_of<fx::TreeNode, default_config>());
        static_assert(tree.kind == type_kind::structure);
        static_assert(tree.fields.size() == 2U);

        constexpr auto f0_type = tree.fields[0].type();
        static_assert(f0_type.kind == type_kind::string);

        constexpr auto f1_type = tree.fields[1].type();
        static_assert(f1_type.kind == type_kind::array);

        constexpr auto children_arr = static_cast<const array_type_info&>(tree.fields[1].type());
        constexpr auto child_elem = children_arr.element();
        static_assert(child_elem.kind == type_kind::structure);
    }

    // deeply recursive structure with mixed indirection (optional->ptr->self,
    // map<string, vector<self>>). Copy at every level.
    {
        constexpr auto mx = static_cast<const struct_type_info&>(
            type_info_of<fx::MixedRecursive, default_config>());
        static_assert(mx.kind == type_kind::structure);
        static_assert(mx.fields.size() == 3U);

        constexpr auto deep_type = mx.fields[1].type();
        static_assert(deep_type.kind == type_kind::optional);
        constexpr auto deep_opt = static_cast<const optional_type_info&>(mx.fields[1].type());
        constexpr auto deep_opt_inner = deep_opt.inner();
        static_assert(deep_opt_inner.kind == type_kind::pointer);
        constexpr auto deep_ptr = static_cast<const optional_type_info&>(deep_opt.inner());
        constexpr auto deep_ptr_inner = deep_ptr.inner();
        static_assert(deep_ptr_inner.kind == type_kind::structure);

        constexpr auto grouped_type = mx.fields[2].type();
        static_assert(grouped_type.kind == type_kind::map);
        constexpr auto grouped_map = static_cast<const map_type_info&>(mx.fields[2].type());
        constexpr auto grouped_val = grouped_map.value();
        static_assert(grouped_val.kind == type_kind::array);
        constexpr auto grouped_arr = static_cast<const array_type_info&>(grouped_map.value());
        constexpr auto grouped_arr_elem = grouped_arr.element();
        static_assert(grouped_arr_elem.kind == type_kind::structure);
    }
}

ZEST_CASE(recursive_self_pointer_identity) {
    // The reference returned by a recursive field must alias type_info_of<Self>().
    constexpr auto& tree = type_info_of<fx::TreeNode>();
    constexpr auto& tree_si = static_cast<const struct_type_info&>(tree);
    constexpr auto& children_arr = static_cast<const array_type_info&>(tree_si.fields[1].type());
    ZSTATIC_EXPECT(&children_arr.element() == &tree);

    constexpr auto& linked = type_info_of<fx::LinkedNode>();
    constexpr auto& linked_si = static_cast<const struct_type_info&>(linked);
    constexpr auto& next_ptr = static_cast<const optional_type_info&>(linked_si.fields[1].type());
    ZSTATIC_EXPECT(&next_ptr.inner() == &linked);

    constexpr auto& mx = type_info_of<fx::MixedRecursive>();
    constexpr auto& mx_si = static_cast<const struct_type_info&>(mx);
    // deep: optional<unique_ptr<Self>>
    constexpr auto& deep_opt = static_cast<const optional_type_info&>(mx_si.fields[1].type());
    constexpr auto& deep_ptr = static_cast<const optional_type_info&>(deep_opt.inner());
    ZSTATIC_EXPECT(&deep_ptr.inner() == &mx);
    // grouped: map<string, vector<Self>>
    constexpr auto& grouped_map = static_cast<const map_type_info&>(mx_si.fields[2].type());
    constexpr auto& grouped_arr = static_cast<const array_type_info&>(grouped_map.value());
    ZSTATIC_EXPECT(&grouped_arr.element() == &mx);
}

ZEST_CASE(virtual_schema_recursive_fields_are_usable) {
    // Drives the same code path as type_info_of but through virtual_schema, which
    // previously routed through struct_info_node. Verifies fields/count are
    // instantiable and consistent for recursive types.
    using TS = virtual_schema<fx::TreeNode>;
    ZSTATIC_EXPECT(TS::count == 2U);
    ZSTATIC_EXPECT(TS::fields.size() == 2U);
    ZSTATIC_EXPECT(TS::fields[0].name == "value");
    ZSTATIC_EXPECT(TS::fields[1].name == "children");

    using MX = virtual_schema<fx::MixedRecursive>;
    ZSTATIC_EXPECT(MX::count == 3U);
    ZSTATIC_EXPECT(MX::fields.size() == 3U);
    ZSTATIC_EXPECT(MX::fields[0].name == "tag");
    ZSTATIC_EXPECT(MX::fields[1].name == "deep");
    ZSTATIC_EXPECT(MX::fields[2].name == "grouped");

    using VB = virtual_schema<fx::VariantBranch>;
    ZSTATIC_EXPECT(VB::count == 1U);
    ZSTATIC_EXPECT(VB::fields[0].name == "nodes");
}

ZEST_CASE(recursive_cv_shares_storage) {
    // Combines recursion with cv canonicalization: const T and T must resolve to
    // the exact same type_info object even for self-referential structs.
    constexpr auto& non_cv = type_info_of<fx::TreeNode>();
    constexpr auto& with_cv = type_info_of<const fx::TreeNode>();
    ZSTATIC_EXPECT(&non_cv == &with_cv);

    constexpr auto& tree_si = static_cast<const struct_type_info&>(non_cv);
    constexpr auto& arr = static_cast<const array_type_info&>(tree_si.fields[1].type());
    ZSTATIC_EXPECT(&arr.element() == &with_cv);
}

};  // ZEST_SUITE(meta_schema_type_info)

}  // namespace

}  // namespace kota::meta
