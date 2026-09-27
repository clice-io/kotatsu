// Every type of kota/ipc/lsp/protocol.h against the metaModel it is generated
// from, through lsp_roundtrip:
//
// - a value drawn from a type's metaModel declaration decodes, and encodes
//   back to the same JSON;
// - a value broken at one place (a property left out, or a value replaced by
//   one of another kind or range) decodes exactly when the metaModel says it
//   is still a value of the type.
//
// Both hold as the metaModel says, bent where known_deviations.ts records
// that protocol.h reads otherwise. Every type is checked, and the failures of
// all of them reported together.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import fc from "fast-check";

import {
  loadMetaModel,
  Schema,
  type Type,
} from "../../../../scripts/lsp/metamodel.ts";
import { Driver } from "../../../harness/driver.ts";
import {
  FUZZ_TIMEOUT,
  fuzz,
  roundtrip,
  RUNS,
  within,
} from "../../../harness/fuzz.ts";
import type { JsonLines } from "../../../harness/jsonl.ts";
import { ProtocolValues } from "../harness/protocol_values.ts";

const schema = new Schema(await loadMetaModel());
const values = new ProtocolValues(schema);

// Each type by the name lsp_roundtrip knows it by (protocol_types.inc).
const types: [string, Type][] = [
  ...[
    ...schema.enumerations.keys(),
    ...schema.aliases.keys(),
    ...schema.structures.keys(),
  ]
    .filter((name) => !name.startsWith("_"))
    .map((name): [string, Type] => [name, { kind: "reference", name }]),
  ...schema.requests.map(({ method, result }): [string, Type] => {
    assert.ok(result);
    return [`result:${method}`, result];
  }),
];

// A tenth of the runs for each type, as there are hundreds of them.
const RUNS_PER_TYPE = Math.max(1, Math.round(RUNS / 10));

type Answer = { value?: unknown; error?: string };

/**
 * lsp_roundtrip's answers, until it ends or misses one: an answer after that
 * could be to any earlier question. A run from then on is skipped, so that a
 * failure it caused is not shrunk against answers that mean nothing.
 */
class Roundtrip {
  readonly #lines: JsonLines;
  /** Why the answers can no longer be trusted, once they cannot. */
  broken: string | undefined;

  constructor(lines: JsonLines) {
    this.#lines = lines;
  }

  async ask(type: string, value: unknown): Promise<Answer> {
    fc.pre(this.broken === undefined);
    this.#lines.send({ type, value });
    try {
      const answer = await within(
        this.#lines.receive(),
        `answer about ${type}`,
      );
      assert.ok(answer !== undefined, "lsp_roundtrip ended");
      return answer as Answer;
    } catch (error) {
      this.broken = String(error);
      throw error;
    }
  }

  end(): void {
    this.#lines.end();
  }
}

/**
 * Checks `property` for every type, and fails with every type it fails for.
 * One deadline, 60% of the test's timeout, covers every type: the types left
 * when it passes are not checked, as a slow leg's runs are not. Once
 * lsp_roundtrip ends or misses an answer, the types left are not checked
 * either, and the test fails.
 */
async function forEveryType(
  t: TestContext,
  property: (
    roundtrip: Roundtrip,
    name: string,
    type: Type,
  ) => fc.IAsyncPropertyWithHooks<unknown>,
): Promise<void> {
  const driver = await Driver.spawn(t, "lsp_roundtrip");
  const channel = new Roundtrip(driver.jsonLines());
  const failures: string[] = [];
  const deadline = performance.now() + FUZZ_TIMEOUT * 0.6;
  let checked = 0;
  for (const [name, type] of types) {
    const left = deadline - performance.now();
    if (channel.broken !== undefined || left <= 0) {
      break;
    }
    try {
      await fuzz(property(channel, name, type), RUNS_PER_TYPE, left);
    } catch (error) {
      // fast-check's message has the counterexample, its cause what failed.
      const { message, cause } = error as Error;
      failures.push(`${name}: ${message}\n${String(cause)}`);
    }
    checked += 1;
  }
  if (channel.broken !== undefined) {
    failures.push(
      `${types.length - checked} types not checked: ${channel.broken}`,
    );
  } else if (checked < types.length) {
    t.diagnostic(`${types.length - checked} types not checked in time`);
  }
  channel.end();
  await driver.expectExit(0);
  assert.deepEqual(failures, [], `${failures.length} of ${checked} types fail`);
}

test("values_roundtrip", { timeout: FUZZ_TIMEOUT }, (t) =>
  forEveryType(t, (channel, name, type) =>
    fc.asyncProperty(values.of(type), async (value) => {
      assert.deepEqual(await channel.ask(name, value), {
        value: roundtrip(value),
      });
    }),
  ),
);

type Path = (string | number)[];

function paths(value: unknown, path: Path = []): Path[] {
  const children =
    typeof value === "object" && value !== null
      ? Object.entries(value).flatMap(([key, child]) =>
          paths(child, [...path, Array.isArray(value) ? Number(key) : key]),
        )
      : [];
  return [path, ...children];
}

const LEAVE_OUT = Symbol("left out");

// What a place is broken with: nothing, for a property, or a value of another
// kind or range.
const BREAKS = [
  LEAVE_OUT,
  "text",
  0,
  -1,
  1.5,
  2 ** 31,
  2 ** 32,
  true,
  null,
  {},
  [],
] as const;

function broken(
  value: unknown,
  path: Path,
  by: (typeof BREAKS)[number],
): unknown {
  if (path.length === 0) {
    return by;
  }
  const copy = structuredClone(value) as Record<string | number, unknown>;
  let parent = copy;
  for (const key of path.slice(0, -1)) {
    parent = parent[key] as Record<string | number, unknown>;
  }
  const last = path.at(-1) as string | number;
  if (by === LEAVE_OUT) {
    delete parent[last];
  } else {
    parent[last] = structuredClone(by);
  }
  return copy;
}

test(
  "broken_values_decode_as_the_metamodel_says",
  { timeout: FUZZ_TIMEOUT },
  (t) =>
    forEveryType(t, (channel, name, type) =>
      fc.asyncProperty(
        values.of(type),
        fc.nat(),
        fc.constantFrom(...BREAKS),
        async (value, pick, by) => {
          const places = paths(value);
          const place = places[pick % places.length];
          // Only a property can be left out.
          fc.pre(by !== LEAVE_OUT || typeof place.at(-1) === "string");
          const input = broken(value, place, by);
          const answer = await channel.ask(name, input);
          const expected = values.reads(type, input);
          assert.equal(
            answer.error === undefined,
            expected,
            `${JSON.stringify(input)} ${expected ? "is" : "is not"} a ${name}, ` +
              `but lsp_roundtrip answered ${JSON.stringify(answer)}`,
          );
        },
      ),
    ),
);
