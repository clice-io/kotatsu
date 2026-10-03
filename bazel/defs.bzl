"""Rules for kotatsu's own targets."""

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# The flags kotatsu's own sources compile with; they are not passed on to
# dependents. -Werror is the development build's (.bazelrc), so that a newer
# compiler's warnings do not fail a consumer's build.
KOTA_COPTS = select({
    Label("//bazel:no_exceptions"): ["-fno-exceptions"],
    "//conditions:default": ["-fexceptions"],
}) + select({
    Label("//bazel:no_rtti"): ["-fno-rtti"],
    "//conditions:default": ["-frtti"],
}) + [
    "-Wall",
    "-Wextra",
]

def kota_library(name, deps = [], **kwargs):
    """A library of include/kota/<module>, included as "kota/<module>/..."."""
    cc_library(
        name = name,
        copts = KOTA_COPTS,
        # No shared library of its own: one on Windows would have to resolve
        # every symbol of its dependencies.
        linkstatic = True,
        strip_include_prefix = "/include",
        visibility = ["//visibility:public"],
        deps = deps + [Label("//bazel:options")],
        **kwargs
    )

def kota_binary(name, copts = [], **kwargs):
    cc_binary(
        name = name,
        copts = KOTA_COPTS + copts,
        **kwargs
    )

def kota_test(name, copts = [], linkopts = [], **kwargs):
    cc_test(
        name = name,
        copts = KOTA_COPTS + copts,
        # A test links its libraries dynamically by default, each shared
        # object with a C++ runtime of its own (xclang links libc++
        # statically), so memory one allocates another frees.
        linkstatic = True,
        linkopts = linkopts,
        **kwargs
    )
