#pragma once

#include <string_view>
#include <utility>

#include "kota/support/ranges.h"
#include "kota/codec/detail/spelling.h"

namespace kota::codec {

template <typename Backend, typename T>
auto deserialize(typename Backend::value_type& src, T& out) -> typename Backend::error_type;

template <typename Backend, typename MapT>
struct map_visitor {
    using E = typename Backend::error_type;
    using key_t = typename MapT::key_type;
    using mapped_t = typename MapT::mapped_type;

    MapT& out;

    E visit_field(std::string_view key, typename Backend::value_type& val) {
        auto parsed_key = spelling::parse_map_key<key_t>(key);
        if(!parsed_key) {
            return Backend::type_mismatch;
        }
        mapped_t mapped{};
        auto err = deserialize<Backend>(val, mapped);
        if(err != Backend::success)
            return err;
        kota::detail::insert_map_entry(out, std::move(*parsed_key), std::move(mapped));
        return Backend::success;
    }
};

}  // namespace kota::codec
