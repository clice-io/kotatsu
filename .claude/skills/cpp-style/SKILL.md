---
name: cpp-style
description: kotatsu C++ coding conventions — redundancy elimination (the rule we care most about), file organization, macro.h isolation, dual-mode exception policy, template deduction and type-trait rules, naming, modern C++ preferences. Read BEFORE writing or modifying any C++ code.
---

# kotatsu C++ Coding Style

## Redundancy Is a Defect

This is the convention we care most about. Redundant code is not a style
nit — it actively misleads: every guard implies the guarded state can
occur, every branch implies it can be taken, every parameter implies a
caller needs it. When that implication is false, the reader wastes time
defending against ghosts.

- **Every branch must be reachable.** Before adding a guard or fallback,
  prove the state can actually occur — construct the input that hits it.
  If you can't, don't write it.
- **No speculative generality.** No parameters, options, hooks, or
  abstraction layers for hypothetical future callers. Add them when the
  second real caller arrives.
- **One way to do each thing.** Don't leave an old path alive next to its
  replacement "just in case" — migrate all callers and delete it in the
  same change.
- **Re-read after every change.** Edits leave residue: conditions that
  became constant, variables read once, branches that now collapse,
  `else` after `return`, a helper with one remaining caller. Fold them
  before you're done — simplification that removes a concept beats one
  that merely shortens lines.
- **Delete, don't comment out.** Git history is the archive.

## Files & Organization

- Headers are `.h` with `#pragma once` — never include guards. Public
  headers live under `include/kota/<module>/`.
- Source extensions are mixed across modules (`.cpp` in some, `.cc` in
  others) — match the module you are editing.
- File names are `snake_case`.
- File-local helpers: a single one is `static`; a cluster of them goes in
  one anonymous namespace.

## Module Boundaries & macro.h

- Libraries with user-facing declaration macros keep them in a
  dedicated `macro.h` (today: `deco`, `zest`) instead of exporting them
  from regular headers. **Iron rule: such a `macro.h` contains zero
  `#include` directives — not even standard headers.** It must stay a
  pure macro sheet a consumer can include from anywhere with no
  transitive cost.
- This rule covers only those dedicated macro sheets. Configuration and
  implementation macros that ordinary headers legitimately define —
  e.g. `KOTA_THROW` and the platform feature macros in
  `kota/support/config.h` — stay where they are.

## Data & Types

- `struct` by default, even for types with methods. `class` only when
  there is a real invariant that private access protects.
- `enum class` with an explicit underlying type (e.g. `: std::uint8_t`);
  document each enumerator with `///` when its meaning is not obvious.
- Prefer designated initializers (`{.field = value}`) for aggregate
  construction.
- West const: `const T&`, never `T const&`.

## Errors, Exceptions & Defense

- kotatsu is a library that builds both with and without exceptions
  (`KOTA_ENABLE_EXCEPTIONS`). Library code never writes bare
  `throw`/`try`/`catch` — use the `KOTA_THROW` / `KOTA_TRY` /
  `KOTA_CATCH_ALL` / `KOTA_RETHROW` macros from `kota/support/config.h`,
  which degrade to `std::abort()` in the no-exceptions build.
- Fallible synchronous operations return `std::expected<T, E>`; async
  code uses kotatsu's outcome types.
- `assert` pins preconditions; impossible branches end in
  `std::unreachable()`.
- Defend only against states that can occur (the redundancy rule) —
  graceful degradation is not scattered null checks. The ASan/TSan
  build trees are the safety net.

## Error Handling (control flow)

- **Prefer `if` with init-statements to tightly scope error variables**, but avoid them when they compromise code readability or flatten control flow.
- **Omit redundant conditions:** If the error type provides an `operator bool` or evaluates implicitly (e.g., standard error codes, custom error wrappers), omit the redundant condition check.
- **Avoid forced `else` branches:** If scoping the variable inside the `if` requires you to introduce an `else` block for the success path (especially when returning early on error), declare the variable in the local scope instead to keep the control flow flat.

```cpp
// Good: Omit redundant condition when the type has operator bool
if (auto err = foo()) {
    /* handle error */
}

// Bad: Redundant condition check
if (auto err = foo(); err) {
    /* handle error */
}

// Good: Use init-statement when a custom condition is required,
// AND the variable isn't needed outside the if-statement
if (auto result = foo(); !result.has_value()) {
    /* handle error */
}

// Bad: Using init-statement forces an 'else' block because 'result'
// goes out of scope, leading to nested/redundant code.
if (auto result = get_data(); !result.has_value()) {
    return result.error();
} else {
    process(result.value()); // Success path is forced into a nested block
}

// Good: Declare as a regular local variable to allow early exit
// and keep the success path un-nested (flat control flow).
auto result = get_data();
if (!result.has_value()) {
    return result.error();
}
process(result.value());
```

## Concurrency & Async

- Async code is coroutine-style (`kota::task`, `co_await`) — an
  operation that produces a single asynchronous result returns an
  awaitable, never takes a completion callback; completion callbacks
  appear only at the libuv boundary inside the implementation.
- Event handlers and customization hooks are a different animal:
  public callback registration APIs such as `Peer::on_request`,
  `Peer::on_notification`, or `set_logger` are legitimate extension
  points — do not replace them with coroutine APIs.

## Naming Conventions

- **Variables, member fields, function names**: `snake_case`. Class member fields do NOT use any special suffix/prefix (no trailing `_`, no `m_` prefix).
- **Class names, template parameter names, enum names**: `PascalCase`. Exception: some class names also use `snake_case` — follow the existing style in the module.
- **Enum values**: `PascalCase`.
- Doc comments on declarations use `///`.

## Template & Type Traits

- Do NOT blindly add `std::remove_cvref_t` on every template parameter. Understand C++ template argument deduction rules:
  - `template<typename T> void f(T x)` — `T` is always deduced as a non-reference, non-cv-qualified type. No need for `remove_cvref_t`.
  - `template<typename T> void f(T& x)` — `T` is deduced as the referred-to type (possibly cv-qualified, but never a reference). No need for `remove_cvref_t` to strip references.
  - `template<typename T> void f(const T& x)` — `T` is deduced as a non-const, non-reference type. No need for `remove_cvref_t`.
  - `template<typename T> void f(T&& x)` — **forwarding reference**: `T` CAN be deduced as an lvalue reference (e.g., `int&`). This is the ONLY case where `std::remove_cvref_t<T>` is needed to get the bare type.
  - The same reasoning, not a blanket rule, applies to other deduction contexts: strip cv/ref only where the context can actually produce a cv/ref-qualified type. Plain `auto` return deduction and by-value CTAD guides never yield references, but `decltype(auto)` and explicit deduction guides can carry references and cv-qualifiers.
  - Separately from deduction: explicitly supplied template arguments bypass deduction entirely, so `T` is whatever the caller wrote — `f<int&>(x)` makes `T = int&` even for a by-value `f(T x)`. Only defend with `remove_cvref_t` if the API actually invites explicit reference arguments.

## Type Traits & Concepts (C++20/23)

- This project targets C++20/23. Use variable templates directly for type traits — do NOT use the old pattern of wrapping a class template static member in a variable template. Prefer:

  ```cpp
  // Good: directly specialize a variable template
  template<typename T>
  inline constexpr bool is_my_type_v = false;

  template<>
  inline constexpr bool is_my_type_v<MyType> = true;
  ```

  ```cpp
  // Bad: unnecessary class template wrapper
  template<typename T>
  struct is_my_type : std::false_type {};

  template<>
  struct is_my_type<MyType> : std::true_type {};

  template<typename T>
  inline constexpr bool is_my_type_v = is_my_type<T>::value;
  ```

- When defining a concept that checks a type trait, do NOT add `std::remove_cvref_t` unless you specifically intend the concept to see through references/cv-qualifiers. If the concept is meant for a bare type, just use `T` directly — the caller is responsible for passing the right type.

  ```cpp
  // Good
  template<typename T>
  concept MyTrait = is_my_type_v<T>;

  // Bad: unnecessary remove_cvref_t
  template<typename T>
  concept MyTrait = is_my_type_v<std::remove_cvref_t<T>>;
  ```

## String Literals

- Prefer C++11 raw string literals `R"(...)"` over escaped strings. Avoid `\"`, `\\`, `\n` in string literals when a raw literal is cleaner.

## Style

- Prefer `[[maybe_unused]]` over `(void)` for intentionally unused variables or parameters.
- No banner comments (a section title between `// ----` rules). A file too long to read without them splits by aspect; a comment says what the names do not.

## Modern C++ Usage

- Use C++20/23 APIs whenever possible.
- Library code does not print — it returns. Do NOT use `<iostream>` facilities (`std::cout`, `std::cin`, `std::cerr`, etc.) or C-style I/O (`printf`, `fprintf`, etc.) in library components. Exempt are components whose purpose is producing output: CLI presentation (e.g. `deco` usage printing), test reporting in `zest`, and fatal-diagnostic paths that must emit before aborting.
- Prefer `std::format` for string building.
- Prefer `std::ranges` / `std::views` APIs over raw loops and traditional `<algorithm>` calls.

## Parameter Passing Preferences

- For string parameters, prefer `std::string_view` > `const std::string&`.
- For array/span parameters, prefer `std::span` > `const std::vector&`.
