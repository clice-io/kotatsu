#pragma once

// Backend-agnostic combinatorial roundtrip suite.
//
// Each backend provides an adapter:
//
//   struct my_adapter {
//       constexpr static std::string_view name = "json";
//       constexpr static roundtrip::cap caps = roundtrip::cap::Uint64Full | ...;
//       template <typename T>
//       static auto run(const T& value) -> std::expected<T, some_error>;  // encode + decode
//   };
//
// and instantiates the generated corpus inside a ZEST_SUITE:
//
//   ZEST_CASE_GROUP(standard_roundtrip) {
//       roundtrip::register_cases<my_adapter>(add_case);
//   }
//
// One zest test case is registered per value type; each case runs every corpus
// value for that type. Values whose capability requirements the adapter does
// not declare are filtered out; a case where nothing remains reports Skipped.

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <print>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "standard_case_suite.h"
#include "kota/zest/zest.h"
#include "kota/support/type_list.h"

namespace kota::codec::roundtrip {

enum class cap : std::uint32_t {
    None = 0,
    /// uint64 values above int64::max survive the encoded format
    Uint64Full = 1U << 0,
    /// null-like elements (nullopt / null pointer) inside sequences and maps
    NullInSeq = 1U << 1,
    /// plain std::variant roundtrips (by untagged scoring or native index)
    VariantPlain = 1U << 2,
    /// annotation attrs (rename / skip / skip_if / flatten / enum_string)
    Attrs = 1U << 3,
    /// externally/adjacently/internally tagged variants
    VariantTagged = 1U << 4,
};

constexpr cap operator|(cap a, cap b) {
    return static_cast<cap>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr bool supports(cap have, cap need) {
    return (static_cast<std::uint32_t>(need) & ~static_cast<std::uint32_t>(have)) == 0;
}

template <typename T>
struct test_value {
    std::string label;
    T value{};
    cap required = cap::None;
    bool null_like = false;
};

template <typename T>
struct corpus;

template <>
struct corpus<bool> {
    static std::string name() {
        return "bool";
    }

    static auto values() {
        return std::vector<test_value<bool>>{
            {.label = "true",  .value = true },
            {.label = "false", .value = false},
        };
    }
};

template <>
struct corpus<char> {
    static std::string name() {
        return "char";
    }

    static auto values() {
        std::vector<test_value<char>> out;
        for(char c: {'a', 'Z', '0', ' ', '~'}) {
            out.push_back({.label = std::format("'{}'", c), .value = c});
        }
        return out;
    }
};

template <typename T>
    requires (std::signed_integral<T> && !std::same_as<T, bool> && !std::same_as<T, char>)
struct corpus<T> {
    static std::string name() {
        return std::format("i{}", sizeof(T) * 8);
    }

    static auto values() {
        return std::vector<test_value<T>>{
            {.label = "min", .value = (std::numeric_limits<T>::min)()},
            {.label = "-1",  .value = T(-1)                          },
            {.label = "0",   .value = T(0)                           },
            {.label = "1",   .value = T(1)                           },
            {.label = "max", .value = (std::numeric_limits<T>::max)()},
        };
    }
};

template <typename T>
    requires (std::unsigned_integral<T> && !std::same_as<T, bool> && !std::same_as<T, char>)
struct corpus<T> {
    static std::string name() {
        return std::format("u{}", sizeof(T) * 8);
    }

    static auto values() {
        std::vector<test_value<T>> out{
            {.label = "0", .value = T(0)},
            {.label = "1", .value = T(1)},
        };
        constexpr auto max = (std::numeric_limits<T>::max)();
        constexpr bool above_int64 =
            static_cast<std::uint64_t>(max) >
            static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
        out.push_back(
            {.label = "max", .value = max, .required = above_int64 ? cap::Uint64Full : cap::None});
        return out;
    }
};

template <std::floating_point T>
struct corpus<T> {
    static std::string name() {
        return sizeof(T) == 4 ? "f32" : "f64";
    }

    static auto values() {
        return std::vector<test_value<T>>{
            {.label = "0",      .value = T(0)                            },
            {.label = "1.5",    .value = T(1.5)                          },
            {.label = "-2.25",  .value = T(-2.25)                        },
            {.label = "lowest", .value = std::numeric_limits<T>::lowest()},
            {.label = "max",    .value = (std::numeric_limits<T>::max)() },
        };
    }
};

template <>
struct corpus<std::string> {
    static std::string name() {
        return "string";
    }

    static auto values() {
        return std::vector<test_value<std::string>>{
            {.label = "empty", .value = ""},
            {.label = "ascii", .value = "hello roundtrip"},
            {.label = "unicode", .value = "héllo, 世界"},
            {.label = "escapes", .value = "quotes \" backslash \\ newline \n tab \t"},
            {.label = "long", .value = std::string(512, 'x')},
        };
    }
};

template <>
struct corpus<std::monostate> {
    static std::string name() {
        return "monostate";
    }

    static auto values() {
        return std::vector<test_value<std::monostate>>{
            {.label = "monostate", .value = {}, .null_like = true},
        };
    }
};

template <typename T>
struct corpus<std::optional<T>> {
    static std::string name() {
        return std::format("optional<{}>", corpus<T>::name());
    }

    static auto values() {
        std::vector<test_value<std::optional<T>>> out;
        out.push_back({.label = "nullopt", .value = std::nullopt, .null_like = true});
        for(auto& tv: corpus<T>::values()) {
            out.push_back({.label = std::format("some({})", tv.label),
                           .value = std::optional<T>(std::move(tv.value)),
                           .required = tv.required});
        }
        return out;
    }
};

template <typename T>
struct corpus<std::vector<T>> {
    static std::string name() {
        return std::format("vector<{}>", corpus<T>::name());
    }

    static auto values() {
        std::vector<test_value<std::vector<T>>> out;
        out.push_back({.label = "empty", .value = {}});
        auto inner = corpus<T>::values();
        if(auto it = std::ranges::find_if(inner, [](const auto& tv) { return !tv.null_like; });
           it != inner.end()) {
            out.push_back({.label = std::format("[{}]", it->label),
                           .value = std::vector<T>{it->value},
                           .required = it->required});
        }
        std::vector<T> all;
        cap req = cap::None;
        bool has_null = false;
        for(auto& tv: inner) {
            all.push_back(std::move(tv.value));
            req = req | tv.required;
            has_null = has_null || tv.null_like;
        }
        if(has_null) {
            req = req | cap::NullInSeq;
        }
        out.push_back({.label = "all-values", .value = std::move(all), .required = req});
        return out;
    }
};

template <>
struct corpus<std::vector<std::byte>> {
    static std::string name() {
        return "bytes";
    }

    static auto values() {
        return std::vector<test_value<std::vector<std::byte>>>{
            {.label = "empty", .value = {}                                        },
            {.label = "bytes",
             .value = {std::byte{0}, std::byte{1}, std::byte{127}, std::byte{255}}},
        };
    }
};

template <typename T>
struct corpus<std::set<T>> {
    static std::string name() {
        return std::format("set<{}>", corpus<T>::name());
    }

    static auto values() {
        std::vector<test_value<std::set<T>>> out;
        out.push_back({.label = "empty", .value = {}});
        std::set<T> all;
        cap req = cap::None;
        for(auto& tv: corpus<T>::values()) {
            if(tv.null_like) {
                continue;
            }
            all.insert(std::move(tv.value));
            req = req | tv.required;
        }
        out.push_back({.label = "all-values", .value = std::move(all), .required = req});
        return out;
    }
};

template <typename K, typename T>
struct corpus<std::map<K, T>> {
    static std::string name() {
        return std::format("map<{},{}>", corpus<K>::name(), corpus<T>::name());
    }

    static auto values() {
        std::vector<test_value<std::map<K, T>>> out;
        out.push_back({.label = "empty", .value = {}});
        std::map<K, T> all;
        cap req = cap::None;
        bool has_null = false;
        std::size_t index = 0;
        for(auto& tv: corpus<T>::values()) {
            if constexpr(std::same_as<K, std::string>) {
                all.insert_or_assign(std::format("k{}", index), std::move(tv.value));
            } else {
                all.insert_or_assign(static_cast<K>(index), std::move(tv.value));
            }
            req = req | tv.required;
            has_null = has_null || tv.null_like;
            index += 1;
        }
        if(has_null) {
            req = req | cap::NullInSeq;
        }
        out.push_back({.label = "all-values", .value = std::move(all), .required = req});
        return out;
    }
};

template <typename A, typename B>
struct corpus<std::pair<A, B>> {
    static std::string name() {
        return std::format("pair<{},{}>", corpus<A>::name(), corpus<B>::name());
    }

    static auto values() {
        std::vector<test_value<std::pair<A, B>>> out;
        auto lhs = corpus<A>::values();
        auto rhs = corpus<B>::values();
        out.push_back({
            .label = std::format("({},{})", lhs.front().label, rhs.front().label),
            .value = {lhs.front().value, rhs.front().value},
            .required = lhs.front().required | rhs.front().required
        });
        out.push_back({
            .label = std::format("({},{})", lhs.back().label, rhs.back().label),
            .value = {lhs.back().value, rhs.back().value},
            .required = lhs.back().required | rhs.back().required
        });
        return out;
    }
};

template <typename... Ts>
struct corpus<std::tuple<Ts...>> {
    static std::string name() {
        std::string out = "tuple<";
        const char* sep = "";
        ((out += sep, out += corpus<Ts>::name(), sep = ","), ...);
        out += ">";
        return out;
    }

    static auto values() {
        std::vector<test_value<std::tuple<Ts...>>> out;
        out.push_back(pick([](auto& vals) -> auto& { return vals.front(); }, "firsts"));
        out.push_back(pick([](auto& vals) -> auto& { return vals.back(); }, "lasts"));
        return out;
    }

private:
    static auto pick(auto select, std::string label) -> test_value<std::tuple<Ts...>> {
        cap req = cap::None;
        auto get = [&]<typename T>(type_list<T>) -> T {
            auto vals = corpus<T>::values();
            auto& tv = select(vals);
            req = req | tv.required;
            return std::move(tv.value);
        };
        auto value = std::tuple<Ts...>{get(type_list<Ts>{})...};
        return {.label = std::move(label), .value = std::move(value), .required = req};
    }
};

template <typename... Alts>
struct corpus<std::variant<Alts...>> {
    static std::string name() {
        std::string out = "variant<";
        const char* sep = "";
        ((out += sep, out += corpus<Alts>::name(), sep = ","), ...);
        out += ">";
        return out;
    }

    static auto values() {
        std::vector<test_value<std::variant<Alts...>>> out;
        (add_alternative<Alts>(out), ...);
        return out;
    }

private:
    template <typename Alt>
    static void add_alternative(std::vector<test_value<std::variant<Alts...>>>& out) {
        for(auto& tv: corpus<Alt>::values()) {
            out.push_back(
                {.label = std::format("{}:{}", corpus<Alt>::name(), tv.label),
                 .value = std::variant<Alts...>(std::in_place_type<Alt>, std::move(tv.value)),
                 .required = tv.required | cap::VariantPlain,
                 .null_like = tv.null_like});
        }
    }
};

template <>
struct corpus<standard_case::Role> {
    static std::string name() {
        return "enum(Role)";
    }

    static auto values() {
        return std::vector<test_value<standard_case::Role>>{
            {.label = "admin", .value = standard_case::Role::admin},
            {.label = "user",  .value = standard_case::Role::user },
            {.label = "guest", .value = standard_case::Role::guest},
        };
    }
};

template <>
struct corpus<standard_case::Basic> {
    static std::string name() {
        return "struct(Basic)";
    }

    static auto values() {
        return std::vector<test_value<standard_case::Basic>>{
            {.label = "default", .value = {}},
            {.label = "typical", .value = standard_case::make_basic(true, -42, 3.1415, "basic")},
            {.label = "extremes",
             .value = standard_case::make_basic(false,
             (std::numeric_limits<std::int32_t>::max)(),
             -0.5,
             "")},
        };
    }
};

template <>
struct corpus<meta::fixtures::AllPrimitives> {
    static std::string name() {
        return "struct(AllPrimitives)";
    }

    static auto values() {
        return std::vector<test_value<meta::fixtures::AllPrimitives>>{
            {.label = "default", .value = {}                           },
            {.label = "typical", .value = standard_case::make_scalars()},
        };
    }
};

template <>
struct corpus<standard_case::Compound> {
    static std::string name() {
        return "struct(Compound)";
    }

    static auto values() {
        return std::vector<test_value<standard_case::Compound>>{
            {.label = "typical",
             .value =
                 standard_case::Compound{
                     .string_list = {"alpha", "beta", "gamma"},
                     .fixed_array = {1.5F, -2.25F, 0.0F},
                     .heterogeneous_tuple = {7, true, "tuple"},
                 }},
        };
    }
};

template <>
struct corpus<standard_case::NestedContainers> {
    static std::string name() {
        return "struct(NestedContainers)";
    }

    static auto values() {
        return std::vector<test_value<standard_case::NestedContainers>>{
            {.label = "typical", .value = standard_case::make_nested_containers()},
        };
    }
};

template <>
struct corpus<standard_case::EmptyContainers> {
    static std::string name() {
        return "struct(EmptyContainers)";
    }

    static auto values() {
        return std::vector<test_value<standard_case::EmptyContainers>>{
            {.label = "empty", .value = standard_case::make_empty_containers()},
        };
    }
};

template <>
struct corpus<standard_case::Ultimate> {
    static std::string name() {
        return "struct(Ultimate)";
    }

    static auto values() {
        std::vector<test_value<standard_case::Ultimate>> out;
        out.push_back({.label = "typical",
                       .value = standard_case::make_ultimate(),
                       .required = cap::VariantPlain});
        auto cleared = standard_case::make_ultimate();
        cleared.adts.multi_variant = std::monostate{};
        cleared.nullables.opt_value.reset();
        cleared.nullables.heap_allocated.reset();
        out.push_back(
            {.label = "cleared", .value = std::move(cleared), .required = cap::VariantPlain});
        return out;
    }
};

template <>
struct corpus<standard_case::AttrPayload> {
    static std::string name() {
        return "struct(AttrPayload)";
    }

    static auto values() {
        std::vector<test_value<standard_case::AttrPayload>> out;
        out.push_back({.label = "typical",
                       .value = standard_case::make_attr_payload(),
                       .required = cap::Attrs});
        return out;
    }
};

template <>
struct corpus<standard_case::TaggedExternalHolder> {
    static std::string name() {
        return "struct(TaggedExternalHolder)";
    }

    static auto values() {
        return std::vector<test_value<standard_case::TaggedExternalHolder>>{
            {.label = "typical",
             .value = standard_case::make_tagged_external_holder(),
             .required = cap::VariantTagged},
        };
    }
};

template <>
struct corpus<standard_case::TaggedAdjacentHolder> {
    static std::string name() {
        return "struct(TaggedAdjacentHolder)";
    }

    static auto values() {
        return std::vector<test_value<standard_case::TaggedAdjacentHolder>>{
            {.label = "typical",
             .value = standard_case::make_tagged_adjacent_holder(),
             .required = cap::VariantTagged},
        };
    }
};

template <>
struct corpus<standard_case::TaggedInternalHolder> {
    static std::string name() {
        return "struct(TaggedInternalHolder)";
    }

    static auto values() {
        return std::vector<test_value<standard_case::TaggedInternalHolder>>{
            {.label = "typical",
             .value = standard_case::make_tagged_internal_holder(),
             .required = cap::VariantTagged},
        };
    }
};

template <typename Adapter, typename T>
void register_type(const ::kota::zest::CaseRegistrar& add_case) {
    add_case(corpus<T>::name(), [] {
        auto values = corpus<T>::values();
        std::size_t executed = 0;
        for(auto& tv: values) {
            if(!supports(Adapter::caps, tv.required)) {
                continue;
            }
            executed += 1;
            auto decoded = Adapter::run(tv.value);
            if(!decoded.has_value()) {
                std::println("[roundtrip:{}] {} / '{}': failed: {}",
                             Adapter::name,
                             corpus<T>::name(),
                             tv.label,
                             decoded.error().to_string());
                ::kota::zest::failure();
                continue;
            }
            if(!::kota::meta::eq(*decoded, tv.value)) {
                std::println("[roundtrip:{}] {} / '{}': decoded value differs from input",
                             Adapter::name,
                             corpus<T>::name(),
                             tv.label);
                ::kota::zest::failure();
            }
        }
        if(executed == 0) {
            ::kota::zest::skip();
        }
    });
}

template <typename Adapter, typename... Ts>
void register_list(const ::kota::zest::CaseRegistrar& add_case, type_list<Ts...>) {
    (register_type<Adapter, Ts>(add_case), ...);
}

template <template <typename> class W, typename... Ts>
consteval auto wrap(type_list<Ts...>) -> type_list<W<Ts>...> {
    return {};
}

template <typename T>
using vec_of = std::vector<T>;
template <typename T>
using set_of = std::set<T>;
template <typename T>
using map_str = std::map<std::string, T>;
template <typename T>
using map_i32 = std::map<std::int32_t, T>;
template <typename T>
using pair_self = std::pair<T, T>;
template <typename T>
using tuple_mix = std::tuple<T, bool, std::string>;

using scalar_types = type_list<bool,
                               char,
                               std::int8_t,
                               std::uint8_t,
                               std::int16_t,
                               std::uint16_t,
                               std::int32_t,
                               std::uint32_t,
                               std::int64_t,
                               std::uint64_t,
                               float,
                               double,
                               std::string>;

// representative element set for depth-2 combinations: full cartesian over all
// scalars would explode compile time without adding signal
using nested_elem_types = type_list<std::int32_t, std::uint64_t, std::string, standard_case::Basic>;

using untagged_variant_t =
    std::variant<std::monostate, std::int32_t, double, std::string, standard_case::Basic>;
using container_variant_t = std::
    variant<std::tuple<std::int32_t, std::string>, std::vector<std::int32_t>, standard_case::Basic>;

template <typename Adapter>
void register_cases(const ::kota::zest::CaseRegistrar& add_case) {
    register_list<Adapter>(add_case, scalar_types{});

    register_list<Adapter>(add_case, wrap<std::optional>(scalar_types{}));
    register_list<Adapter>(add_case, wrap<vec_of>(scalar_types{}));
    register_list<Adapter>(add_case, wrap<map_str>(scalar_types{}));
    register_list<Adapter>(add_case, wrap<map_i32>(scalar_types{}));
    register_list<Adapter>(add_case, wrap<pair_self>(scalar_types{}));
    register_list<Adapter>(add_case, wrap<tuple_mix>(scalar_types{}));
    register_list<Adapter>(add_case, wrap<set_of>(type_list<std::int32_t, std::string>{}));
    register_type<Adapter, std::vector<std::byte>>(add_case);

    register_list<Adapter>(add_case, wrap<std::optional>(wrap<vec_of>(nested_elem_types{})));
    register_list<Adapter>(add_case, wrap<vec_of>(wrap<std::optional>(nested_elem_types{})));
    register_list<Adapter>(add_case, wrap<vec_of>(wrap<vec_of>(nested_elem_types{})));
    register_list<Adapter>(add_case, wrap<map_str>(wrap<vec_of>(nested_elem_types{})));
    register_list<Adapter>(add_case, wrap<map_str>(wrap<std::optional>(nested_elem_types{})));
    register_list<Adapter>(add_case, wrap<map_str>(wrap<map_str>(nested_elem_types{})));
    register_list<Adapter>(add_case, wrap<vec_of>(wrap<map_str>(nested_elem_types{})));
    register_list<Adapter>(add_case, wrap<std::optional>(wrap<map_str>(nested_elem_types{})));

    register_list<Adapter>(add_case,
                           type_list<standard_case::Role,
                                     standard_case::Basic,
                                     meta::fixtures::AllPrimitives,
                                     standard_case::Compound,
                                     standard_case::NestedContainers,
                                     standard_case::EmptyContainers,
                                     standard_case::Ultimate,
                                     standard_case::AttrPayload,
                                     standard_case::TaggedExternalHolder,
                                     standard_case::TaggedAdjacentHolder,
                                     standard_case::TaggedInternalHolder>{});

    register_list<Adapter>(add_case, type_list<untagged_variant_t, container_variant_t>{});
}

}  // namespace kota::codec::roundtrip
