#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codec/blob/harness/fixtures.h"
#include "kotatsu/zest/zest.h"
#include "kotatsu/codec/blob/blob.h"

namespace kotatsu::codec::blob {

namespace {

template <template <typename> typename Seq>
struct DeepOf {
    Seq<Seq<Seq<char>>> deep;
    Seq<Seq<test::LeafOf<Seq>>> groups;
};

using Deep = DeepOf<Owned>;

using Parts = std::vector<std::pair<std::string, std::uint64_t>>;

/// What measure reports for the root of `bytes`, a path's consecutive parts
/// summed.
template <typename T>
Parts measured(std::span<const std::byte> bytes) {
    auto viewed = view<T>(bytes);
    ZASSERT(viewed);
    Parts parts;
    measure(**viewed, [&](std::string_view path, std::uint64_t size) {
        if(!parts.empty() && parts.back().first == path) {
            parts.back().second += size;
        } else {
            parts.emplace_back(std::string(path), size);
        }
    });
    return parts;
}

ZEST_SUITE(codec_blob_measure) {

ZEST_CASE(measure_splits_bytes_by_field) {
    Parts expected = {
        {"text",          5        },
        {"empty_text",    0        },
        {"numbers",       12       },
        {"empty_numbers", 0        },
        {"names",         3 * 8 + 4},
        {"points",        2 * 16   },
        {"stamps",        2 * 8    },
        {"leaves",        3 * 16   },
        {"leaves.name",   1        },
        {"leaves.values", 4        },
        {"leaves.name",   0        },
        {"leaves.values", 0        },
        {"leaves.name",   2        },
        {"leaves.values", 2        },
        {"badge.label",   5        },
        {"bytes",         5        },
    };
    ZEXPECT(measured<test::Sample>(test::blob_of(test::sample())) == expected);
}

ZEST_CASE(measure_counts_every_depth) {
    // The inner sequences of a sequence of sequences, and the records inside
    // them, count under the outer one.
    Deep value{
        .deep = {{"abc", "de"}, {}},
        .groups = {{{.name = "x", .values = {1}}}},
    };
    Parts expected = {
        {"deep",          2 * 8 + 2 * 8 + 3 + 2},
        {"groups",        8 + 16               },
        {"groups.name",   1                    },
        {"groups.values", 2                    },
    };
    ZEXPECT(measured<Deep>(test::blob_of(value)) == expected);
}

};  // ZEST_SUITE(codec_blob_measure)

}  // namespace

}  // namespace kotatsu::codec::blob
