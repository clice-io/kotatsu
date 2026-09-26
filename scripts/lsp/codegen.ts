// Generate include/kota/ipc/lsp/protocol.h from the pinned LSP metaModel
// (metamodel.ts). `--check` regenerates in memory and fails when the committed
// header differs.
//
// Mapping from the metaModel's TypeScript constructs to C++ (vocabulary in
// kota/ipc/lsp/ts.h):
//
// - structures become aggregates with every inherited property (`extends` and
//   `mixins`) inlined, so members are reached and designated directly; a
//   property a structure redeclares narrows the inherited one in place.
// - `LSPAny` / `LSPObject` / `LSPArray` alias the codec's dynamic value types.
// - `T | null` is `nullable<T>` (over a `variant` for several alternatives), an
//   optional property is `optional<T>`, an optional boolean is `optional_bool`
//   (absent reads as false), and an optional property holding its own
//   structure is `optional_ptr<T>`.
// - a string literal type is `Literal<"...">`, which decodes only its own text
//   so untagged variants tell their alternatives apart by it.
// - enumerations keep unknown values: integer ones are `enum class` over the
//   full integer, string ones wrap `std::string` with named constants.
// - every request / notification gets a `RequestTraits` / `NotificationTraits`
//   specialization keyed by its params type; methods without params get an
//   empty params structure so the key stays unique.

import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { isDeepStrictEqual, parseArgs } from "node:util";

import {
  COMMIT,
  VERSION,
  loadMetaModel,
  type BaseTypes,
  type Enumeration,
  type EnumerationEntry,
  type MetaModel,
  type Notification,
  type Property,
  type Request,
  type Structure,
  type Type,
  type TypeAlias,
} from "./metamodel.ts";

const OUTPUT = "include/kota/ipc/lsp/protocol.h";
const OUTPUT_PATH = join(import.meta.dirname, "../..", OUTPUT);

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
// a property documented to default to true would silently flip.
const DEFAULT_TRUE = /defaults? (?:to|is) true|true by default/i;

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

/** The metaModel uses a construct this generator does not map. */
class SchemaError extends Error {
  override name = "SchemaError";
}

/** A request or notification, with the params type its traits are keyed by. */
interface Message {
  method: string;
  params: Type;
  result?: Type;
}

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

function byName<T extends { name: string }>(items: T[]): Map<string, T> {
  return new Map(items.map((item) => [item.name, item]));
}

/**
 * Items in code unit order of their key, which unlike localeCompare does not
 * depend on the locale.
 */
function sortedBy<T>(items: Iterable<T>, key: (item: T) => string): T[] {
  return [...items].sort((a, b) => {
    const [x, y] = [key(a), key(b)];
    return x < y ? -1 : x > y ? 1 : 0;
  });
}

function isBase(t: Type, name: BaseTypes): boolean {
  return t.kind === "base" && t.name === name;
}

/** Name of the empty params structure of a method that takes none. */
function paramsName(message: Request | Notification): string {
  const typeName = message.typeName ?? "";
  for (const suffix of ["Request", "Notification"]) {
    if (typeName.endsWith(suffix)) {
      return typeName.slice(0, -suffix.length) + "Params";
    }
  }
  throw new SchemaError(
    `method \`${message.method}\` has an unexpected type name`,
  );
}

class Generator {
  readonly structures: Map<string, Structure>;
  readonly enumerations: Map<string, Enumeration>;
  readonly aliases: Map<string, TypeAlias>;
  readonly requests: Message[];
  readonly notifications: Message[];
  readonly #properties = new Map<string, Property[]>();

  constructor(model: MetaModel) {
    this.structures = byName(model.structures);
    this.enumerations = byName(model.enumerations);
    this.aliases = byName(model.typeAliases);
    this.requests = sortedBy(model.requests, (item) => item.method).map(
      (request) => ({ ...this.withParams(request), result: request.result }),
    );
    this.notifications = sortedBy(
      model.notifications,
      (item) => item.method,
    ).map((notification) => this.withParams(notification));
  }

  /**
   * The message with its params type; one that takes none gets an empty
   * params structure.
   */
  withParams(message: Request | Notification): Message {
    const { method, params } = message;
    if (Array.isArray(params)) {
      throw new SchemaError(`method \`${method}\` takes positional params`);
    }
    if (params !== undefined) {
      return { method, params };
    }
    const name = paramsName(message);
    if (this.structures.has(name)) {
      throw new SchemaError(`params structure \`${name}\` already exists`);
    }
    this.structures.set(name, {
      name,
      properties: [],
      documentation: `Params of \`${method}\`, which takes none.`,
    });
    return { method, params: { kind: "reference", name } };
  }

  structure(name: string): Structure {
    const structure = this.structures.get(name);
    if (structure === undefined) {
      throw new SchemaError(`\`${name}\` is not a structure`);
    }
    return structure;
  }

  properties(name: string): Property[] {
    const cached = this.#properties.get(name);
    if (cached !== undefined) {
      return cached;
    }
    const structure = this.structure(name);
    const merged = new Map<string, Property>();
    for (const parent of [
      ...(structure.extends ?? []),
      ...(structure.mixins ?? []),
    ]) {
      if (parent.kind !== "reference") {
        throw new SchemaError(`${name} inherits a \`${parent.kind}\` type`);
      }
      for (const prop of this.properties(parent.name)) {
        const seen = merged.get(prop.name);
        if (seen !== undefined && !isDeepStrictEqual(seen, prop)) {
          throw new SchemaError(
            `${name}: parents disagree on \`${prop.name}\``,
          );
        }
        merged.set(prop.name, prop);
      }
    }
    for (const prop of structure.properties) {
      const inherited = merged.get(prop.name);
      // The one redeclaration the spec makes narrows a string to a literal
      // (`ResourceOperation.kind` in CreateFile and friends).
      if (
        inherited !== undefined &&
        !(
          isBase(inherited.type, "string") &&
          prop.type.kind === "stringLiteral" &&
          prop.optional === inherited.optional
        )
      ) {
        throw new SchemaError(
          `${name}.${prop.name} does not narrow its parent's`,
        );
      }
      // Setting an existing key keeps its position: the narrowed property
      // stays where the parent declared it.
      merged.set(prop.name, prop);
    }
    const properties = [...merged.values()];
    this.#properties.set(name, properties);
    return properties;
  }

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
        const alternatives = t.items
          .filter((item) => !isBase(item, "null"))
          .map((item) => this.render(item));
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
      if (DEFAULT_TRUE.test(prop.documentation ?? "")) {
        throw new SchemaError(`${owner}.${prop.name} defaults to true`);
      }
      return `optional_bool ${name} = {};`;
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

  generate(): string {
    const blocks = [
      ...sortedBy(this.enumerations.values(), (item) => item.name).map(
        (enumeration) => this.emitEnumeration(enumeration),
      ),
      ...this.orderedDeclarations().map((name) => {
        const alias = this.aliases.get(name);
        return alias === undefined
          ? this.emitStructure(this.structure(name))
          : this.emitAlias(alias);
      }),
      this.emitTraits(),
    ];
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
}

const { values: options } = parseArgs({
  options: { check: { type: "boolean" } },
});
const header = new Generator(await loadMetaModel()).generate();
if (!options.check) {
  await writeFile(OUTPUT_PATH, header);
} else {
  // A CRLF checkout (core.autocrlf) holds the same header.
  const committed = await readFile(OUTPUT_PATH, "utf8");
  if (committed.replaceAll("\r\n", "\n") !== header) {
    console.error(`${OUTPUT} is stale; run \`pixi run lsp-codegen\``);
    process.exitCode = 1;
  }
}
