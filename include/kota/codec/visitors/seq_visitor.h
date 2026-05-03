#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "kota/support/ranges.h"

namespace kota::codec {

template <typename Backend, typename T>
auto deserialize(typename Backend::value_type& src, T& out) -> typename Backend::error_type;

template <typename Backend, typename ElemT, typename Container>
struct seq_visitor {
    using E = typename Backend::error_type;

    Container& out;
    std::size_t index = 0;

    E visit_element(typename Backend::value_type& val) {
        ElemT element{};
        auto err = deserialize<Backend>(val, element);
        if(err != Backend::success) [[unlikely]] {
            if constexpr(requires { Backend::report_prepend_index(index); }) {
                Backend::report_prepend_index(index);
            }
            return err;
        }
        kota::detail::append_sequence_element(out, std::move(element));
        ++index;
        return Backend::success;
    }
};

template <typename Backend, typename ArrayT>
struct array_visitor {
    using E = typename Backend::error_type;
    using ElemT = typename ArrayT::value_type;
    static constexpr std::size_t N = std::tuple_size_v<ArrayT>;

    ArrayT& out;
    std::size_t index = 0;

    E visit_element(typename Backend::value_type& val) {
        if(index >= N) [[unlikely]]
            return Backend::type_mismatch;
        auto err = deserialize<Backend>(val, out[index]);
        if(err != Backend::success)
            return err;
        ++index;
        return Backend::success;
    }

    E finish() const {
        if(index != N) [[unlikely]]
            return Backend::type_mismatch;
        return Backend::success;
    }
};

template <typename Backend, typename TupleT>
struct tuple_visitor {
    using E = typename Backend::error_type;
    static constexpr std::size_t N = std::tuple_size_v<TupleT>;

    TupleT& out;
    std::size_t index = 0;
    E error = Backend::success;

    E visit_element(typename Backend::value_type& val) {
        if(index >= N) [[unlikely]]
            return Backend::type_mismatch;
        visit_at(val, std::make_index_sequence<N>{});
        ++index;
        return error;
    }

    E finish() const {
        if(index != N) [[unlikely]]
            return Backend::type_mismatch;
        return Backend::success;
    }

private:
    template <std::size_t... Is>
    void visit_at(typename Backend::value_type& val, std::index_sequence<Is...>) {
        (void)((Is == index ? (error = deserialize<Backend>(val, std::get<Is>(out)), true) : false) ||
               ...);
    }
};

}  // namespace kota::codec
