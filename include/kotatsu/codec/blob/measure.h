#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "kotatsu/meta/struct.h"
#include "kotatsu/codec/blob/type.h"

namespace kotatsu::codec::blob {

namespace detail {

/// `part`, a Vec or a record holding one, under `path`.
template <typename A, typename Visit>
void measure_part(const A& part, Visit& visit, std::string& path) {
    if constexpr(kind_of<A>() == Kind::Record) {
        for_each_index<A>([&](auto index) {
            if constexpr(!is_plain<field_t<A, index()>>()) {
                auto prefix = path.size();
                if(prefix != 0) {
                    path += '.';
                }
                path += meta::field_name<index(), A>();
                detail::measure_part(meta::field_of<index()>(part), visit, path);
                path.resize(prefix);
            }
        });
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

/// Calls `visit(path, bytes)` for every Vec under `root`, a view, with the
/// path of the field holding it (`leaves.name`; a sequence's elements count
/// under the sequence, at every depth) and the bytes of its part, 0 for an
/// empty one: what a field costs beyond the inline parts and the padding.
template <typename A, typename Visit>
void measure(const A& root, Visit&& visit) {
    static_assert(detail::kind_of<A>() == detail::Kind::Record, "a blob's root is a record");
    std::string path;
    detail::measure_part(root, visit, path);
}

}  // namespace kotatsu::codec::blob
