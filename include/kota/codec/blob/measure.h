#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "kota/meta/struct.h"
#include "kota/codec/blob/type.h"

namespace kota::codec::blob {

namespace detail {

template <typename A, typename Visit>
void measure_part(const A& part, Visit& visit, std::string& path);

template <typename A, typename Visit>
void measure_record(const A& record, Visit& visit, std::string& path) {
    for_each_index<A>([&](auto index) {
        if constexpr(!is_plain<field_t<A, index()>>()) {
            auto prefix = path.size();
            if(prefix != 0) {
                path += '.';
            }
            path += meta::field_name<index(), A>();
            detail::measure_part(meta::field_of<index()>(record), visit, path);
            path.resize(prefix);
        }
    });
}

/// `part`, a Vec or a record holding one.
template <typename A, typename Visit>
void measure_part(const A& part, Visit& visit, std::string& path) {
    if constexpr(kind_of<A>() == Kind::Record) {
        detail::measure_record(part, visit, path);
    } else {
        using E = element_t<A>;
        visit(std::string_view(path), std::uint64_t(part.size()) * sizeof(E));
        if constexpr(!is_plain<E>()) {
            for(const auto& element: part) {
                detail::measure_part(element, visit, path);
            }
        }
    }
}

}  // namespace detail

/// Calls `visit(path, bytes)` for each out-of-line part of `root`, a view,
/// with the path of the field holding it (`ranges.packed`; a sequence's
/// elements count under the sequence, at every depth) and its bytes: what a
/// field costs beyond the inline parts and the padding.
template <typename A, typename Visit>
void measure(const A& root, Visit&& visit) {
    static_assert(detail::kind_of<A>() == detail::Kind::Record, "a blob's root is a record");
    std::string path;
    detail::measure_record(root, visit, path);
}

}  // namespace kota::codec::blob
