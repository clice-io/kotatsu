#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/argv.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

constexpr decl::Category shared_category{
    .exclusive = false,
    .name = "shared",
    .description = "set together",
};

constexpr decl::Category trailing_category{
    .exclusive = false,
    .name = "trailing",
    .description = "after --",
};

struct Nested {
    DecoKV(required = false;)
    <std::string> out_path;
};

struct Everything {
    DecoFlag(names = {"-v", "--verbose"}; required = false; category = shared_category;
             after_parsed = Action::next;)
    verbose;

    DecoKV(required = false; help = "the level"; meta_var = "N";)
    <int> level;

    DecoKVStyled(decl::KVStyle::Joined, names = {"-I", "--include"}; required = false;)
    <std::string> include;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, names = {"--flags", "--flags="};
                 required = false;)
    <std::string> flags;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, required = false;)
    <std::string> o;

    DecoKVStyled(decl::KVStyle::Joined, required = false;)
    <std::string> joined;

    DecoComma(names = {"-T"}; required = false;)
    <std::vector<std::string>> tags;

    DecoMulti(2, names = {"-P", "--pair"}; required = false;)
    <std::vector<std::string>> pair;

    DecoFlag(names = {"/w"}; required = false;)
    slash;

    Nested nested;

    DecoInput(required = false; category = shared_category;)
    <std::string> input;

    DecoPack(required = false; category = trailing_category;)
    <std::vector<std::string>> pack;

    DecoFlagAlias(names = {"-O1", "--opt-one"}; forward = {"--level", "1"};
                  category = shared_category;) _;
};

struct ManyNames {
    DecoFlag(required = false;)
    a;

    DecoFlag(names = {"-b", "--bb", "--bbb", "--bbbb"}; required = false;)
    b;

    DecoKVStyled(decl::KVStyle::JoinedOrSeparate, names = {"-c", "--cc=", "--ccc", "--cccc="};
                 required = false;)
    <int> c;
};

struct NoInput {
    DecoFlag(required = false;)
    verbose;
};

/// What the table T's generator builds parses `line` into, up to the first error.
struct Parsed {
    std::vector<std::string> argv;
    std::vector<option::ParsedArg> args;
    std::optional<option::ParseError> error;
};

template <typename T>
Parsed parse_with_table(std::string_view line) {
    const auto& generator = detail::generator_of<T>();
    Parsed parsed{.argv = test::split(line), .args = {}, .error = {}};
    const auto table = generator.make_opt_table();
    for(const auto& result: table.parse(parsed.argv, generator.make_parse_options())) {
        if(!result.has_value()) {
            parsed.error = result.error();
            break;
        }
        parsed.args.push_back(*result);
    }
    return parsed;
}

/// The option `spelling` names in T's table.
template <typename T>
const option::Option* option_named(std::string_view spelling) {
    for(const auto& option: detail::generator_of<T>().option_infos()) {
        if(option.prefixed_name == spelling) {
            return &option;
        }
    }
    return nullptr;
}

ZEST_SUITE(deco_facade_backend) {

ZEST_CASE(ids_index_the_options) {
    const auto infos = detail::generator_of<Everything>().option_infos();
    for(std::size_t i = 0; i < infos.size(); ++i) {
        ZEST_CONTEXT("option {}", infos[i].prefixed_name);
        EXPECT(infos[i].id == i + 1);
    }
}

ZEST_CASE(every_table_has_unknown_and_input) {
    const auto& generator = detail::generator_of<NoInput>();
    const auto infos = generator.option_infos();
    ASSERT(infos.size() == 3U);
    EXPECT(infos[0].kind == option::Kind::Unknown);
    EXPECT(infos[0].id == generator.unknown_option_id);
    EXPECT(infos[1].kind == option::Kind::Input);
    EXPECT(infos[1].id == generator.input_option_id);
    EXPECT(!generator.has_input_option());
    EXPECT(!generator.has_trailing_option());
}

ZEST_CASE(field_names_become_option_names) {
    // Underscores are spelled '-', and a one-letter name takes one '-'.
    EXPECT(option_named<Everything>("--level") != nullptr);
    EXPECT(option_named<Everything>("--out-path") != nullptr);
    EXPECT(option_named<Everything>("-o") != nullptr);
    EXPECT(option_named<Everything>("--joined") != nullptr);
}

ZEST_CASE(names_keep_their_prefix) {
    const auto* dash = option_named<Everything>("-v");
    ASSERT(dash != nullptr);
    ASSERT(dash->prefixes.size() == 1U);
    EXPECT(dash->prefixes[0] == "-");

    const auto* double_dash = option_named<Everything>("--verbose");
    ASSERT(double_dash != nullptr);
    ASSERT(double_dash->prefixes.size() == 1U);
    EXPECT(double_dash->prefixes[0] == "--");

    // A '/' option may be written with '-' too.
    const auto* slash = option_named<Everything>("/w");
    ASSERT(slash != nullptr);
    ASSERT(slash->prefixes.size() == 2U);
    EXPECT(slash->prefixes[0] == "/");
}

ZEST_CASE(every_name_parses_into_the_field) {
    const auto parsed = parse_with_table<Everything>(
        "-v --verbose /w -w --out-path x -T,a --pair a b -P c d main.cc");
    ASSERT(!parsed.error.has_value());
    ASSERT(parsed.args.size() == 9U);

    Everything options;
    const auto& generator = detail::generator_of<Everything>();
    const std::vector<void*> fields = {
        &options.verbose,
        &options.verbose,
        &options.slash,
        &options.slash,
        &options.nested.out_path,
        &options.tags,
        &options.pair,
        &options.pair,
        &options.input,
    };
    for(std::size_t i = 0; i < parsed.args.size(); ++i) {
        ZEST_CONTEXT("argument {}", parsed.args[i].spelling);
        EXPECT(generator.field_ptr_of(parsed.args[i].id, options) == fields[i]);
    }
}

ZEST_CASE(option_with_many_names_is_built) {
    // Each further name copies the option, which used to read the option after the
    // counting pass had moved it.
    const auto parsed = parse_with_table<ManyNames>("-a -b --bbb --bbbb -c 1 --cc=2 --cccc=3");
    ASSERT(!parsed.error.has_value());
    ASSERT(parsed.args.size() == 7U);

    ManyNames options;
    const auto& generator = detail::generator_of<ManyNames>();
    for(std::size_t i = 1; i < 4; ++i) {
        ZEST_CONTEXT("argument {}", parsed.args[i].spelling);
        EXPECT(generator.field_ptr_of(parsed.args[i].id, options) == &options.b);
    }
    for(std::size_t i = 4; i < 7; ++i) {
        ZEST_CONTEXT("argument {}", parsed.args[i].spelling);
        EXPECT(generator.field_ptr_of(parsed.args[i].id, options) == &options.c);
    }
}

ZEST_CASE(kv_is_separate_by_default) {
    const auto* level = option_named<Everything>("--level");
    ASSERT(level != nullptr);
    EXPECT(level->kind == option::Kind::Separate);
    EXPECT(level->num_args == 1U);
    EXPECT(option_named<Everything>("--level=") == nullptr);
}

ZEST_CASE(joined_kv_takes_each_name_as_spelled) {
    const auto* dash = option_named<Everything>("-I");
    ASSERT(dash != nullptr);
    EXPECT(dash->kind == option::Kind::Joined);
    const auto* double_dash = option_named<Everything>("--include");
    ASSERT(double_dash != nullptr);
    EXPECT(double_dash->kind == option::Kind::Joined);
    EXPECT(option_named<Everything>("--include=") == nullptr);
}

ZEST_CASE(joined_or_separate_kv_takes_each_name_by_its_end) {
    const auto* separate = option_named<Everything>("--flags");
    ASSERT(separate != nullptr);
    EXPECT(separate->kind == option::Kind::Separate);
    const auto* joined = option_named<Everything>("--flags=");
    ASSERT(joined != nullptr);
    EXPECT(joined->kind == option::Kind::Joined);
}

ZEST_CASE(generated_kv_name_also_takes_its_value_after_equals) {
    const auto* separate = option_named<Everything>("-o");
    ASSERT(separate != nullptr);
    EXPECT(separate->kind == option::Kind::Separate);
    const auto* joined = option_named<Everything>("-o=");
    ASSERT(joined != nullptr);
    EXPECT(joined->kind == option::Kind::Joined);

    // Joined alone: the name takes its value right after it, or after '='.
    const auto* bare = option_named<Everything>("--joined");
    ASSERT(bare != nullptr);
    EXPECT(bare->kind == option::Kind::Joined);
    const auto* equals = option_named<Everything>("--joined=");
    ASSERT(equals != nullptr);
    EXPECT(equals->kind == option::Kind::Joined);
}

ZEST_CASE(comma_and_multi_take_their_kinds) {
    const auto* tags = option_named<Everything>("-T");
    ASSERT(tags != nullptr);
    EXPECT(tags->kind == option::Kind::CommaJoined);
    const auto* pair = option_named<Everything>("--pair");
    ASSERT(pair != nullptr);
    EXPECT(pair->kind == option::Kind::MultiArg);
    EXPECT(pair->num_args == 2U);
}

ZEST_CASE(help_and_meta_var_reach_the_table) {
    const auto* level = option_named<Everything>("--level");
    ASSERT(level != nullptr);
    EXPECT(std::string_view(level->help_text) == "the level");
    EXPECT(std::string_view(level->meta_var) == "N");
}

ZEST_CASE(every_spelling_shares_category_and_callback) {
    const auto& generator = detail::generator_of<Everything>();
    const auto* short_name = option_named<Everything>("-v");
    const auto* long_name = option_named<Everything>("--verbose");
    ASSERT(short_name != nullptr);
    ASSERT(long_name != nullptr);
    EXPECT(generator.category_of(short_name->id) == &shared_category);
    EXPECT(generator.category_of(long_name->id) == &shared_category);
    EXPECT(bool(generator.callback_of(short_name->id)));
    EXPECT(bool(generator.callback_of(long_name->id)));

    const auto* level = option_named<Everything>("--level");
    ASSERT(level != nullptr);
    EXPECT(generator.category_of(level->id) == &decl::default_category);
    EXPECT(!generator.callback_of(level->id));
}

ZEST_CASE(alias_forwards_and_reaches_no_field) {
    const auto& generator = detail::generator_of<Everything>();
    for(const auto spelling: {"-O1", "--opt-one"}) {
        ZEST_CONTEXT("alias {}", spelling);
        const auto* alias = option_named<Everything>(spelling);
        ASSERT(alias != nullptr);
        EXPECT(alias->kind == option::Kind::Flag);
        const auto* meta = generator.alias_meta_of(alias->id);
        ASSERT(meta != nullptr);
        EXPECT(meta->kind == decl::DecoType::Flag);
        EXPECT(meta->forward_kind == decl::AliasForwardField::Kind::Static);
        EXPECT(std::vector(meta->static_tokens.begin(), meta->static_tokens.end()) ==
               (std::vector<std::string_view>{"--level", "1"}));
        EXPECT(generator.category_of(alias->id) == &shared_category);
    }
}

ZEST_CASE(option_is_no_alias) {
    const auto* level = option_named<Everything>("--level");
    ASSERT(level != nullptr);
    EXPECT(detail::generator_of<Everything>().alias_meta_of(level->id) == nullptr);
}

ZEST_CASE(input_and_pack_share_the_input_option) {
    const auto parsed = parse_with_table<Everything>("main.cc -- a b");
    ASSERT(!parsed.error.has_value());
    ASSERT(parsed.args.size() == 2U);

    const auto& generator = detail::generator_of<Everything>();
    EXPECT(generator.has_input_option());
    EXPECT(generator.has_trailing_option());
    EXPECT(generator.is_input_argument(parsed.args[0]));
    EXPECT(!generator.is_trailing_argument(parsed.args[0]));
    EXPECT(generator.is_trailing_argument(parsed.args[1]));
    EXPECT(!generator.is_input_argument(parsed.args[1]));

    Everything options;
    EXPECT(generator.trailing_ptr_of(options) == &options.pack);
    EXPECT(generator.trailing_category() == &trailing_category);
    EXPECT(generator.category_of(generator.input_option_id) == &shared_category);
}

ZEST_CASE(dash_dash_packs_only_with_a_pack) {
    const auto with_pack = detail::generator_of<Everything>().make_parse_options();
    EXPECT(with_pack.dash_dash_parsing);
    EXPECT(with_pack.dash_dash_packing);
    const auto without = detail::generator_of<NoInput>().make_parse_options();
    EXPECT(!without.dash_dash_parsing);
    EXPECT(!without.dash_dash_packing);
}

ZEST_CASE(visit_fields_hands_over_each_option_and_alias) {
    std::vector<std::string> names;
    const bool finished = detail::generator_of<Everything>().visit_fields(
        Everything{},
        [&](const auto&, const auto&, std::string_view name, auto) {
            names.emplace_back(name);
            return true;
        });
    EXPECT(finished);
    EXPECT(names == (std::vector<std::string>{"verbose",
                                              "level",
                                              "include",
                                              "flags",
                                              "o",
                                              "joined",
                                              "tags",
                                              "pair",
                                              "slash",
                                              "out_path",
                                              "input",
                                              "pack",
                                              "_"}));
}

ZEST_CASE(visit_fields_stops_when_told) {
    std::size_t visited = 0;
    const bool finished = detail::generator_of<Everything>().visit_fields(
        Everything{},
        [&](const auto&, const auto&, std::string_view name, auto) {
            ++visited;
            return name != "level";
        });
    EXPECT(!finished);
    EXPECT(visited == 2U);
}

ZEST_CASE(visit_schema_fields_hands_over_the_types) {
    std::vector<std::string> names;
    detail::generator_of<Everything>().visit_schema_fields(
        [&](auto field, const auto&, std::string_view name, auto) {
            using field_t = typename decltype(field)::type;
            if constexpr(ty::deco_option_like<field_t>) {
                names.emplace_back(name);
            }
            return true;
        });
    EXPECT(names.size() == 12U);
    ASSERT(!names.empty());
    EXPECT(names.front() == "verbose");
}

};  // ZEST_SUITE(deco_facade_backend)

}  // namespace

}  // namespace kota::deco
