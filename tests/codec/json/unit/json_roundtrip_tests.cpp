#include <string_view>

#include "codec/harness/roundtrip_suite.h"
#include "kota/zest/zest.h"
#include "kota/codec/json/json.h"

namespace kota::codec {

namespace {

using roundtrip::cap;

struct json_adapter {
    constexpr static std::string_view name = "json";
    constexpr static cap caps =
        cap::Uint64Full | cap::NullInSeq | cap::VariantPlain | cap::Attrs | cap::VariantTagged;

    template <typename T>
    static auto run(const T& input) -> std::expected<T, rich_error> {
        auto encoded = json::to_string(input);
        if(!encoded) {
            return std::unexpected(std::move(encoded).error());
        }
        return json::from_string<T>(*encoded);
    }
};

ZEST_SUITE(codec_json_roundtrip) {

ZEST_CASE_GROUP(standard_corpus) {
    roundtrip::register_cases<json_adapter>(add_case);
}

};  // ZEST_SUITE(codec_json_roundtrip)

}  // namespace

}  // namespace kota::codec
