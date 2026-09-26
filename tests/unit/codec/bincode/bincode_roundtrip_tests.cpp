#include <string_view>

#include "../roundtrip_suite.h"
#include "kota/zest/zest.h"
#include "kota/codec/bincode/bincode.h"

namespace kota::codec {

namespace {

using roundtrip::cap;

struct bincode_adapter {
    constexpr static std::string_view name = "bincode";
    constexpr static cap caps = cap::Uint64Full | cap::NullInSeq | cap::VariantPlain;

    template <typename T>
    static auto run(const T& input) -> std::expected<T, bincode::error> {
        auto encoded = bincode::to_bytes(input);
        if(!encoded) {
            return std::unexpected(encoded.error());
        }
        return bincode::from_bytes<T>(*encoded);
    }
};

ZEST_SUITE(serde_bincode_roundtrip){

    ZEST_CASE_GROUP(standard_corpus){roundtrip::register_cases<bincode_adapter>(add_case);

}  // namespace

};  // namespace kota::codec

}  // namespace

}  // namespace kota::codec
