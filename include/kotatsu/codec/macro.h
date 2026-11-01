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

/// Names a bundle of entries, reusable in any annotation as one more entry there, which the
/// entries written after it override. Written at namespace scope:
///
///     KANNOTATION(help, description = "display this help", cli::names = {"-h", "--help"});
///
///     KANNOTATE(&help, help, description = "show usage")
///     bool help = false;
#define KANNOTATION(name, ...)                                                                     \
    [[maybe_unused]] constexpr inline auto name = KANNOTATION_ENTRIES(__VA_ARGS__)

/// Annotates a field of the class it is written in, above the field, which
/// `&field` names as a member pointer names it:
///
///     KANNOTATE(&compile_commands,
///               rename = "compileCommands",
///               alias = {"compile_commands"},
///               description = "Path to the compilation database.")
///     std::string compile_commands;
///
/// The field keeps the type it is declared with. The annotation is filed under
/// the field's name, by a consteval function that is neither emitted nor
/// mangled; a name no member of the class has fails to compile, and so does a
/// second annotation of one field. Type-valued attributes ride along as
/// `as = type<Target>`, `with = type<Adapter>`, `skip_if = type<Pred>` and
/// `enum_string = type<Policy>`. A field whose type KANNOTATE_TYPE annotates
/// keeps that annotation: an entry given here overrides the type's, and the
/// rest of both apply.
#define KANNOTATE(member, ...)                                                                    \
    [[maybe_unused]] static consteval auto __kotatsu__annotation(                                  \
        ::kotatsu::meta::detail::member_key_t<#member>) {                                          \
        [[maybe_unused]] ::std::type_identity<decltype(::kotatsu::meta::detail::member_probe {     \
        } member)> annotated;                                                                      \
        return KANNOTATION_ENTRIES(__VA_ARGS__);                                                   \
    }

/// Annotates a field from outside its class, for a class whose definition
/// cannot be changed. `&T::field` names the field, its class and all. Written
/// in the namespace of the class, where argument-dependent lookup finds it,
/// with the class complete:
///
///     KANNOTATE_FIELD(&third_party::Point::y, rename = "Y");
///
/// A field annotated both here and in its class fails to compile.
#define KANNOTATE_FIELD(member, ...)                                                               \
    [[maybe_unused]] consteval auto __kotatsu__field_annotation(                                   \
        ::std::type_identity<::kotatsu::class_type_t<decltype(member)>>,                           \
        ::kotatsu::meta::detail::member_key_t<#member>) {                                          \
        static_assert(::std::is_member_object_pointer_v<decltype(member)>,                         \
                      "KANNOTATE_FIELD names a data member: &T::field");                           \
        return KANNOTATION_ENTRIES(__VA_ARGS__);                                                   \
    }                                                                                              \
    static_assert(true)

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

/// Annotates a type, wherever a value of it is: a document's root, an element,
/// a field. It takes the type's description, when a field of it is left out
/// (skip_if, defaulted), the struct and variant entries (rename_all,
/// deny_unknown_fields, tag, tag_names, ...) and extension entries; a field's
/// own annotation overrides them entry by entry. A variant that a document
/// holds other than as a field takes its tagging here, as a class derived from
/// the std::variant:
///
///     KANNOTATE_TYPE(struct Shape, tag = "kind", tag_names = {"circle", "rect"});
///     struct Shape : std::variant<Circle, Rect> {
///         using variant::variant;
///     };
///
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
        __kotatsu__type_annotation(::std::type_identity<annotated>) {                              \
        return KANNOTATION_ENTRIES(__VA_ARGS__);                                                   \
    }                                                                                              \
    static_assert(::kotatsu::meta::detail::has_type_annotation<annotated>,                         \
                  "KANNOTATE_TYPE: write it in the namespace of the type it annotates")
