// Generate include/kota/ipc/lsp/protocol.h from the pinned LSP metaModel
// (metamodel.ts), and the table of its types the integration tests decode and
// encode each of (tests/ipc/lsp/harness/protocol_types.inc). `--check`
// regenerates both in memory and fails when a committed one differs.
//
// Mapping from the metaModel's TypeScript constructs to C++ (vocabulary in
// kota/ipc/lsp/ts.h):
//
// - structures become aggregates with every inherited property (`extends` and
//   `mixins`) inlined, so members are reached and designated directly; a
//   property a structure redeclares narrows the inherited one in place.
// - `LSPAny` / `LSPObject` / `LSPArray` alias the codec's dynamic value types.
// - `T | null` is `nullable<T>` (over a `variant` for several alternatives),
//   which a structure requires present; an optional property is `optional<T>`,
//   or `optional_nullable<T>` when null is among its values, so that null does
//   not read as absent; an optional boolean is `optional_bool` (absent reads as
//   false) but for the few TRI_STATE_BOOLEANS, `optional<boolean>`; and an
//   optional property holding its own structure is `optional_ptr<T>`.
// - an untagged variant lists a structure before the ones it derives from, so
//   that a value reads as the most derived alternative it fills.
// - a string literal type is `Literal<"...">`, which decodes only its own text
//   so untagged variants tell their alternatives apart by it.
// - enumerations keep unknown values: integer ones are `enum class` over the
//   full integer, string ones wrap `std::string` with named constants.
// - every request / notification gets a `RequestTraits` / `NotificationTraits`
//   specialization keyed by its params type; methods without params get an
//   empty params structure so the key stays unique.

import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseArgs } from "node:util";

import {
  COMMIT,
  Schema,
  SchemaError,
  TRI_STATE_BOOLEANS,
  VERSION,
  isBase,
  loadMetaModel,
  sortedBy,
  type BaseTypes,
  type Enumeration,
  type EnumerationEntry,
  type Property,
  type Structure,
  type Type,
  type TypeAlias,
} from "./metamodel.ts";

const ROOT = join(import.meta.dirname, "../..");
const HEADER = "include/kota/ipc/lsp/protocol.h";
const TABLE = "tests/ipc/lsp/harness/protocol_types.inc";

// Named after the metaModel; the aliases live in kota/ipc/protocol.h and ts.h.
const BASE_TYPES = new Set<BaseTypes>([
  "boolean",
  "integer",
  "uinteger",
  "decimal",
  "string",
  "null",
  "URI",
  "DocumentUri",
]);
const DYNAMIC_TYPES = new Map([
  ["LSPAny", "kota::codec::dyn::Value"],
  ["LSPObject", "kota::codec::dyn::Object"],
  ["LSPArray", "kota::codec::dyn::Array"],
]);

// An optional boolean becomes `optional_bool`, which reads absence as false;
// a property documented to default to true would silently flip, and one whose
// documentation gives undefined a meaning of its own has to be tri-state.
const DEFAULT_TRUE = /defaults? (?:to|is) true|true by default/i;
const UNDEFINED = /undefined/i;

// prettier-ignore
const CPP_KEYWORDS = new Set([
  "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool",
  "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class",
  "compl", "concept", "const", "consteval", "constexpr", "constinit", "const_cast",
  "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete",
  "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
  "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
  "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator",
  "or", "or_eq", "private", "protected", "public", "register", "reinterpret_cast",
  "requires", "return", "short", "signed", "sizeof", "static", "static_assert",
  "static_cast", "struct", "switch", "template", "this", "thread_local", "throw",
  "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
  "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
]);

function identifier(name: string): string {
  if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(name) || CPP_KEYWORDS.has(name)) {
    throw new SchemaError(`\`${name}\` is not usable as a C++ identifier`);
  }
  return name;
}

function capitalized(text: string): string {
  return text.charAt(0).toUpperCase() + text.slice(1);
}

/**
 * snake_case member whose lower_camel rename (ipc::lsp_config) restores the
 * property name.
 */
function memberName(propertyName: string): string {
  const snake = identifier(
    propertyName
      .replace(/(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])/g, "_")
      .toLowerCase(),
  );
  const [head, ...rest] = snake.split("_");
  if (head + rest.map(capitalized).join("") !== propertyName) {
    throw new SchemaError(
      `property \`${propertyName}\` does not survive the rename`,
    );
  }
  return snake;
}

function constantName(valueName: string): string {
  return identifier(capitalized(valueName));
}

/** A C++ string literal: JSON's string escapes are C++ escapes too. */
function quoted(text: string): string {
  return JSON.stringify(text);
}

function docComment(
  item: { name: string; documentation?: string },
  indent = "",
): string[] {
  // The documentation already carries its @since / @deprecated tags.
  const lines = (item.documentation ?? "").split(/\r\n|\r|\n/);
  // A final line break ends the last line rather than starting another.
  if (lines.at(-1) === "") {
    lines.pop();
  }
  const comment = lines.map((line) => `${indent}/// ${line}`.trimEnd());
  if (comment.some((line) => line.endsWith("\\"))) {
    // A trailing backslash would splice the next line into the comment.
    throw new SchemaError(
      `documentation of \`${item.name}\` ends a line in \`\\\``,
    );
  }
  return comment;
}

/** Blocks joined by blank lines. */
function separated(blocks: string[][]): string[] {
  return blocks.flatMap((block, i) => (i === 0 ? block : ["", ...block]));
}

class Generator extends Schema {
  // The TRI_STATE_BOOLEANS met, each of which must be.
  readonly #triState = new Set<string>();

  render(t: Type): string {
    switch (t.kind) {
      case "base":
        if (!BASE_TYPES.has(t.name)) {
          throw new SchemaError(`unknown base type \`${t.name}\``);
        }
        return t.name;
      case "reference":
        if (
          t.name.startsWith("_") ||
          !(
            this.structures.has(t.name) ||
            this.enumerations.has(t.name) ||
            this.aliases.has(t.name)
          )
        ) {
          throw new SchemaError(`unusable reference \`${t.name}\``);
        }
        return t.name;
      case "array":
        return `std::vector<${this.render(t.element)}>`;
      case "map":
        return `std::map<${this.render(t.key)}, ${this.render(t.value)}>`;
      case "tuple":
        return `std::tuple<${t.items.map((item) => this.render(item)).join(", ")}>`;
      case "or": {
        const alternatives = this.derivedFirst(
          t.items.filter((item) => !isBase(item, "null")),
        ).map((item) => this.render(item));
        const rendered =
          alternatives.length === 1
            ? alternatives[0]
            : `variant<${alternatives.join(", ")}>`;
        return t.items.some((item) => isBase(item, "null"))
          ? `nullable<${rendered}>`
          : rendered;
      }
      case "literal":
        if (t.value.properties.length > 0) {
          throw new SchemaError(
            "object literal types with properties are unsupported",
          );
        }
        return "EmptyObject";
      case "stringLiteral":
        return `Literal<${quoted(t.value)}>`;
      default:
        throw new SchemaError(`unsupported type kind \`${t.kind}\``);
    }
  }

  /** Structures and aliases a rendered type names. */
  dependencies(t: Type): string[] {
    switch (t.kind) {
      case "reference":
        return this.enumerations.has(t.name) ? [] : [t.name];
      case "array":
        return this.dependencies(t.element);
      case "map":
        return [...this.dependencies(t.key), ...this.dependencies(t.value)];
      case "or":
      case "tuple":
        return t.items.flatMap((item) => this.dependencies(item));
      default:
        return [];
    }
  }

  declaration(owner: string, prop: Property): string {
    const name = memberName(prop.name);
    const t = prop.type;
    if (t.kind === "reference" && t.name === owner) {
      // The structure contains itself (`SelectionRange.parent`); a by-value
      // member would make it infinite.
      if (!prop.optional) {
        throw new SchemaError(`${owner}.${prop.name} requires itself`);
      }
      return `optional_ptr<${owner}> ${name} = {};`;
    }
    if (prop.optional && isBase(t, "boolean")) {
      if (TRI_STATE_BOOLEANS.has(`${owner}.${prop.name}`)) {
        this.#triState.add(`${owner}.${prop.name}`);
        return `optional<boolean> ${name} = {};`;
      }
      if (DEFAULT_TRUE.test(prop.documentation ?? "")) {
        throw new SchemaError(`${owner}.${prop.name} defaults to true`);
      }
      if (UNDEFINED.test(prop.documentation ?? "")) {
        throw new SchemaError(
          `${owner}.${prop.name} gives undefined a meaning; make it tri-state`,
        );
      }
      return `optional_bool ${name} = {};`;
    }
    if (prop.optional && this.admitsNull(t)) {
      return `optional_nullable<${this.render(t)}> ${name} = {};`;
    }
    if (prop.optional) {
      return `optional<${this.render(t)}> ${name} = {};`;
    }
    if (t.kind === "stringLiteral") {
      return `${this.render(t)} ${name} = {};`;
    }
    return `${this.render(t)} ${name};`;
  }

  emitStructure(structure: Structure): string[] {
    const { name } = structure;
    const head = docComment(structure);
    const members = this.properties(name).map((prop) => [
      ...docComment(prop, "    "),
      `    ${this.declaration(name, prop)}`,
    ]);
    const struct = `struct ${identifier(name)}`;
    if (members.length === 0) {
      return [...head, `${struct} {};`];
    }
    return [...head, `${struct} {`, ...separated(members), "};"];
  }

  emitAlias(alias: TypeAlias): string[] {
    const target = DYNAMIC_TYPES.get(alias.name) ?? this.render(alias.type);
    return [
      ...docComment(alias),
      `using ${identifier(alias.name)} = ${target};`,
    ];
  }

  emitEnumeration(enumeration: Enumeration): string[] {
    const name = identifier(enumeration.name);
    const base = enumeration.type.name;
    const spaced = enumeration.values.some(
      (value) => value.documentation !== undefined,
    );
    const entries = (
      declare: (constant: string, value: EnumerationEntry) => string,
    ): string[] => {
      const blocks = enumeration.values.map((value) => [
        ...docComment(value, "    "),
        `    ${declare(constantName(value.name), value)}`,
      ]);
      return spaced ? separated(blocks) : blocks.flat();
    };

    const head = docComment(enumeration);
    if (base === "integer" || base === "uinteger") {
      // Declared over the full integer so values newer peers send decode
      // instead of failing the whole message.
      return [
        ...head,
        `enum class ${name} : ${base} {`,
        ...entries((constant, { name: entry, value }) => {
          if (!Number.isSafeInteger(value)) {
            throw new SchemaError(`${name}.${entry} is not an integer`);
          }
          return `${constant} = ${value},`;
        }),
        "};",
      ];
    }
    if (base !== "string") {
      throw new SchemaError(`enumeration \`${name}\` over \`${base}\``);
    }
    return [
      ...head,
      `struct ${name} : std::string {`,
      "    using std::string::string;",
      "    using std::string::operator=;",
      "",
      `    ${name}() = default;`,
      "",
      // Implicit, so the constants below initialize the type directly
      // (std::string's string_view constructor is explicit).
      `    ${name}(std::string_view value) : std::string(value) {}`,
      "",
      ...entries((constant, { name: entry, value }) => {
        if (typeof value !== "string") {
          throw new SchemaError(`${name}.${entry} is not a string`);
        }
        return `constexpr static std::string_view ${constant} = ${quoted(value)};`;
      }),
      "};",
    ];
  }

  emitTraits(): string[] {
    const blocks: string[][] = [];
    for (const [kind, messages] of [
      ["RequestTraits", this.requests],
      ["NotificationTraits", this.notifications],
    ] as const) {
      const keys = messages.map((message) => this.render(message.params));
      if (new Set(keys).size !== keys.length) {
        throw new SchemaError(`two methods share a ${kind} params type`);
      }
      blocks.push(
        ...messages.map((message, i) => [
          "template <>",
          `struct ${kind}<${keys[i]}> {`,
          ...(message.result === undefined
            ? []
            : [`    using Result = ${this.render(message.result)};`]),
          `    constexpr static std::string_view method = ${quoted(message.method)};`,
          "};",
        ]),
      );
    }
    return separated(blocks);
  }

  /** Aliases and structures, each after every other one it names. */
  orderedDeclarations(): string[] {
    const graph = new Map<string, Set<string>>();
    for (const [name, alias] of this.aliases) {
      graph.set(
        name,
        new Set(DYNAMIC_TYPES.has(name) ? [] : this.dependencies(alias.type)),
      );
    }
    // Structures prefixed with `_` exist only to be inherited
    // (`_InitializeParams`); their properties are inlined into the children
    // and they are not emitted.
    for (const name of this.structures.keys()) {
      if (name.startsWith("_")) {
        continue;
      }
      const dependencies = new Set(
        this.properties(name).flatMap((prop) => this.dependencies(prop.type)),
      );
      // A structure may name itself: inside std::vector, which admits the
      // incomplete type, or through `optional_ptr` (see declaration()).
      dependencies.delete(name);
      graph.set(name, dependencies);
    }

    // Emitted in rounds: each round is every declaration whose dependencies
    // earlier rounds emitted, sorted by name so the order is stable. A
    // dependency not declared here fails later, when its type is rendered.
    const order: string[] = [];
    const pending = new Map(graph);
    while (pending.size > 0) {
      const ready = [...pending]
        .filter(([, dependencies]) =>
          [...dependencies].every((dependency) => !pending.has(dependency)),
        )
        .map(([name]) => name)
        .sort();
      if (ready.length === 0) {
        throw new SchemaError(
          `declarations name each other in a cycle: ${[...pending.keys()].sort().join(", ")}`,
        );
      }
      for (const name of ready) {
        pending.delete(name);
      }
      order.push(...ready);
    }
    return order;
  }

  /** Enumerations, then aliases and structures in dependency order. */
  declarationOrder(): string[] {
    return [
      ...sortedBy(this.enumerations.keys(), (name) => name),
      ...this.orderedDeclarations(),
    ];
  }

  generate(): string {
    const blocks = [
      ...this.declarationOrder().map((name) => {
        const enumeration = this.enumerations.get(name);
        if (enumeration !== undefined) {
          return this.emitEnumeration(enumeration);
        }
        const alias = this.aliases.get(name);
        return alias === undefined
          ? this.emitStructure(this.structure(name))
          : this.emitAlias(alias);
      }),
      this.emitTraits(),
    ];
    for (const name of TRI_STATE_BOOLEANS) {
      if (!this.#triState.has(name)) {
        throw new SchemaError(`${name} is no optional boolean`);
      }
    }
    return [
      "#pragma once",
      "",
      `// Generated by scripts/lsp/codegen.ts from the LSP ${VERSION} metaModel at`,
      `// microsoft/language-server-protocol@${COMMIT}. DO NOT EDIT.`,
      "",
      '#include "kota/ipc/lsp/ts.h"',
      "",
      "namespace kota::ipc::protocol {",
      "",
      ...separated(blocks),
      "",
      "}  // namespace kota::ipc::protocol",
      "",
    ].join("\n");
  }

  table(): string {
    return [
      `// Generated by scripts/lsp/codegen.ts from the LSP ${VERSION} metaModel at`,
      `// microsoft/language-server-protocol@${COMMIT}. DO NOT EDIT.`,
      "//",
      "// Every type in kota/ipc/lsp/protocol.h, for tests that decode and encode",
      "// each: KOTA_LSP_TYPE(name) for each enumeration, alias and structure, and",
      "// KOTA_LSP_RESULT(method, params) for the result of each request, named by",
      "// its params type as RequestTraits is.",
      "",
      ...this.declarationOrder().map((name) => `KOTA_LSP_TYPE(${name})`),
      ...this.requests.map(
        (request) =>
          `KOTA_LSP_RESULT(${quoted(request.method)}, ${this.render(request.params)})`,
      ),
      "",
    ].join("\n");
  }
}

const { values: options } = parseArgs({
  options: { check: { type: "boolean" } },
});
const generator = new Generator(await loadMetaModel());
for (const [path, text] of [
  [HEADER, generator.generate()],
  [TABLE, generator.table()],
]) {
  if (!options.check) {
    await writeFile(join(ROOT, path), text);
    continue;
  }
  // A CRLF checkout (core.autocrlf) holds the same text.
  const committed = await readFile(join(ROOT, path), "utf8");
  if (committed.replaceAll("\r\n", "\n") !== text) {
    console.error(`${path} is stale; run \`pixi run lsp-codegen\``);
    process.exitCode = 1;
  }
}
