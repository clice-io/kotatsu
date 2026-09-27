// The base protocol's framing, as jsonrpc_driver reads it from raw bytes:
//
// - a stream of frames reads the same however it is split into writes, with
//   any spelling of the header the base protocol allows; a frame whose
//   payload is not JSON is answered with ParseError (-32700), and one over the
//   limit with MessageTooLarge (-32010), and the frames after them are read;
// - a header the driver cannot frame by, or input that ends inside a frame,
//   ends the connection cleanly: what came before is answered, nothing after,
//   and the driver exits with 0;
// - a frame larger than the limit is skipped, and fails what it concerns as
//   far as its first bytes tell (D1): a request is answered MessageTooLarge
//   (-32010), a response fails the request it answers, a notification is
//   dropped, and anything else fails every pending request; the connection
//   goes on;
// - no input makes it crash or hang.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import fc from "fast-check";

import { Driver } from "../harness/driver.ts";
import { FUZZ_TIMEOUT, fuzz, roundtrip, within } from "../harness/fuzz.ts";
import { frame, type Message } from "../harness/raw.ts";
import { Session } from "../harness/session.ts";

type Header = {
  name: string;
  before: string;
  after: string;
  extra?: "first" | "last";
};

const header: fc.Arbitrary<Header> = fc.record(
  {
    name: fc.constantFrom("Content-Length", "content-length", "CONTENT-LENGTH"),
    before: fc.constantFrom("", " ", "\t", "  "),
    after: fc.constantFrom("", " ", "\t"),
    extra: fc.constantFrom("first" as const, "last" as const),
  },
  { requiredKeys: ["name", "before", "after"] },
);

const CONTENT_TYPE = "Content-Type: application/vscode-jsonrpc; charset=utf-8";

function framed(payload: string, spelling: Header): Buffer {
  const bytes = Buffer.from(payload);
  const lines = [
    `${spelling.name}:${spelling.before}${bytes.length}${spelling.after}`,
  ];
  if (spelling.extra === "first") {
    lines.unshift(CONTENT_TYPE);
  } else if (spelling.extra === "last") {
    lines.push(CONTENT_TYPE);
  }
  return Buffer.concat([Buffer.from(`${lines.join("\r\n")}\r\n\r\n`), bytes]);
}

const notJson = fc.string().filter((text) => {
  try {
    JSON.parse(text);
    return false;
  } catch {
    return true;
  }
});

// The limit split_stream_reads_the_same runs the driver with.
const SPLIT_LIMIT = 1024;

// Frames of echo requests, some over the limit, and of payloads that are not
// JSON, each with its own header.
const frames = fc.array(
  fc.tuple(
    fc.oneof(
      {
        weight: 4,
        arbitrary: fc
          .array(fc.jsonValue({ depthSize: "small" }))
          .map((params) => ({ params })),
      },
      {
        weight: 1,
        arbitrary: fc
          .integer({ min: SPLIT_LIMIT, max: 3 * SPLIT_LIMIT })
          .map((length) => ({ params: ["x".repeat(length)] })),
      },
      { weight: 1, arbitrary: notJson.map((junk) => ({ junk })) },
    ),
    header,
  ),
  { minLength: 1, maxLength: 12 },
);

// Byte offsets at which a stream is cut into writes.
function splitAt(bytes: Buffer, cuts: number[]): Buffer[] {
  const offsets = [
    ...new Set(cuts.map((cut) => cut % (bytes.length + 1))),
  ].sort((a, b) => a - b);
  const chunks: Buffer[] = [];
  let start = 0;
  for (const offset of [...offsets, bytes.length]) {
    chunks.push(bytes.subarray(start, offset));
    start = offset;
  }
  return chunks.filter((chunk) => chunk.length > 0);
}

async function spawn(
  t: TestContext,
  args: string[] = [],
): Promise<[Driver, Session]> {
  const driver = await Driver.spawn(t, "jsonrpc_driver", args);
  return [driver, new Session(driver.raw())];
}

async function reported<T>(driver: Driver, body: () => Promise<T>): Promise<T> {
  try {
    return await body();
  } catch (error) {
    await driver.kill();
    throw new Error(
      `${String(error)}\n${driver.name}'s stderr:\n${driver.stderr}`,
      {
        cause: error,
      },
    );
  }
}

test("split_stream_reads_the_same", { timeout: FUZZ_TIMEOUT }, (t) =>
  fuzz(
    fc.asyncProperty(frames, fc.array(fc.nat()), async (drawn, cuts) => {
      const [driver, session] = await spawn(t, [
        `--max-payload=${SPLIT_LIMIT}`,
      ]);
      await reported(driver, async () => {
        const echoes: {
          response: Promise<Message>;
          params: unknown;
          oversized: boolean;
        }[] = [];
        const bytes: Buffer[] = [];
        for (const [content, spelling] of drawn) {
          if ("params" in content) {
            const id = `echo-${echoes.length}`;
            const payload = JSON.stringify({
              jsonrpc: "2.0",
              id,
              method: "test/echo",
              params: content.params,
            });
            const oversized = Buffer.byteLength(payload) > SPLIT_LIMIT;
            if (oversized) {
              driver.expectLog(/^\[warn\] skipped: /);
              driver.expectLog(/^\[error\] error response: /);
            }
            echoes.push({
              response: session.expect(id),
              params: content.params,
              oversized,
            });
            bytes.push(framed(payload, spelling));
          } else {
            driver.expectLog(/^\[error\] error response: /);
            bytes.push(framed(content.junk, spelling));
          }
        }
        for (const chunk of splitAt(Buffer.concat(bytes), cuts)) {
          await session.wire.write(chunk);
        }
        for (const { response, params, oversized } of echoes) {
          const answer = await within(response, "echo answer");
          if (oversized) {
            assert.equal((answer.error as { code?: unknown }).code, -32010);
          } else {
            assert.deepEqual(answer.result, roundtrip(params));
          }
        }
        // What does not parse is answered with a parse error, in order, so
        // before the probe's answer.
        const probe = await within(
          session.request("test/echo", []),
          "probe answer",
        );
        assert.deepEqual(probe.result, []);
        const junk = drawn.filter(([content]) => "junk" in content).length;
        assert.deepEqual(
          session.strays.map(
            (stray) => (stray.error as { code?: unknown } | undefined)?.code,
          ),
          Array.from({ length: junk }, () => -32700),
        );
        session.wire.end();
        await within(session.ended, "end of the driver's output");
        await driver.expectExit(0);
      });
    }),
  ),
);

const echo = (id: number) =>
  frame({ jsonrpc: "2.0", id, method: "test/echo", params: [id] });

// Input after which the driver can no longer find the next frame. A second
// request follows each, and must not be answered.
const unframeable: [string, Buffer][] = [
  ["no_content_length", Buffer.from(`${CONTENT_TYPE}\r\n\r\n{}`)],
  ["empty_content_length", Buffer.from("Content-Length: \r\n\r\n{}")],
  ["non_numeric_content_length", Buffer.from("Content-Length: 2x\r\n\r\n{}")],
  ["negative_content_length", Buffer.from("Content-Length: -2\r\n\r\n{}")],
  [
    "overflowing_content_length",
    Buffer.from("Content-Length: 99999999999999999999999\r\n\r\n"),
  ],
  ["header_over_8_kib", Buffer.from(`X-Padding: ${"a".repeat(9000)}`)],
  ["lf_only_header", Buffer.from("Content-Length: 2\n\n{}")],
];

for (const [name, bytes] of unframeable) {
  test(`${name}_ends_the_connection`, async (t) => {
    const [driver, session] = await spawn(t);
    const first = session.expect(1);
    const second = session.expect(2);
    await session.wire.write(Buffer.concat([echo(1), bytes, echo(2)]));
    assert.deepEqual((await first).result, [1]);
    session.wire.end();
    await session.ended;
    await assert.rejects(second);
    assert.deepEqual(session.strays, []);
    await driver.expectExit(0);
  });
}

// Input that ends inside a frame.
const truncated: [string, Buffer][] = [
  ["header", Buffer.from("Content-Len")],
  ["separator", Buffer.from("Content-Length: 2\r\n")],
  ["payload", Buffer.from('Content-Length: 20\r\n\r\n{"jsonrpc"')],
];

for (const [name, bytes] of truncated) {
  test(`input_ending_inside_${name}_ends_cleanly`, async (t) => {
    const [driver, session] = await spawn(t);
    const first = session.expect(1);
    await session.wire.write(Buffer.concat([echo(1), bytes]));
    assert.deepEqual((await first).result, [1]);
    session.wire.end();
    await session.ended;
    assert.deepEqual(session.strays, []);
    await driver.expectExit(0);
  });
}

// The limit the driver runs with here, and a payload over it.
const LIMIT = 256;
const LARGE = "x".repeat(LIMIT + 1);

async function limited(t: TestContext): Promise<[Driver, Session]> {
  const driver = await Driver.spawn(t, "jsonrpc_driver", [
    `--max-payload=${LIMIT}`,
  ]);
  driver.expectLog(/^\[warn\] skipped: /);
  return [driver, new Session(driver.raw())];
}

async function stillServes(session: Session): Promise<void> {
  const probe = await within(
    session.request("test/echo", ["probe"]),
    "probe answer",
  );
  assert.deepEqual(probe.result, ["probe"]);
}

test("oversized_request_is_answered_too_large", async (t) => {
  const [driver, session] = await limited(t);
  driver.expectLog(/^\[error\] error response: /);
  const answer = session.expect(7);
  session.wire.send({
    jsonrpc: "2.0",
    id: 7,
    method: "test/echo",
    params: [LARGE],
  });
  assert.equal(((await answer).error as { code?: unknown }).code, -32010);
  await stillServes(session);
  session.wire.end();
  await session.ended;
  await driver.expectExit(0);
});

test("oversized_response_fails_its_request", async (t) => {
  const driver = await Driver.spawn(t, "jsonrpc_driver", [
    `--max-payload=${LIMIT}`,
  ]);
  driver.expectLog(/^\[warn\] skipped: /);
  const session = new Session(driver.raw(), (request) => ({
    jsonrpc: "2.0",
    id: request.id,
    result: LARGE,
  }));
  const call = await session.request("test/call", {
    method: "client/large",
    params: {},
  });
  const { error } = call.result as { error?: { code?: unknown } };
  assert.equal(error?.code, -32010);
  await stillServes(session);
  session.wire.end();
  await session.ended;
  await driver.expectExit(0);
});

test("oversized_notification_is_dropped", async (t) => {
  const [driver, session] = await limited(t);
  session.notify("test/large", [LARGE]);
  await stillServes(session);
  assert.deepEqual(session.strays, []);
  session.wire.end();
  await session.ended;
  await driver.expectExit(0);
});

// Its first bytes tell nothing, so it could have answered any of them.
test("oversized_unknown_message_fails_every_pending_request", async (t) => {
  const asked = Promise.withResolvers<void>();
  const driver = await Driver.spawn(t, "jsonrpc_driver", [
    `--max-payload=${LIMIT}`,
  ]);
  driver.expectLog(/^\[warn\] skipped: /);
  driver.expectLog(/^\[error\] failing 1 pending request\(s\): /);
  const session = new Session(driver.raw(), () => {
    asked.resolve();
    return undefined;
  });
  const call = session.request("test/call", {
    method: "client/stall",
    params: {},
  });
  await asked.promise;
  await session.wire.write(
    Buffer.from(`Content-Length: ${LARGE.length}\r\n\r\n${LARGE}`),
  );
  const { error } = (await call).result as { error?: { code?: unknown } };
  assert.equal(error?.code, -32010);
  await stillServes(session);
  session.wire.end();
  await session.ended;
  await driver.expectExit(0);
});

test("any_input_ends_cleanly", { timeout: FUZZ_TIMEOUT }, (t) =>
  fuzz(
    fc.asyncProperty(
      fc.array(
        fc.oneof(
          fc.uint8Array({ maxLength: 512 }),
          fc
            .jsonValue({ depthSize: "small" })
            .map((params) =>
              frame({ jsonrpc: "2.0", id: 1, method: "test/echo", params }),
            ),
          fc.constantFrom(
            ...[...unframeable, ...truncated].map(([, bytes]) => bytes),
          ),
        ),
        { maxLength: 8 },
      ),
      async (pieces) => {
        const [driver, session] = await spawn(t);
        await reported(driver, async () => {
          // Whatever it logs about the input.
          driver.expectLog(/^\[(?:warn|error)\] /);
          for (const piece of pieces) {
            await session.wire.write(piece);
          }
          session.wire.end();
          await within(session.ended, "end of the driver's output");
          await driver.expectExit(0);
        });
      },
    ),
  ),
);
