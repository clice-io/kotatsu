// The LSP metaModel pinned to one commit of microsoft/language-server-protocol,
// so everything derived from it is reproducible; bump COMMIT and SHA256 (and
// VERSION for a new release) to follow the spec.
//
// The types transcribe metaModel.schema.json at the pinned commit. The loader
// trusts the model to match them, so a new pin needs the schema diffed too.

import { createHash } from "node:crypto";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";

export const VERSION = "3.18";
export const COMMIT = "b7f5132c95261c0898ae5124e7a91707abc48fcd";
const SHA256 =
  "caae8df639a4248520a3f589fd72945365e9d8ebca5baf564161a515430d9d41";
const SOURCE_URL =
  "https://raw.githubusercontent.com/microsoft/language-server-protocol/" +
  `${COMMIT}/_specifications/lsp/${VERSION}/metaModel/metaModel.json`;

const CACHE_PATH = join(
  import.meta.dirname,
  "../../.cache/lsp",
  `metaModel-${COMMIT}.json`,
);

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

function sha256(bytes: Uint8Array): string {
  return createHash("sha256").update(bytes).digest("hex");
}

async function metaModelBytes(): Promise<Uint8Array> {
  // A missing or damaged cache is fetched again.
  const cached = await readFile(CACHE_PATH).catch(() => undefined);
  if (cached !== undefined && sha256(cached) === SHA256) {
    return cached;
  }
  console.error(`fetching ${SOURCE_URL}`);
  const response = await fetch(SOURCE_URL);
  if (!response.ok) {
    throw new Error(`fetching ${SOURCE_URL}: HTTP ${response.status}`);
  }
  const bytes = new Uint8Array(await response.arrayBuffer());
  const digest = sha256(bytes);
  if (digest !== SHA256) {
    throw new Error(`${SOURCE_URL} has sha256 ${digest}, pinned ${SHA256}`);
  }
  await mkdir(dirname(CACHE_PATH), { recursive: true });
  await writeFile(CACHE_PATH, bytes);
  return bytes;
}

/** The pinned metaModel, downloaded once into .cache/. */
export async function loadMetaModel(): Promise<MetaModel> {
  const text = new TextDecoder().decode(await metaModelBytes());
  return JSON.parse(text) as MetaModel;
}
