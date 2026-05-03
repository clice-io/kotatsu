#pragma once

#include "kota/codec/bincode/deserializer.h"
#include "kota/codec/bincode/error.h"
#include "kota/codec/bincode/serializer.h"
#include "kota/codec/detail/raw_value.h"

namespace kota::codec {

template <typename Config>
struct serialize_traits<bincode::Serializer<Config>, RawValue> {
    using value_type = typename bincode::Serializer<Config>::value_type;
    using error_type = typename bincode::Serializer<Config>::error_type;

    static auto serialize(bincode::Serializer<Config>& serializer, const RawValue& value)
        -> std::expected<value_type, error_type> {
        auto bytes =
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(value.data.data()),
                                       value.data.size());
        return serializer.serialize_bytes(bytes);
    }
};

/// deserialize_traits for RawValue: read length-prefixed bytes from bincode
template <>
struct deserialize_traits<bincode::bincode_backend, RawValue> {
    static auto read(bincode::byte_reader*& v, RawValue& value) -> bincode::error_kind {
        std::vector<std::byte> bytes;
        auto err = bincode::bincode_backend::read_bytes(v, bytes);
        if(err != bincode::error_kind::ok)
            return err;
        value.data.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return bincode::error_kind::ok;
    }
};

}  // namespace kota::codec
