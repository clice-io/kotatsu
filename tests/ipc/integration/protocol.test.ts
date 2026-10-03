// Model-based fuzzing of JSON-RPC against jsonrpc_driver. fast-check draws a
// sequence of client actions and runs it on a fresh driver over a raw
// channel, checking:
//
// - every request gets exactly one response, with its id;
// - echo answers its params; fail answers its error; a cancelled sleep
//   answers null or RequestCancelled (-32800);
// - an unknown method fails with MethodNotFound (-32601), params of the wrong
//   shape with InvalidParams (-32602), and an unknown notification is not
//   answered;
// - what the driver sends while it answers a request, notifications and
//   requests to the client, arrives before that request's response, and a
//   request to the client that times out is cancelled with $/cancelRequest;
// - the driver stays alive: a probe after every action is answered;
// - at the end of its input the driver answers what is in flight and exits
//   with 0.
//
// A second property mixes in garbage (bad JSON, JSON that is no message, an
// id of the wrong type or null, an error response without id), which must be
// answered as JSON-RPC says, with an error and a null id or not at all, and
// leave the connection usable.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import fc from "fast-check";

import type { Driver } from "../../harness/driver.ts";
import {
  anyChar,
  FUZZ_TIMEOUT,
  fuzz,
  roundtrip,
  within,
} from "../../harness/fuzz.ts";
import { probe } from "../harness/jsonrpc_driver.ts";
import { errorOf, frameText, resultOf, type Message } from "../harness/raw.ts";
import { Session } from "../harness/session.ts";

type Model = { sleeping: number[] };

type Real = {
  driver: Driver;
  session: Session;
  cancelled: Set<number>;
  // Checks of responses that come later, run as they come.
  settled: Promise<void>[];
  // The driver's request for each test/call, by its tag.
  calls: Map<number, Message>;
  nextTag: number;
};

type Command = fc.AsyncCommand<Model, Real>;

// Text of any code point, control characters and those past ASCII included:
// Content-Length counts bytes, and JSON escapes control characters.
const text = fc.string({ unit: anyChar });

function json(depthSize?: fc.DepthSize) {
  return fc.jsonValue({ depthSize, stringUnit: anyChar });
}

// A JSON-RPC request's params: structured.
const params = fc.oneof(
  fc.dictionary(text, json("small")),
  fc.array(json("small")),
);

// A line of text, so that a log line quoting it stays one line: none of the
// line ends a regular expression's ^ and $ see.
const line = text.filter((value) => !/[\r\n\u2028\u2029]/.test(value));

// The driver still serves, and has sent nothing the session did not ask for.
async function stillServes(real: Real): Promise<void> {
  assert.deepEqual(await probe(real.session), []);
}

class Echo implements Command {
  readonly params: unknown;
  constructor(params: unknown) {
    this.params = params;
  }
  check = () => true;
  async run(_: Model, real: Real) {
    const response = await within(
      real.session.request("test/echo", this.params),
      "echo",
    );
    assert.deepEqual(resultOf(response), roundtrip(this.params));
    await stillServes(real);
  }
  toString = () => `echo ${JSON.stringify(this.params)}`;
}

class Sleep implements Command {
  readonly ms: number;
  constructor(ms: number) {
    this.ms = ms;
  }
  check = () => true;
  async run(model: Model, real: Real) {
    const { id, response } = real.session.start("test/sleep", { ms: this.ms });
    model.sleeping.push(id);
    const checked = within(response, `answer to sleep ${id}`).then((answer) => {
      if (real.cancelled.has(id) && "error" in answer) {
        assert.equal(errorOf(answer).code, -32800);
      } else {
        assert.equal(resultOf(answer), null);
      }
    });
    // Handled when the run awaits it, at its end.
    checked.catch(() => {});
    real.settled.push(checked);
    await stillServes(real);
  }
  toString = () => `sleep ${this.ms}`;
}

class Cancel implements Command {
  readonly pick: number;
  constructor(pick: number) {
    this.pick = pick;
  }
  check = (model: Readonly<Model>) => model.sleeping.length > 0;
  async run(model: Model, real: Real) {
    const id = model.sleeping[this.pick % model.sleeping.length];
    real.driver.expectLog(/^\[error\] error response: request cancelled$/);
    real.cancelled.add(id);
    real.session.notify("$/cancelRequest", { id });
    await stillServes(real);
  }
  toString = () => `cancel sleep #${this.pick}`;
}

class Fail implements Command {
  readonly code: number;
  readonly message: string;
  constructor(code: number, message: string) {
    this.code = code;
    this.message = message;
  }
  check = () => true;
  async run(_: Model, real: Real) {
    real.driver.expectLog(/^\[error\] error response: /);
    const response = await within(
      real.session.request("test/fail", {
        code: this.code,
        message: this.message,
      }),
      "fail",
    );
    const { code, message } = errorOf(response);
    assert.deepEqual(
      { code, message },
      { code: this.code, message: this.message },
    );
    await stillServes(real);
  }
  toString = () => `fail ${this.code} ${JSON.stringify(this.message)}`;
}

class UnknownMethod implements Command {
  readonly method: string;
  constructor(method: string) {
    this.method = method;
  }
  check = () => true;
  async run(_: Model, real: Real) {
    real.driver.expectLog(/^\[error\] error response: method not found: /);
    const response = await within(
      real.session.request(this.method, {}),
      "unknown method",
    );
    assert.equal(errorOf(response).code, -32601);
    await stillServes(real);
  }
  toString = () => `request ${JSON.stringify(this.method)}`;
}

class BadParams implements Command {
  readonly params: unknown;
  constructor(params: unknown) {
    this.params = params;
  }
  check = () => true;
  async run(_: Model, real: Real) {
    real.driver.expectLog(
      /^\[warn\] request 'test\/sleep' params deserialization failed: /,
    );
    real.driver.expectLog(/^\[error\] error response: /);
    const response = await within(
      real.session.request("test/sleep", this.params),
      "bad params",
    );
    assert.equal(errorOf(response).code, -32602);
    await stillServes(real);
  }
  toString = () => `sleep with ${JSON.stringify(this.params)}`;
}

class UnknownNotification implements Command {
  readonly method: string;
  constructor(method: string) {
    this.method = method;
  }
  check = () => true;
  async run(_: Model, real: Real) {
    real.driver.expectLog(/^\[warn\] unhandled notification: /);
    real.session.notify(this.method, {});
    await stillServes(real);
  }
  toString = () => `notify ${JSON.stringify(this.method)}`;
}

class Notify implements Command {
  readonly params: unknown;
  readonly count: number;
  constructor(params: unknown, count: number) {
    this.params = params;
    this.count = count;
  }
  check = () => true;
  async run(_: Model, real: Real) {
    const method = `test/note/${real.nextTag++}`;
    const response = await within(
      real.session.request("test/notify", {
        method,
        params: this.params,
        count: this.count,
      }),
      "notify",
    );
    assert.equal(resultOf(response), null);
    const notes = real.session.notifications.filter(
      (note) => note.method === method,
    );
    assert.deepEqual(
      notes.map((note) => note.params),
      Array.from({ length: this.count }, () => roundtrip(this.params)),
    );
    await stillServes(real);
  }
  toString = () => `notify ${this.count} x ${JSON.stringify(this.params)}`;
}

// How the client answers the driver's request in a test/call.
type Policy =
  | { kind: "result"; value: unknown }
  | { kind: "error"; code: number; message: string; data?: unknown }
  | { kind: "none" }
  | { kind: "wrongId" };

const TIMED_OUT = { code: -32800, message: "request timed out" };

class Call implements Command {
  readonly policy: Policy;
  constructor(policy: Policy) {
    this.policy = policy;
  }
  check = () => true;
  async run(_: Model, real: Real) {
    const tag = real.nextTag++;
    const unanswered =
      this.policy.kind === "none" || this.policy.kind === "wrongId";
    if (this.policy.kind === "wrongId") {
      real.driver.expectLog(/^\[warn\] orphan response for id=/);
    }
    const response = await within(
      real.session.request("test/call", {
        method: "client/call",
        params: { tag, policy: this.policy },
        timeoutMs: unanswered ? 20 : undefined,
      }),
      "call",
    );
    const request = real.calls.get(tag);
    assert.ok(request, "the driver's request never came");
    const outcome = resultOf(response) as {
      result?: unknown;
      error?: Record<string, unknown>;
    };
    switch (this.policy.kind) {
      case "result":
        assert.deepEqual(outcome, { result: roundtrip(this.policy.value) });
        break;
      case "error": {
        const { code, message, data } = this.policy;
        // An error without data leaves the member out, and so does one
        // whose data is null, which reads as none.
        assert.deepEqual(outcome, {
          error:
            data === undefined || data === null
              ? { code, message }
              : { code, message, data: roundtrip(data) },
        });
        break;
      }
      default: {
        const { code, message } = outcome.error ?? {};
        assert.deepEqual({ code, message }, TIMED_OUT);
        const cancels = real.session.notifications.filter(
          (note) =>
            note.method === "$/cancelRequest" &&
            (note.params as { id?: unknown })?.id === request.id,
        );
        assert.equal(cancels.length, 1);
      }
    }
    await stillServes(real);
  }
  toString = () => `call answered ${JSON.stringify(this.policy)}`;
}

// Answers the driver's request in a test/call as its policy says, keeping the
// request under its tag.
function answer(calls: Map<number, Message>) {
  return (request: Message): object | undefined => {
    const { tag, policy } = request.params as { tag: number; policy: Policy };
    calls.set(tag, request);
    switch (policy.kind) {
      case "result":
        return { jsonrpc: "2.0", id: request.id, result: policy.value };
      case "error": {
        const { code, message, data } = policy;
        return {
          jsonrpc: "2.0",
          id: request.id,
          error: { code, message, data },
        };
      }
      case "none":
        return undefined;
      case "wrongId":
        return {
          jsonrpc: "2.0",
          id: `wrong-${String(request.id)}`,
          result: null,
        };
    }
  };
}

// Messages no conforming client sends, each with the answer JSON-RPC asks
// for: an error response, under the id a request named or a null one, or
// none.
type Junk = { payload: string; code?: number; id?: string };

const junk: fc.Arbitrary<Junk> = fc.oneof(
  // Not JSON.
  line
    .filter((text) => {
      try {
        JSON.parse(text);
        return false;
      } catch {
        return true;
      }
    })
    .map((payload) => ({ payload, code: -32700 })),
  // JSON, but not a message: a primitive, or a batch, which LSP has no use for.
  fc
    .oneof(
      fc.integer(),
      text,
      fc.boolean(),
      fc.constant(null),
      fc.array(json()),
    )
    .map((value) => ({ payload: JSON.stringify(value), code: -32600 })),
  // A request whose id is neither an integer nor a string, or null.
  fc
    .oneof(
      fc.boolean(),
      fc.double({ noInteger: true, noDefaultInfinity: true, noNaN: true }),
      fc.constant(null),
      fc.array(fc.integer()),
      fc.dictionary(text, fc.integer()),
    )
    .map((id) => ({
      payload: JSON.stringify({
        jsonrpc: "2.0",
        id,
        method: "test/echo",
        params: [],
      }),
      code: -32600,
    })),
  // A request that names no JSON-RPC 2.0, answered under its id.
  fc.constantFrom({}, { jsonrpc: "1.0" }, { jsonrpc: 2 }).map((envelope) => ({
    payload: JSON.stringify({
      ...envelope,
      id: "junk",
      method: "test/echo",
      params: [],
    }),
    code: -32600,
    id: "junk",
  })),
  // An error response without id, which must not be answered.
  fc
    .constantFrom({ jsonrpc: "2.0", id: null }, { jsonrpc: "2.0" })
    .map((envelope) => ({
      payload: JSON.stringify({
        ...envelope,
        error: { code: -32600, message: "invalid" },
      }),
    })),
);

class Garbage implements Command {
  readonly junk: Junk;
  constructor(junk: Junk) {
    this.junk = junk;
  }
  check = () => true;
  async run(_: Model, real: Real) {
    const { code, id = null } = this.junk;
    real.driver.expectLog(
      code === undefined
        ? /^\[warn\] error response without an id: /
        : /^\[error\] error response: /,
    );
    await real.session.channel.write(frameText(this.junk.payload));
    // The driver answers in order, so its answer to the garbage, if any,
    // comes before the probe's, as a stray.
    const replies = await probe(real.session);
    assert.deepEqual(
      replies.map((reply) => ({ id: reply.id, code: errorOf(reply).code })),
      code === undefined ? [] : [{ id, code }],
    );
  }
  toString = () => `garbage ${JSON.stringify(this.junk.payload)}`;
}

const valid: fc.Arbitrary<Command>[] = [
  params.map((value) => new Echo(value)),
  fc.integer({ min: 0, max: 20 }).map((ms) => new Sleep(ms)),
  fc.nat().map((pick) => new Cancel(pick)),
  fc
    .tuple(fc.integer({ min: -(2 ** 31), max: 2 ** 31 - 1 }), line)
    .map(([code, message]) => new Fail(code, message)),
  text.map((name) => new UnknownMethod(`unknown/${name}`)),
  fc
    .oneof(fc.record({ ms: text }), text, fc.boolean(), fc.array(text))
    .map((value) => new BadParams(value)),
  text.map((name) => new UnknownNotification(`unknown/${name}`)),
  fc
    .tuple(params, fc.integer({ min: 0, max: 3 }))
    .map(([value, count]) => new Notify(value, count)),
  fc
    .oneof(
      json("small").map((value): Policy => ({ kind: "result", value })),
      fc
        .record(
          {
            code: fc.integer({ min: -(2 ** 31), max: 2 ** 31 - 1 }),
            message: line,
            data: json("small"),
          },
          { requiredKeys: ["code", "message"] },
        )
        .map((error): Policy => ({ kind: "error", ...error })),
      fc.constant<Policy>({ kind: "none" }),
      fc.constant<Policy>({ kind: "wrongId" }),
    )
    .map((policy) => new Call(policy)),
];

async function runCommands(
  t: TestContext,
  commands: Iterable<Command>,
): Promise<void> {
  const calls = new Map<number, Message>();
  const [driver, session] = await Session.spawn(t, "jsonrpc_driver", {
    answer: answer(calls),
  });
  await driver.reported(async () => {
    const real: Real = {
      driver,
      session,
      cancelled: new Set(),
      settled: [],
      calls,
      nextTag: 0,
    };
    await fc.asyncModelRun(() => ({ model: { sleeping: [] }, real }), commands);
    await Promise.all(real.settled);
    assert.deepEqual(session.strays, []);
    await session.finish(driver);
  });
}

test("protocol_holds_in_random_use", { timeout: FUZZ_TIMEOUT }, (t) =>
  fuzz(
    fc.asyncProperty(fc.commands(valid, { maxCommands: 30 }), (commands) =>
      runCommands(t, commands),
    ),
  ),
);

test(
  "garbage_is_answered_as_jsonrpc_specifies",
  { timeout: FUZZ_TIMEOUT },
  (t) =>
    fuzz(
      fc.asyncProperty(
        fc.commands([...valid, junk.map((message) => new Garbage(message))], {
          maxCommands: 30,
        }),
        (commands) => runCommands(t, commands),
      ),
    ),
);
