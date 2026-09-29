#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "deco/harness/argv.h"
#include "deco/harness/text.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

constexpr decl::Category mode_category{
    .exclusive = true,
    .required = true,
    .name = "mode",
    .description = "one mode at a time",
};

auto forward_static(const ParsedArgOwning&) -> decl::AliasForwardResult {
    return std::vector<std::string>{"--target"};
}

auto forward_with_context(const ParsedArgOwning&, const decl::IntoContext&)
    -> decl::AliasForwardResult {
    return std::vector<std::string>{"--target"};
}

/// An option kind of its own, which only knows how to read an argument without context.
struct Counting : decl::DecoOptionBase {
    using decl::DecoOptionBase::into;

    int calls = 0;

    std::optional<std::string> into(const ParsedArgOwning&) override {
        ++calls;
        return std::nullopt;
    }
};

ParsedArgOwning owning(std::uint32_t index, std::vector<std::string> values = {}) {
    return ParsedArgOwning{.id = 3, .index = index, .spelling = "-o", .values = std::move(values)};
}

ZEST_SUITE(deco_facade_decl) {

ZEST_CASE(parsed_arg_owning_copies_the_argument) {
    std::string text = "value";
    option::ParsedArg arg{.id = 4, .index = 2, .next_index = 4, .spelling = "-o", .values = {}};
    arg.add_value(text);
    const auto copy = ParsedArgOwning::from(arg);
    text = "changed";
    EXPECT(copy.id == 4U);
    EXPECT(copy.index == 2U);
    EXPECT(copy.spelling == "-o");
    EXPECT(copy.values == std::vector<std::string>{"value"});
}

ZEST_CASE(category_ref_starts_at_the_default_category) {
    const decl::CategoryRef ref;
    EXPECT(ref.ptr() == &decl::default_category);
    EXPECT(ref->name == "default");
    EXPECT(!ref->exclusive);
    EXPECT(!ref->required);
}

ZEST_CASE(category_ref_refers_to_what_it_is_given) {
    decl::CategoryRef ref;
    ref = mode_category;
    EXPECT(ref.ptr() == &mode_category);
    EXPECT(&ref.get() == &mode_category);
    EXPECT(&*ref == &mode_category);
    const decl::Category& category = ref;
    EXPECT(&category == &mode_category);
}

ZEST_CASE(meta_var_is_explicit_once_assigned) {
    decl::MetaVarField meta_var;
    EXPECT(meta_var.value == "<value>");
    EXPECT(!meta_var.is_explicit());
    meta_var = "FILE";
    EXPECT(meta_var.is_explicit());
    EXPECT(std::string_view(meta_var) == "FILE");
    EXPECT(!meta_var.empty());
}

ZEST_CASE(config_override_is_set_only_by_assignment) {
    decl::ConfigOverrideField<bool> field = true;
    EXPECT(!field.is_overridden());
    EXPECT(field.get());
    field = false;
    EXPECT(field.is_overridden());
    EXPECT(!field.get());
}

ZEST_CASE(config_fields_override_nothing_by_default) {
    const decl::ConfigFields fields{};
    EXPECT(!fields.required.is_overridden());
    EXPECT(!fields.category.is_overridden());
    EXPECT(!fields.help.is_overridden());
    EXPECT(!fields.meta_var.is_overridden());
}

ZEST_CASE(alias_forward_starts_empty) {
    const decl::AliasForwardField forward;
    EXPECT(!forward);
    EXPECT(forward.kind == decl::AliasForwardField::Kind::None);
}

ZEST_CASE(alias_forward_takes_tokens) {
    decl::AliasForwardField forward;
    forward = {"--optimize", "1"};
    EXPECT(bool(forward));
    EXPECT(forward.kind == decl::AliasForwardField::Kind::Static);
    EXPECT(forward.static_tokens == std::vector<std::string_view>{"--optimize", "1"});

    forward = std::vector<std::string_view>{"--define"};
    EXPECT(forward.static_tokens == std::vector<std::string_view>{"--define"});
}

ZEST_CASE(alias_forward_takes_a_function) {
    decl::AliasForwardField forward;
    forward = {"--optimize"};
    forward = &forward_static;
    EXPECT(forward.kind == decl::AliasForwardField::Kind::Dynamic);
    EXPECT(forward.dynamic == &forward_static);
    EXPECT(forward.static_tokens.empty());

    forward = &forward_with_context;
    EXPECT(forward.kind == decl::AliasForwardField::Kind::DynamicWithContext);
    EXPECT(forward.dynamic_with_context == &forward_with_context);
    EXPECT(forward.dynamic == nullptr);
}

ZEST_CASE(alias_forward_of_a_null_function_is_empty) {
    decl::AliasForwardField forward;
    forward = static_cast<decl::AliasForwardFn>(nullptr);
    EXPECT(!forward);
    forward = static_cast<decl::AliasForwardFnWithContext>(nullptr);
    EXPECT(!forward);
}

ZEST_CASE(parse_control_next_and_stop) {
    EXPECT(decl::ParseControl::next().action == decl::ParseControl::Action::Continue);
    EXPECT(decl::ParseControl::stop().action == decl::ParseControl::Action::Stop);
}

ZEST_CASE(parse_control_restart_views_a_span) {
    auto argv = test::args("-v");
    const auto control = decl::ParseControl::restart(std::span<std::string>(argv));
    EXPECT(control.action == decl::ParseControl::Action::Restart);
    EXPECT(control.next_argv.data() == argv.data());
    EXPECT(!control.owned_next_argv);
}

ZEST_CASE(parse_control_restart_owns_a_vector) {
    const auto control = decl::ParseControl::restart(test::args("-v", "x"));
    EXPECT(control.action == decl::ParseControl::Action::Restart);
    ASSERT(control.owned_next_argv != nullptr);
    EXPECT(control.next_argv.data() == control.owned_next_argv->data());
    EXPECT(control.next_argv.size() == 2U);
}

ZEST_CASE(parse_step_reads_its_parts) {
    auto argv = test::args("-o", "out");
    const auto arg = owning(0, {"out"});
    const std::string value = "out";
    const decl::ParseStep<std::string> step(arg, 2, argv, value);
    EXPECT(&step.arg() == &arg);
    EXPECT(step.next_cursor() == 2U);
    EXPECT(step.argv().size() == 2U);
    EXPECT(step.value() == "out");
    EXPECT(step.next().action == decl::ParseControl::Action::Continue);
    EXPECT(step.stop().action == decl::ParseControl::Action::Stop);
    EXPECT(step.restart(step.argv()).action == decl::ParseControl::Action::Restart);
    EXPECT(step.restart(test::args("x")).owned_next_argv != nullptr);
}

ZEST_CASE(into_context_at_cursor_clamps_to_the_end) {
    const auto argv = test::args("a", "b");
    const auto context = decl::IntoContext::at_cursor(argv, 7);
    EXPECT(context.highlight_begin() == 2U);
    EXPECT(context.highlight_end() == 2U);
    EXPECT(context.argv().size() == 2U);
}

ZEST_CASE(into_context_of_an_argument_spans_its_values) {
    const auto argv = test::args("-v", "--pair", "a", "b", "c");
    const auto context = decl::IntoContext::from_argument(argv, owning(1, {"a", "b"}));
    EXPECT(context.highlight_begin() == 1U);
    EXPECT(context.highlight_end() == 4U);
}

ZEST_CASE(into_context_of_an_argument_ends_where_argv_differs) {
    // A joined value, "-oout", is no element of its own.
    const auto argv = test::args("--pair", "a", "x", "-oout");
    const auto pair = decl::IntoContext::from_argument(argv, owning(0, {"a", "b"}));
    EXPECT(pair.highlight_begin() == 0U);
    EXPECT(pair.highlight_end() == 2U);
    const auto joined = decl::IntoContext::from_argument(argv, owning(3, {"out"}));
    EXPECT(joined.highlight_begin() == 3U);
    EXPECT(joined.highlight_end() == 4U);
}

ZEST_CASE(into_context_of_an_argument_past_argv_is_at_the_end) {
    const auto argv = test::args("a");
    const auto context = decl::IntoContext::from_argument(argv, owning(3));
    EXPECT(context.highlight_begin() == 1U);
    EXPECT(context.highlight_end() == 1U);
}

ZEST_CASE(into_context_of_a_value_is_its_element) {
    const auto argv = test::args("--pair", "a", "b");
    const auto context = decl::IntoContext::from_value(argv, owning(0, {"a", "b"}), "b");
    EXPECT(context.highlight_begin() == 2U);
    EXPECT(context.highlight_end() == 3U);
}

ZEST_CASE(into_context_of_an_input_value_is_the_argument) {
    const auto argv = test::args("main.cc");
    const auto context = decl::IntoContext::from_value(argv, owning(0), "main.cc");
    EXPECT(context.highlight_begin() == 0U);
    EXPECT(context.highlight_end() == 1U);
}

ZEST_CASE(into_context_of_a_joined_value_is_the_argument) {
    const auto argv = test::args("-oout");
    const auto context = decl::IntoContext::from_value(argv, owning(0, {"out"}), "out");
    EXPECT(context.highlight_begin() == 0U);
    EXPECT(context.highlight_end() == 1U);
}

ZEST_CASE(format_error_positions_the_message) {
    const auto renderer = test::tagged_renderer();
    const auto argv = test::args("a", "b");
    const decl::IntoContext context(argv, 1, 2, &renderer);
    EXPECT(context.format_error("bad") == "ERR<1:bad>");
    EXPECT(context.renderer() == &renderer);
}

ZEST_CASE(format_error_without_argv_is_the_message) {
    const decl::IntoContext context;
    EXPECT(context.format_error("bad") == "bad");
}

ZEST_CASE(format_error_passes_no_error_through) {
    const auto renderer = test::tagged_renderer();
    const auto argv = test::args("a");
    const decl::IntoContext context(argv, 0, 1, &renderer);
    EXPECT(!context.format_error(std::optional<std::string>()).has_value());
    EXPECT(context.format_error(std::optional<std::string>("bad")) == "ERR<0:bad>");
}

ZEST_CASE(alias_placeholder_names_are_underscores) {
    EXPECT(decl::is_alias_placeholder_name("_"));
    EXPECT(decl::is_alias_placeholder_name("___"));
    EXPECT(decl::is_alias_placeholder_name("__deco_alias_wrapper12"));
    EXPECT(!decl::is_alias_placeholder_name(""));
    EXPECT(!decl::is_alias_placeholder_name("_verbose"));
}

ZEST_CASE(deco_option_holds_a_value_like_optional) {
    decl::ScalarOption<int> option;
    EXPECT(!option);
    EXPECT(option.value_or(3) == 3);
    option = 5;
    ASSERT(option.has_value());
    EXPECT(*option == 5);
    EXPECT(option.as_optional() == std::optional<int>(5));
    option.emplace(6);
    EXPECT(option.value() == 6);
    option.reset();
    EXPECT(!option.has_value());
}

ZEST_CASE(deco_option_starts_with_a_default) {
    const decl::ScalarOption<std::string> option = std::string("default");
    ASSERT(option.has_value());
    EXPECT(option->size() == 7U);
}

ZEST_CASE(into_with_context_defaults_to_into) {
    Counting option;
    decl::DecoOptionBase& base = option;
    EXPECT(!base.into(owning(0), decl::IntoContext()));
    EXPECT(option.calls == 1);
}

ZEST_CASE(erased_parse_callback_starts_empty) {
    EXPECT(!decl::ErasedParseCallback{});
}

};  // ZEST_SUITE(deco_facade_decl)

}  // namespace

}  // namespace kota::deco
