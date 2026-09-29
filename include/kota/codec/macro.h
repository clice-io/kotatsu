#pragma once

// The kotatsu annotation macros, and nothing else — this header includes
// nothing, by design. Modules cannot export macros, so a downstream consuming
// kotatsu as a module still has to pick these up textually, and anything
// included here would duplicate declarations the module already provides.
//
// The kota::meta entities an expansion refers to only have to be visible where
// the macro is used, e.g. via a codec backend header such as
// "kota/codec/json/json.h".

#define KOTATSU_CONCAT_IMPL(a, b) a##b
#define KOTATSU_CONCAT(a, b) KOTATSU_CONCAT_IMPL(a, b)

// The DSL names are brought in with using-declarations rather than a
// using-directive: block-scope using-declarations shadow outer names, while a
// using-directive would make e.g. `rename` ambiguous with ::rename from
// <stdio.h> at any use site outside namespace kota.

/// Defines a named, reusable annotation tag. The entries decide whether it is
/// a field annotation (rename, skip, ...) or a struct/variant annotation
/// (rename_all, tagged, ...); attach it with kota::meta::annotate:
///
///     KOTATSU_ANNOTATION(shape_annotation, tag = "kind",
///                        tag_names = {"circle", "rect"});
///     using shape = kota::meta::annotate<shape_annotation>::type<
///         std::variant<circle, rect>>;
#define KOTATSU_ANNOTATION(name, ...)                                                              \
    struct name {                                                                                  \
        constexpr static auto spec = [] {                                                          \
            using ::kota::meta::dsl::rename;                                                       \
            using ::kota::meta::dsl::description;                                                  \
            using ::kota::meta::dsl::alias;                                                        \
            using ::kota::meta::dsl::idx;                                                          \
            using ::kota::meta::dsl::skip;                                                         \
            using ::kota::meta::dsl::flatten;                                                      \
            using ::kota::meta::dsl::defaulted;                                                    \
            using ::kota::meta::dsl::skip_if;                                                      \
            using ::kota::meta::dsl::as;                                                           \
            using ::kota::meta::dsl::with;                                                         \
            using ::kota::meta::dsl::enum_string;                                                  \
            using ::kota::meta::dsl::rename_all;                                                   \
            using ::kota::meta::dsl::deny_unknown_fields;                                          \
            using ::kota::meta::dsl::tagged;                                                       \
            using ::kota::meta::dsl::tag;                                                          \
            using ::kota::meta::dsl::content;                                                      \
            using ::kota::meta::dsl::tag_names;                                                    \
            using ::kota::meta::dsl::type;                                                         \
            using ::kota::meta::skip_when;                                                         \
            using ::kota::naming::Casing;                                                          \
            return ::kota::meta::make_annotation(__VA_ARGS__);                                     \
        }();                                                                                       \
    }

#define KOTATSU_ANNOTATE_IMPL(tag, ...)                                                            \
    KOTATSU_ANNOTATION(tag, __VA_ARGS__);                                                          \
    typename ::kota::meta::annotate<tag>::template type

/// Annotates the next field declaration with serde attributes:
///
///     KOTATSU_ANNOTATE(rename = "compileCommands",
///                      alias = {"compile_commands"},
///                      description = "Path to the compilation database.")
///     <std::string> compile_commands;
///
/// The attribute strings live in one constexpr spec object per annotation;
/// only the short generated tag type appears in template arguments, so
/// mangled names stay small no matter how long the strings are. Type-valued
/// attributes ride along as `as = type<Target>`, `with = type<Adapter>`,
/// `skip_if = type<Pred>`, `enum_string = type<Policy>`, or as explicit
/// trailing template arguments after the field type.
#define KOTATSU_ANNOTATE(...)                                                                      \
    KOTATSU_ANNOTATE_IMPL(KOTATSU_CONCAT(kotatsu_annotation_, __LINE__), __VA_ARGS__)
