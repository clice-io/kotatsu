#include "deco/harness/text.h"
#include "kota/deco/deco.h"
#include "kota/zest/zest.h"

namespace kota::deco {

namespace {

ZEST_SUITE(deco_facade_config) {

ZEST_CASE(merge_takes_only_what_is_overridden) {
    config::Config base;
    base.enum_meta_var.max_items = 3;
    base.render.compatible.usage.help_column = 10;

    const auto unchanged = config::merge(base, {});
    ZEXPECT(unchanged.enum_meta_var.max_items == 3U);
    ZEXPECT(unchanged.render.compatible.usage.help_column == 10U);

    config::EnumMetaVarConfig enum_meta_var;
    enum_meta_var.separator = ",";
    const auto merged = config::merge(base, {.enum_meta_var = enum_meta_var, .render = {}});
    ZEXPECT(merged.enum_meta_var.separator == ",");
    ZEXPECT(merged.enum_meta_var.max_items == 6U);
    ZEXPECT(merged.render.compatible.usage.help_column == 10U);
}

ZEST_CASE(set_replaces_the_global_config) {
    test::ScopedDecoConfig restore;
    auto updated = config::get();
    updated.enum_meta_var.enabled = false;
    config::set(updated);
    ZEXPECT(!config::get().enum_meta_var.enabled);
}

ZEST_CASE(set_render_keeps_the_enum_config) {
    test::ScopedDecoConfig restore;
    config::EnumMetaVarConfig enum_meta_var;
    enum_meta_var.max_items = 2;
    config::set_enum_meta_var(enum_meta_var);

    config::BuiltInRenderConfig render;
    render.compatible.usage.options_heading = "Flags:";
    config::set_render(render);

    ZEXPECT(config::get().enum_meta_var.max_items == 2U);
    ZEXPECT(config::get().render.compatible.usage.options_heading == "Flags:");
}

ZEST_CASE(set_rebuilds_the_renderer_it_makes) {
    test::ScopedDecoConfig restore;
    test::ScopedDefaultRenderer restore_renderer;
    cli::text::clear_default_renderer();

    auto updated = config::get();
    updated.render.compatible.usage.options_heading = "Flags:";
    config::set(updated);
    ZEXPECT(cli::text::default_renderer().style.usage.options_heading == "Flags:");
}

ZEST_CASE(restoring_the_config_restores_its_renderer) {
    test::ScopedDefaultRenderer restore_renderer;
    cli::text::clear_default_renderer();
    {
        test::ScopedDecoConfig restore;
        config::BuiltInRenderConfig render;
        render.compatible.usage.options_heading = "Flags:";
        config::set_render(render);
    }
    ZEXPECT(cli::text::default_renderer().style.usage.options_heading == "Options:");
}

ZEST_CASE(renderer_configs_name_their_headings) {
    const config::CompatibleRendererConfig compatible;
    ZEXPECT(compatible.usage.options_heading == "Options:");
    ZEXPECT(compatible.subcommand.heading == "Subcommands:");

    const config::ModernRendererConfig modern;
    ZEXPECT(modern.usage.options_heading == "Options");
    ZEXPECT(modern.subcommand.heading == "Commands");
}

};  // ZEST_SUITE(deco_facade_config)

}  // namespace

}  // namespace kota::deco
