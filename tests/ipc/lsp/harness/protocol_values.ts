// Values of the LSP protocol's types, drawn from the pinned metaModel: fast-check
// arbitraries for each type, and whether kota/ipc/lsp/protocol.h reads a value
// as a type. Both follow the spec, bent where known_deviations.ts says
// protocol.h reads otherwise.

import fc from "fast-check";

import {
  isBase,
  TRI_STATE_BOOLEANS,
  type BaseTypes,
  type Property,
  type Schema,
  type Type,
} from "../../../../scripts/lsp/metamodel.ts";
import { deviations } from "./known_deviations.ts";

// Structures deeper than this leave their optional properties out, and
// arrays and maps are empty, which ends every recursion the spec has.
const MAX_DEPTH = 3;

const INT32 = { min: -(2 ** 31), max: 2 ** 31 - 1 };
const UINT = {
  min: 0,
  max: deviations.fullUinteger ? 2 ** 32 - 1 : 2 ** 31 - 1,
};

type Json = unknown;

function isObject(value: Json): value is Record<string, Json> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function inRange(value: Json, range: { min: number; max: number }): boolean {
  return (
    Number.isInteger(value) &&
    (value as number) >= range.min &&
    (value as number) <= range.max
  );
}

// Unicode text; a lone surrogate is no text, and JSON has no way to carry it.
const text = fc.string({ unit: "grapheme", maxLength: 8 });

const BASE: Record<BaseTypes, fc.Arbitrary<Json>> = {
  integer: fc.integer(INT32),
  uinteger: fc.integer(UINT),
  decimal: fc.double({ noNaN: true, noDefaultInfinity: true }),
  string: text,
  URI: text,
  DocumentUri: text,
  RegExp: text,
  boolean: fc.boolean(),
  null: fc.constant(null),
};

export class ProtocolValues {
  readonly #schema: Schema;
  readonly #arbitraries = new Map<string, fc.Arbitrary<Json>>();

  constructor(schema: Schema) {
    this.#schema = schema;
  }

  /** Values of `t`. */
  of(t: Type, depth = 0): fc.Arbitrary<Json> {
    const deep = depth >= MAX_DEPTH;
    switch (t.kind) {
      case "base":
        return BASE[t.name];
      case "reference":
        return this.#named(t.name, depth);
      case "array":
        return deep
          ? fc.constant([])
          : fc.array(this.of(t.element, depth + 1), { maxLength: 2 });
      case "map":
        return deep
          ? fc.constant({})
          : fc.dictionary(text, this.of(t.value, depth + 1), { maxKeys: 2 });
      case "tuple":
        return fc.tuple(...t.items.map((item) => this.of(item, depth + 1)));
      case "literal":
        return fc.constant({});
      case "stringLiteral":
        return fc.constant(t.value);
      case "or": {
        // In protocol.h's order, in which an earlier alternative that reads a
        // value takes it.
        const items = this.#schema.derivedFirst(t.items);
        const alternatives = items.map((item, i) =>
          this.of(item, depth).map((value): [number, Json] => [i, value]),
        );
        return fc
          .oneof(...alternatives)
          .filter(
            ([i, value]) =>
              !deviations.shadowedAlternatives ||
              !items.slice(0, i).some((earlier) => this.reads(earlier, value)),
          )
          .map(([, value]) => value);
      }
      default:
        throw new Error(
          `the metaModel's \`${t.kind}\` types have no values here`,
        );
    }
  }

  /** Whether protocol.h reads `value` as `t`. */
  reads(t: Type, value: Json): boolean {
    switch (t.kind) {
      case "base":
        switch (t.name) {
          case "integer":
            return inRange(value, INT32);
          case "uinteger":
            return inRange(value, UINT);
          case "decimal":
            return typeof value === "number";
          case "boolean":
            return typeof value === "boolean";
          case "null":
            return value === null;
          default:
            return typeof value === "string";
        }
      case "reference":
        return this.#readsNamed(t.name, value);
      case "array":
        return (
          Array.isArray(value) &&
          value.every((item) => this.reads(t.element, item))
        );
      case "map":
        return (
          isObject(value) &&
          Object.values(value).every((item) => this.reads(t.value, item))
        );
      case "tuple":
        return (
          Array.isArray(value) &&
          value.length === t.items.length &&
          t.items.every((item, i) => this.reads(item, value[i]))
        );
      case "literal":
        return isObject(value);
      case "stringLiteral":
        return value === t.value;
      case "or":
        return t.items.some((item) => this.reads(item, value));
      default:
        throw new Error(
          `the metaModel's \`${t.kind}\` types have no values here`,
        );
    }
  }

  #named(name: string, depth: number): fc.Arbitrary<Json> {
    const key = `${name}@${Math.min(depth, MAX_DEPTH)}`;
    let arbitrary = this.#arbitraries.get(key);
    if (arbitrary === undefined) {
      arbitrary = this.#make(name, depth);
      this.#arbitraries.set(key, arbitrary);
    }
    return arbitrary;
  }

  #make(name: string, depth: number): fc.Arbitrary<Json> {
    switch (name) {
      case "LSPAny":
        return fc.jsonValue({ depthSize: "xsmall" });
      case "LSPObject":
        return fc.dictionary(text, fc.jsonValue({ depthSize: "xsmall" }), {
          maxKeys: 2,
        });
      case "LSPArray":
        return fc.array(fc.jsonValue({ depthSize: "xsmall" }), {
          maxLength: 2,
        });
    }
    const enumeration = this.#schema.enumerations.get(name);
    if (enumeration !== undefined) {
      const known = fc.constantFrom(
        ...enumeration.values.map(({ value }) => value),
      );
      return deviations.openEnumerations
        ? fc.oneof(known, BASE[enumeration.type.name])
        : known;
    }
    const alias = this.#schema.aliases.get(name);
    if (alias !== undefined) {
      return this.of(alias.type, depth);
    }
    const properties = this.#schema
      .properties(name)
      .filter((prop) => !prop.optional || depth < MAX_DEPTH);
    const model = Object.fromEntries(
      properties.map((prop) => [
        prop.name,
        this.#property(name, prop, depth + 1),
      ]),
    );
    const requiredKeys = properties
      .filter((prop) => !prop.optional)
      .map((prop) => prop.name);
    return fc.record(model, { requiredKeys });
  }

  #property(owner: string, prop: Property, depth: number): fc.Arbitrary<Json> {
    if (
      prop.optional &&
      isBase(prop.type, "boolean") &&
      !TRI_STATE_BOOLEANS.has(`${owner}.${prop.name}`) &&
      deviations.falseOptionalBoolean
    ) {
      return fc.constant(true);
    }
    return this.of(prop.type, depth);
  }

  #readsNamed(name: string, value: Json): boolean {
    switch (name) {
      case "LSPAny":
        return true;
      case "LSPObject":
        return isObject(value);
      case "LSPArray":
        return Array.isArray(value);
    }
    const enumeration = this.#schema.enumerations.get(name);
    if (enumeration !== undefined) {
      return deviations.openEnumerations
        ? this.reads({ kind: "base", name: enumeration.type.name }, value)
        : enumeration.values.some((entry) => entry.value === value);
    }
    const alias = this.#schema.aliases.get(name);
    if (alias !== undefined) {
      return this.reads(alias.type, value);
    }
    if (!isObject(value)) {
      return false;
    }
    const properties = this.#schema.properties(name);
    if (!deviations.unknownProperties) {
      const known = new Set(properties.map((prop) => prop.name));
      if (Object.keys(value).some((key) => !known.has(key))) {
        return false;
      }
    }
    return properties.every((prop) => {
      if (!(prop.name in value)) {
        return prop.optional === true;
      }
      const member = value[prop.name];
      const readsAbsentAsFalse =
        isBase(prop.type, "boolean") &&
        !TRI_STATE_BOOLEANS.has(`${name}.${prop.name}`);
      if (
        member === null &&
        prop.optional &&
        !readsAbsentAsFalse &&
        deviations.nullOptional
      ) {
        return true;
      }
      return this.reads(prop.type, member);
    });
  }
}
