"""A test that runs a command on programs of the build, as ctest runs one.

The command runs through bazel/command_test.ts on node, from the source tree
(where node_modules is, for the integration tests) or from the runfiles. Each
`env` value is a runfiles path, $(rootpath) of a label, that the test sees as
an absolute path; the command's arguments may name $(rootpath) too.
"""

def _command_test_impl(ctx):
    targets = ctx.attr.data
    config = ctx.actions.declare_file(ctx.label.name + ".json")
    ctx.actions.write(config, json.encode({
        "command": [ctx.expand_location(arg, targets) for arg in ctx.attr.command],
        "env": {name: ctx.expand_location(value, targets) for name, value in ctx.attr.env.items()},
        "source_tree": ctx.attr.source_tree,
    }))

    windows = ctx.target_platform_has_constraint(ctx.attr._windows[platform_common.ConstraintValueInfo])
    launcher = ctx.actions.declare_file(ctx.label.name + (".bat" if windows else ".sh"))
    args = "%s %s" % (ctx.file._runner.short_path, config.short_path)
    if windows:
        content = "@node %s %%*\r\n" % args.replace("/", "\\")
    else:
        content = "#!/bin/sh\nexec node %s \"$@\"\n" % args
    ctx.actions.write(launcher, content, is_executable = True)

    runfiles = ctx.runfiles(files = [config, ctx.file._runner, ctx.file._module] + ctx.files.data)
    for target in targets:
        runfiles = runfiles.merge(target[DefaultInfo].default_runfiles)
    return [DefaultInfo(executable = launcher, runfiles = runfiles)]

command_test = rule(
    implementation = _command_test_impl,
    test = True,
    attrs = {
        "command": attr.string_list(mandatory = True),
        "data": attr.label_list(allow_files = True),
        "env": attr.string_dict(),
        "source_tree": attr.bool(default = False),
        "_module": attr.label(
            default = "//:MODULE.bazel",
            allow_single_file = True,
        ),
        "_runner": attr.label(
            default = ":command_test.ts",
            allow_single_file = True,
        ),
        "_windows": attr.label(default = "@platforms//os:windows"),
    },
)
