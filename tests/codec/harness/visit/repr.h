#pragma once

// meta::repr: the declarative, imperative, one-directional, dynamic, chained
// and format-scoped forms, reprs wherever a value can stand, and their
// precedence against field annotations and config.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "codec/harness/fixtures/configs.h"
#include "codec/harness/fixtures/repr.h"
#include "codec/harness/visit/kit.h"
#include "fixtures/structs.h"
#include "kota/meta/annotation.h"
#include "kota/meta/attrs.h"

namespace kota::test {

template <Backend B>
void repr(const Kit<B>& kit) {
    auto symbol = [] {
        return Symbol{
            .rel = Relation::references,
            .ver = {.major = 1, .minor = 22}
        };
    };
    encodes_as(kit, "declarative_encodes_as_plain", symbol, [] {
        return SymbolPlain{.rel = 2, .ver = "1.22"};
    });
    roundtrip(kit, "declarative_roundtrip", symbol);
    auto places = [] {
        return ReprPlaces{
            .relations = {Relation::defines,              Relation::declares            },
            .by_version = {{{.major = 1, .minor = 0}, 10}, {{.major = 2, .minor = 5}, 25}},
            .maybe = Version{.major = 1,                     .minor = 5                    },
        };
    };
    encodes_as(kit, "repr_in_elements_keys_and_optionals", places, [] {
        return ReprPlacesPlain{
            .relations = {1,           0          },
            .by_version = {{"1.0", 10}, {"2.5", 25}},
            .maybe = "1.5",
        };
    });
    roundtrip(kit, "repr_in_elements_keys_and_optionals_roundtrip", places);
    // Decoding into a ReprPlaces whose `maybe` starts engaged: null resets it.
    roundtrip(kit, "repr_in_empty_optional_roundtrip", [] {
        return ReprPlaces{.relations = {}, .by_version = {}, .maybe = std::nullopt};
    });

    using PackedVersion = meta::annotation<Version, meta::behavior::with<VersionAsNumber>>;
    auto packed = [] {
        return Field<PackedVersion>{{{.major = 3, .minor = 14}}};
    };
    encodes_as(kit, "field_annotation_beats_type_repr", packed, [] {
        return Field<std::uint32_t>{3014};
    });
    roundtrip(kit, "field_annotation_beats_type_repr_roundtrip", packed);
    encodes_as(
        kit,
        "one_directional_encodes_as_plain",
        [] { return Field<AuditStamp>{{.at = 1234567}}; },
        [] { return Field<std::uint64_t>{1234567}; });
    auto hex = [] {
        return HexId{.v = 0xDEADBEEF};
    };
    encodes_as(kit, "imperative_encodes_as_plain", hex, [] { return std::string("deadbeef"); });
    roundtrip(kit, "imperative_roundtrip", hex);
    using Loud = meta::annotation<std::string, meta::behavior::with<Shout>>;
    auto loud = [] {
        return Field<Loud>{"loud"};
    };
    encodes_as(kit, "imperative_with_encodes_as_plain", loud, [] {
        return Field<std::string>{"LOUD"};
    });
    roundtrip(kit, "imperative_with_roundtrip", loud);
    auto ticket = [] {
        return Field<Ticket>{{.id = {.v = 7}}};
    };
    encodes_as(kit, "chained_encodes_as_plain", ticket, [] { return Field<std::uint32_t>{7}; });
    roundtrip(kit, "chained_roundtrip", ticket);
    // Decoding into a Stamped whose stamp starts nonzero: the null reaches
    // Lamport's repr, which resets it.
    auto unstamped = [] {
        return Stamped{.stamp = {.tick = 0}};
    };
    encodes_as(kit, "nullable_repr_encodes_as_plain", unstamped, [] {
        return StampedPlain{.stamp = std::nullopt};
    });
    roundtrip(kit, "nullable_repr_roundtrip", unstamped);
    roundtrip(kit, "nullable_repr_engaged_roundtrip", [] { return Stamped{.stamp = {.tick = 5}}; });
    auto fee = [] {
        return Field<BasisPoints>{{.v = 250}};
    };
    encodes_as(kit, "annotation_in_repr_type_encodes_as_plain", fee, [] {
        return Field<double>{250.0};
    });
    roundtrip(kit, "annotation_in_repr_type_roundtrip", fee);
    auto range = [] {
        return Field<LineRange>{
            {.first = 3, .last = 7}
        };
    };
    encodes_as(kit, "struct_attrs_in_repr_type_encode_as_plain", range, [] {
        return Field<LineSpanCamel>{
            {.startLine = 3, .lineCount = 4}
        };
    });
    roundtrip(kit, "struct_attrs_in_repr_type_roundtrip", range);
    auto failed_load = [] {
        return LoadResult{.ok = false, .bytes = 0, .message = "missing"};
    };
    // Tags shape a keyed document; elsewhere the variant travels by index.
    if constexpr(B::caps.self_describing) {
        encodes_as(kit, "tagging_in_repr_type_encodes_as_plain", failed_load, [] {
            return LoadDocument{.status = "err", .value = {.message = "missing"}};
        });
    } else {
        encodes_as(kit, "tagging_in_repr_type_encodes_as_plain", failed_load, [] {
            return std::variant<LoadOk, LoadErr>{LoadErr{.message = "missing"}};
        });
    }
    roundtrip(kit, "tagging_in_repr_type_roundtrip", failed_load);
    using StrictLoad = meta::annotate<StrictCamelTag>::type<LoadResult>;
    auto strict_load = [] {
        return Field<StrictLoad>{{{.ok = true, .bytes = 3, .message = {}}}};
    };
    if constexpr(B::caps.self_describing) {
        encodes_as(kit, "outer_policy_reaches_tagged_repr_alternatives", strict_load, [] {
            return Field<LoadOkDocument<ByteCountCamel>>{
                {.status = "ok", .value = {.byteCount = 3}}
            };
        });
    }
    roundtrip(kit, "outer_policy_reaches_tagged_repr_alternatives_roundtrip", strict_load);
    auto adapted = [] {
        return AdaptedChoice{7};
    };
    encodes_as(kit, "adapter_beats_tagging", adapted, [] { return std::string("i:7"); });
    roundtrip(kit, "adapter_beats_tagging_roundtrip", adapted);
    roundtrip(kit, "repr_alternative_roundtrip", [] {
        return std::vector<std::variant<Version, int>>{
            Version{.major = 1, .minor = 22},
            7
        };
    });
    auto packed_alternative = [] {
        return std::variant<PackedVersion, std::string>{PackedVersion{{.major = 3, .minor = 14}}};
    };
    encodes_as(kit, "annotated_repr_alternative_encodes_as_plain", packed_alternative, [] {
        return std::variant<std::uint32_t, std::string>{std::uint32_t{3014}};
    });
    roundtrip(kit, "annotated_repr_alternative_roundtrip", packed_alternative);

    // A backend with a format tag carries Journal as an integer; one without
    // sees the format-agnostic textual repr.
    auto journal = [] {
        return Field<Journal>{{.page = 41}};
    };
    auto journals = [] {
        return std::map<Journal, int>{
            {{.page = 1}, 10},
            {{.page = 2}, 20}
        };
    };
    if constexpr(B::caps.format_tag) {
        encodes_as(kit, "format_scoped_repr_encodes_as_plain", journal, [] {
            return Field<std::int64_t>{41};
        });
        encodes_as(kit, "format_scoped_map_keys_encode_as_plain", journals, [] {
            return std::map<std::int64_t, int>{
                {1, 10},
                {2, 20}
            };
        });
    } else {
        encodes_as(kit, "format_scoped_repr_encodes_as_plain", journal, [] {
            return Field<std::string>{"p41"};
        });
        encodes_as(kit, "format_scoped_map_keys_encode_as_plain", journals, [] {
            return std::map<std::string, int>{
                {"p1", 10},
                {"p2", 20}
            };
        });
    }
    roundtrip(kit, "format_scoped_repr_roundtrip", journal);
    roundtrip(kit, "format_scoped_map_keys_roundtrip", journals);

    if constexpr(B::caps.dynamic_repr) {
        auto dynamic = [] {
            return DynamicPair{.number = {.v = std::int64_t{42}}, .text = {.v = "free"}};
        };
        encodes_as(kit, "dynamic_encodes_as_plain", dynamic, [] {
            return DynamicPairPlain{.number = 42, .text = "free"};
        });
        roundtrip(kit, "dynamic_roundtrip", dynamic);
    }
    if constexpr(B::caps.string_knobs) {
        encodes_as<EnumStringConfig>(
            kit,
            "repr_beats_enum_string_config",
            [] { return Relation::references; },
            [] { return std::uint32_t{2}; });
    }

    if constexpr(B::caps.self_describing) {
        // The encoded value may be null, but the field itself must be there.
        read_fails<Stamped>(kit,
                            "nullable_repr_keeps_field_required_fails",
                            [] { return Empty{}; },
                            {.message = "missing required field 'stamp'", .path = ""});
        read_fails<Field<LineRange>>(kit,
                                     "struct_attrs_in_repr_type_deny_unknown_fails",
                                     [] {
                                         return Field<LineSpanCamelWithExtra>{
                                             {.startLine = 3, .lineCount = 4, .x = 1}
                                         };
                                     },
                                     {.message = "unknown field 'x'", .path = "value"});
        read_fails<Field<StrictLoad>>(kit,
                                      "outer_policy_denies_unknown_in_alternative_fails",
                                      [] {
                                          return Field<LoadOkDocument<ByteCountCamelWithExtra>>{
                                              {.status = "ok", .value = {.byteCount = 3, .x = 1}}
                                          };
                                      },
                                      {.message = "unknown field 'x'", .path = "value"});
        // Untagged probing judges an alternative by the repr the backend's
        // format selects.
        using JournalOrNumber = std::variant<Journal, std::int64_t>;
        reads<JournalOrNumber>(
            kit,
            "format_scoped_repr_guides_probing",
            [] { return std::int64_t{42}; },
            [] {
                if constexpr(B::caps.format_tag) {
                    return JournalOrNumber{Journal{.page = 42}};
                } else {
                    return JournalOrNumber{std::int64_t{42}};
                }
            });
        reads<std::variant<Version, int>>(
            kit,
            "repr_alternative_reads_by_its_repr_kind",
            [] { return std::string("1.22"); },
            [] {
                return std::variant<Version, int>{
                    Version{.major = 1, .minor = 22}
                };
            });
    }
}

}  // namespace kota::test
