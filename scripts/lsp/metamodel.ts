// The LSP metaModel pinned to one commit of microsoft/language-server-protocol,
// committed as metaModel.json so that everything derived from it is
// reproducible without the network. To follow the spec, replace the file with
// the one at SOURCE for the new COMMIT (and VERSION, for a new release), and
// update SHA256; the loader refuses a file that does not match it.
//
// The types transcribe metaModel.schema.json at the pinned commit. The loader
// trusts the model to match them, so a new pin needs the schema diffed too.
//
// Schema is the model as codegen.ts and the tests read it.

import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { isDeepStrictEqual } from "node:util";

export const VERSION = "3.18";
export const COMMIT = "b7f5132c95261c0898ae5124e7a91707abc48fcd";
const SHA256 =
  "caae8df639a4248520a3f589fd72945365e9d8ebca5baf564161a515430d9d41";
export const SOURCE =
  "https://raw.githubusercontent.com/microsoft/language-server-protocol/" +
  `${COMMIT}/_specifications/lsp/${VERSION}/metaModel/metaModel.json`;

const PATH = join(import.meta.dirname, "metaModel.json");

export type BaseTypes =
  | "URI"
  | "DocumentUri"
  | "integer"
  | "uinteger"
  | "decimal"
  | "RegExp"
  | "string"
  | "boolean"
  | "null";

export interface BaseType {
  kind: "base";
  name: BaseTypes;
}

/** A structure, enumeration or type alias, by name. */
export interface ReferenceType {
  kind: "reference";
  name: string;
}

export interface ArrayType {
  kind: "array";
  element: Type;
}

export type MapKeyType =
  | { kind: "base"; name: "URI" | "DocumentUri" | "string" | "integer" }
  | ReferenceType;

export interface MapType {
  kind: "map";
  key: MapKeyType;
  value: Type;
}

export interface AndType {
  kind: "and";
  items: Type[];
}

export interface OrType {
  kind: "or";
  items: Type[];
}

export interface TupleType {
  kind: "tuple";
  items: Type[];
}

/** An anonymous structure, e.g. `{ uri: DocumentUri }`. */
export interface StructureLiteralType {
  kind: "literal";
  value: StructureLiteral;
}

export interface StringLiteralType {
  kind: "stringLiteral";
  value: string;
}

export interface IntegerLiteralType {
  kind: "integerLiteral";
  value: number;
}

export interface BooleanLiteralType {
  kind: "booleanLiteral";
  value: boolean;
}

export type Type =
  | BaseType
  | ReferenceType
  | ArrayType
  | MapType
  | AndType
  | OrType
  | TupleType
  | StructureLiteralType
  | StringLiteralType
  | IntegerLiteralType
  | BooleanLiteralType;

/** Fields every declaration, member and structure literal may carry. */
export interface Documented {
  documentation?: string;
  since?: string;
  sinceTags?: string[];
  proposed?: boolean;
  deprecated?: string;
}

export type MessageDirection = "clientToServer" | "serverToClient" | "both";

export interface Notification extends Documented {
  method: string;
  typeName?: string;
  /** An array holds positional params. */
  params?: Type | Type[];
  messageDirection: MessageDirection;
  registrationMethod?: string;
  registrationOptions?: Type;
  clientCapability?: string;
  serverCapability?: string;
}

export interface Request extends Notification {
  result: Type;
  partialResult?: Type;
  errorData?: Type;
}

export interface Property extends Documented {
  name: string;
  type: Type;
  optional?: boolean;
}

export interface StructureLiteral extends Documented {
  properties: Property[];
}

export interface Structure extends StructureLiteral {
  name: string;
  extends?: Type[];
  mixins?: Type[];
}

export interface EnumerationEntry extends Documented {
  name: string;
  value: string | number;
}

export interface Enumeration extends Documented {
  name: string;
  type: { kind: "base"; name: "string" | "integer" | "uinteger" };
  values: EnumerationEntry[];
  supportsCustomValues?: boolean;
}

export interface TypeAlias extends Documented {
  name: string;
  type: Type;
}

export interface MetaModel {
  metaData: { version: string };
  requests: Request[];
  notifications: Notification[];
  structures: Structure[];
  enumerations: Enumeration[];
  typeAliases: TypeAlias[];
}

/** The pinned metaModel. */
export async function loadMetaModel(): Promise<MetaModel> {
  const bytes = await readFile(PATH);
  const digest = createHash("sha256").update(bytes).digest("hex");
  if (digest !== SHA256) {
    throw new Error(`${PATH} has sha256 ${digest}, pinned ${SHA256}`);
  }
  return JSON.parse(new TextDecoder().decode(bytes)) as MetaModel;
}

/** The metaModel uses a construct the tools here do not handle. */
export class SchemaError extends Error {
  override name = "SchemaError";
}

export function isBase(t: Type, name: BaseTypes): boolean {
  return t.kind === "base" && t.name === name;
}

/**
 * Items in code unit order of their key, which unlike localeCompare does not
 * depend on the locale.
 */
export function sortedBy<T>(items: Iterable<T>, key: (item: T) => string): T[] {
  return [...items].sort((a, b) => {
    const [x, y] = [key(a), key(b)];
    return x < y ? -1 : x > y ? 1 : 0;
  });
}

function byName<T extends { name: string }>(items: T[]): Map<string, T> {
  return new Map(items.map((item) => [item.name, item]));
}

/** A request or notification, with the params type its traits are keyed by. */
export interface Message {
  method: string;
  params: Type;
  result?: Type;
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

/**
 * The metaModel's declarations by name, its methods in method order with
 * their params, and each structure's properties with the inherited ones
 * inlined. A method that takes no params gets an empty params structure, so
 * that every method has a params type of its own.
 */
export class Schema {
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
      (request) => ({ ...this.#withParams(request), result: request.result }),
    );
    this.notifications = sortedBy(
      model.notifications,
      (item) => item.method,
    ).map((notification) => this.#withParams(notification));
  }

  #withParams(message: Request | Notification): Message {
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

  /**
   * The structure's properties, the inherited ones (`extends` and `mixins`)
   * first; a property the structure redeclares narrows the inherited one in
   * place.
   */
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
}
