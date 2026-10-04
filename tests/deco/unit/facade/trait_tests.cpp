#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

enum class Mode {
    Alpha,
    Beta,
};

struct Name {
    std::optional<std::string> into(std::string_view);
};

struct LocatedName {
    std::optional<std::string> into(std::string_view, const decl::IntoContext&);
};

struct Names {
    std::optional<std::string> into(const std::vector<std::string_view>&);
};

struct LocatedNames {
    std::optional<std::string> into(const std::vector<std::string_view>&, const decl::IntoContext&);
};

/// Reads a value, but reports its error in something else than a string.
struct Mistyped {
    std::optional<int> into(std::string_view);
};

ZEST_SUITE(deco_facade_trait) {

ZEST_CASE(flag_result_is_a_bool_or_a_count) {
    ZSTATIC_EXPECT(trait::FlagResultType<bool>);
    ZSTATIC_EXPECT(trait::FlagResultType<std::uint32_t>);
    ZSTATIC_EXPECT(!trait::FlagResultType<int>);
}

ZEST_CASE(scalar_result_is_primitive_or_reads_itself) {
    ZSTATIC_EXPECT(trait::ScalarResultType<bool>);
    ZSTATIC_EXPECT(trait::ScalarResultType<int>);
    ZSTATIC_EXPECT(trait::ScalarResultType<double>);
    ZSTATIC_EXPECT(trait::ScalarResultType<Mode>);
    ZSTATIC_EXPECT(trait::ScalarResultType<std::string>);
    ZSTATIC_EXPECT(trait::ScalarResultType<Name>);
    ZSTATIC_EXPECT(trait::ScalarResultType<LocatedName>);
}

ZEST_CASE(scalar_result_owns_its_text) {
    // A view would outlive the argv it views.
    ZSTATIC_EXPECT(trait::StringResultType<std::string_view>);
    ZSTATIC_EXPECT(!trait::OwnedStringResultType<std::string_view>);
    ZSTATIC_EXPECT(!trait::ScalarResultType<std::string_view>);
    ZSTATIC_EXPECT(!trait::ScalarResultType<const char*>);
}

ZEST_CASE(scalar_result_is_no_list_nor_long_double) {
    ZSTATIC_EXPECT(!trait::ScalarResultType<std::vector<int>>);
    ZSTATIC_EXPECT(!trait::ScalarResultType<long double>);
    ZSTATIC_EXPECT(!trait::ScalarResultType<Mistyped>);
}

ZEST_CASE(vector_result_holds_scalars_or_reads_itself) {
    ZSTATIC_EXPECT(trait::VectorResultType<std::vector<int>>);
    ZSTATIC_EXPECT(trait::VectorResultType<std::vector<std::string>>);
    ZSTATIC_EXPECT(trait::VectorResultType<std::vector<Mode>>);
    ZSTATIC_EXPECT(trait::VectorResultType<Names>);
    ZSTATIC_EXPECT(trait::VectorResultType<LocatedNames>);
    ZSTATIC_EXPECT(!trait::VectorResultType<std::vector<std::string_view>>);
    ZSTATIC_EXPECT(!trait::VectorResultType<std::span<const std::string>>);
    ZSTATIC_EXPECT(!trait::VectorResultType<std::string>);
}

ZEST_CASE(input_result_is_a_scalar_or_a_list) {
    ZSTATIC_EXPECT(trait::InputResultType<int>);
    ZSTATIC_EXPECT(trait::InputResultType<std::string>);
    ZSTATIC_EXPECT(trait::InputResultType<std::vector<Mode>>);
    ZSTATIC_EXPECT(!trait::InputResultType<std::string_view>);
    ZSTATIC_EXPECT(!trait::InputResultType<std::vector<std::string_view>>);
}

ZEST_CASE(optional_result_is_its_value) {
    ZEXPECT(zest::type_eq<trait::OptionalResultType<std::optional<std::string>>, std::string>());
    ZEXPECT(zest::type_eq<trait::OptionalResultType<std::string>, void>());
}

};  // ZEST_SUITE(deco_facade_trait)

}  // namespace

}  // namespace kota::deco
