#pragma once

// The kotatsu annotation macros, and nothing else — this header includes
// nothing, by design. Modules cannot export macros, so a downstream consuming
// kotatsu as a module still has to pick these up textually, and anything
// included here would duplicate declarations the module already provides.
//
// The kotatsu::meta entities an expansion refers to only have to be visible
// where the macro is used, e.g. via a codec backend header such as
// "kotatsu/codec/json/json.h".

#define KOTATSU_CONCAT_IMPL(a, b) a##b
#define KOTATSU_CONCAT(a, b) KOTATSU_CONCAT_IMPL(a, b)

// The DSL names are brought in with using-declarations rather than a
// using-directive: block-scope using-declarations shadow outer names, while a
// using-directive would make e.g. `rename` ambiguous with ::rename from
// <stdio.h> at any use site outside namespace kotatsu.

/// The entries of one annotation, checked, with the DSL names in scope: what
/// each macro below keeps.
#define KOTATSU_ANNOTATION_ENTRIES(...)                                                            \
    [] {                                                                                           \
        using ::kotatsu::meta::dsl::rename;                                                        \
        using ::kotatsu::meta::dsl::description;                                                   \
        using ::kotatsu::meta::dsl::alias;                                                         \
        using ::kotatsu::meta::dsl::idx;                                                           \
        using ::kotatsu::meta::dsl::skip;                                                          \
        using ::kotatsu::meta::dsl::flatten;                                                       \
        using ::kotatsu::meta::dsl::defaulted;                                                     \
        using ::kotatsu::meta::dsl::skip_if;                                                       \
        using ::kotatsu::meta::dsl::as;                                                            \
        using ::kotatsu::meta::dsl::with;                                                          \
        using ::kotatsu::meta::dsl::enum_string;                                                   \
        using ::kotatsu::meta::dsl::schema_default;                                                \
        using ::kotatsu::meta::dsl::minimum;                                                       \
        using ::kotatsu::meta::dsl::maximum;                                                       \
        using ::kotatsu::meta::dsl::choices;                                                       \
        using ::kotatsu::meta::dsl::rename_all;                                                    \
        using ::kotatsu::meta::dsl::deny_unknown_fields;                                           \
        using ::kotatsu::meta::dsl::defaulted_fields;                                              \
        using ::kotatsu::meta::dsl::tagged;                                                        \
        using ::kotatsu::meta::dsl::tag;                                                           \
        using ::kotatsu::meta::dsl::content;                                                       \
        using ::kotatsu::meta::dsl::tag_names;                                                     \
        using ::kotatsu::meta::dsl::type;                                                          \
        using ::kotatsu::meta::skip_when;                                                          \
        using ::kotatsu::naming::Casing;                                                           \
        return ::kotatsu::meta::make_annotation(__VA_ARGS__);                                      \
    }()

/// Defines a named, reusable annotation tag. Its entries may be field entries
/// (rename, skip, ...), struct or variant entries (rename_all, tagged, ...) or
/// both; attach it with kotatsu::meta::annotate:
///
///     KANNOTATION(shape_annotation, tag = "kind",
///                        tag_names = {"circle", "rect"});
///     using shape = kotatsu::meta::annotate<shape_annotation>::type<
///         std::variant<circle, rect>>;
#define KANNOTATION(name, ...)                                                                     \
    struct name {                                                                                  \
        constexpr static auto entries = KOTATSU_ANNOTATION_ENTRIES(__VA_ARGS__);                   \
    }

#define KOTATSU_ANNOTATE_IMPL(tag, ...)                                                            \
    KANNOTATION(tag, __VA_ARGS__);                                                                 \
    typename ::kotatsu::meta::annotate<tag>::template type

/// Annotates the next field declaration with serde attributes:
///
///     KANNOTATE(rename = "compileCommands",
///                      alias = {"compile_commands"},
///                      description = "Path to the compilation database.")
///     <std::string> compile_commands;
///
/// The entries live in one constexpr object per annotation; only the short
/// generated tag type appears in template arguments, so mangled names stay
/// small no matter how long the strings are. Type-valued attributes ride along
/// as `as = type<Target>`, `with = type<Adapter>`, `skip_if = type<Pred>`,
/// `enum_string = type<Policy>`, or as explicit trailing template arguments
/// after the field type. Annotating a type that is an annotation already
/// (cli::Help, a meta::annotate alias) adds to it: an entry given here
/// overrides the inner one, and the rest of both apply.
#define KANNOTATE(...)                                                                             \
    KOTATSU_ANNOTATE_IMPL(KOTATSU_CONCAT(kotatsu_annotation_, __LINE__), __VA_ARGS__)

/// Annotates a type for every field of that type: its description, and
/// extension entries; a field's own annotation overrides it entry by entry.
/// Written above the type, `struct X` (or `class X`) declares it there, as a
/// forward declaration would:
///
///     KANNOTATE_TYPE(struct Endpoint, description = "A server address");
///     struct Endpoint {
///         std::string host;
///     };
///
/// A type declared already is named plainly: an enum after its opaque
/// declaration (`enum class Mode : std::uint8_t;`) or its definition, a class
/// of another library inside that library's namespace. It is written in the
/// type's namespace, where argument-dependent lookup finds it, and before the
/// type is first reflected. A class derived from the type does not take the
/// annotation.
#define KANNOTATE_TYPE(annotated, ...)                                                             \
    [[maybe_unused]] constexpr auto kotatsu_type_annotation(::std::type_identity<annotated>) {     \
        return KOTATSU_ANNOTATION_ENTRIES(__VA_ARGS__);                                            \
    }                                                                                              \
    static_assert(true)
