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
#define KANNOTATION_ENTRIES(...)                                                                   \
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
///     KANNOTATION(shape_annotation, tag = "kind", tag_names = {"circle", "rect"});
///     using shape = kotatsu::meta::annotate<shape_annotation>::type<
///         std::variant<circle, rect>>;
#define KANNOTATION(name, ...)                                                                     \
    struct name {                                                                                  \
        constexpr static auto entries = KANNOTATION_ENTRIES(__VA_ARGS__);                          \
    }

#define KANNOTATE_IMPL(tag, ...)                                                                   \
    KANNOTATION(tag, __VA_ARGS__);                                                                 \
    typename ::kotatsu::meta::annotate<tag>::template type

/// Annotates the next field declaration with serde attributes:
///
///     KANNOTATE(rename = "compileCommands",
///               alias = {"compile_commands"},
///               description = "Path to the compilation database.")
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
#define KANNOTATE(...) KANNOTATE_IMPL(KOTATSU_CONCAT(kotatsu_annotation_, __LINE__), __VA_ARGS__)

// Whether the first token of a KANNOTATE_TYPE argument is `struct` or `class`:
// pasted onto KOTATSU_CLASS_KEY_, it makes a macro that puts 1 second, and any
// other first token leaves an identifier that puts 0 second.
#define KOTATSU_CLASS_KEY_struct ~, 1,
#define KOTATSU_CLASS_KEY_class ~, 1,
#define KOTATSU_SECOND_IMPL(first, second, ...) second
#define KOTATSU_SECOND(...) KOTATSU_SECOND_IMPL(__VA_ARGS__)
#define KOTATSU_DECLARES(annotated)                                                                \
    KOTATSU_SECOND(KOTATSU_CONCAT(KOTATSU_CLASS_KEY_, annotated), 0, ~)
#define KOTATSU_WHEN_0(...)
#define KOTATSU_WHEN_1(...) __VA_ARGS__
#define KOTATSU_WHEN(condition) KOTATSU_CONCAT(KOTATSU_WHEN_, condition)

/// Annotates a type for every field of that type: its description, and
/// extension entries; a field's own annotation overrides it entry by entry.
/// Written above the type, `struct X` (or `class X`) declares X in the
/// namespace it is written in, as `struct X;` would:
///
///     KANNOTATE_TYPE(struct Endpoint, description = "A server address");
///     struct Endpoint {
///         std::string host;
///     };
///
/// A type declared already is named plainly, without `::` in front: an enum
/// after its opaque declaration (`enum class Mode : std::uint8_t;`) or its
/// definition. It is written in the type's namespace, where
/// argument-dependent lookup finds it, which a static_assert checks, and next
/// to the type's definition: wherever the type is reflected, its annotation
/// must be seen, as a template specialization must. A class derived from the
/// type does not take the annotation.
#define KANNOTATE_TYPE(annotated, ...)                                                             \
    KOTATSU_WHEN(KOTATSU_DECLARES(annotated))(annotated;) [[maybe_unused]] constexpr auto          \
        kotatsu_type_annotation(::std::type_identity<annotated>) {                                 \
        return KANNOTATION_ENTRIES(__VA_ARGS__);                                                   \
    }                                                                                              \
    static_assert(::kotatsu::meta::detail::has_type_annotation<annotated>,                         \
                  "KANNOTATE_TYPE: write it in the namespace of the type it annotates")
