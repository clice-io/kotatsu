// jsonrpc_driver against the client VS Code uses, vscode-jsonrpc: each of the
// driver's methods as that client sees it. What no conforming client sends,
// and close_output, are checked on the raw wire.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import {
  CancellationTokenSource,
  ErrorCodes,
  ResponseError,
  type MessageConnection,
} from "vscode-jsonrpc/node";

import { Driver } from "../harness/driver.ts";
import { frame, frameText, type Message } from "../harness/raw.ts";
import { Session } from "../harness/session.ts";

async function connected(t: TestContext): Promise<[Driver, MessageConnection]> {
  const driver = await Driver.spawn(t, "jsonrpc_driver");
  const connection = driver.connect();
  return [driver, connection];
}

/** Ends the driver's input and checks that it exits with 0. */
async function finish(
  driver: Driver,
  connection: MessageConnection,
): Promise<void> {
  connection.end();
  await driver.expectExit(0);
}

test("echo_answers_its_params", async (t) => {
  const [driver, connection] = await connected(t);
  connection.listen();
  const params = {
    text: "é\u0000😀",
    list: [1, -2.5, 1e300, null, true],
    nested: {},
  };
  assert.deepEqual(await connection.sendRequest("test/echo", params), params);
  await finish(driver, connection);
});

test("fail_answers_its_error", async (t) => {
  const [driver, connection] = await connected(t);
  connection.listen();
  driver.expectLog(/^\[error\] error response: out of cheese$/);
  await assert.rejects(
    connection.sendRequest("test/fail", {
      code: -1234,
      message: "out of cheese",
    }),
    { code: -1234, message: "out of cheese" },
  );
  await finish(driver, connection);
});

test("unknown_method_fails", async (t) => {
  const [driver, connection] = await connected(t);
  connection.listen();
  driver.expectLog(
    /^\[error\] error response: method not found: test\/unknown$/,
  );
  await assert.rejects(connection.sendRequest("test/unknown", {}), {
    code: ErrorCodes.MethodNotFound,
  });
  await finish(driver, connection);
});

test("request_with_wrong_params_fails", async (t) => {
  const [driver, connection] = await connected(t);
  connection.listen();
  driver.expectLog(
    /^\[warn\] request 'test\/sleep' params deserialization failed: /,
  );
  driver.expectLog(/^\[error\] error response: /);
  await assert.rejects(connection.sendRequest("test/sleep", { ms: "soon" }), {
    code: ErrorCodes.InvalidParams,
  });
  await finish(driver, connection);
});

test("cancelled_sleep_fails", async (t) => {
  const [driver, connection] = await connected(t);
  connection.listen();
  driver.expectLog(/^\[error\] error response: request cancelled$/);
  const source = new CancellationTokenSource();
  const sleep = connection.sendRequest(
    "test/sleep",
    { ms: 600_000 },
    source.token,
  );
  source.cancel();
  await assert.rejects(sleep, { code: -32800 });
  await finish(driver, connection);
});

test("notifications_arrive_before_the_answer", async (t) => {
  const [driver, connection] = await connected(t);
  const notes: unknown[] = [];
  connection.onNotification("test/note", (params) => {
    notes.push(params);
  });
  connection.listen();
  const answer = await connection.sendRequest("test/notify", {
    method: "test/note",
    params: { n: 1 },
    count: 3,
  });
  assert.equal(answer, null);
  assert.deepEqual(notes, [{ n: 1 }, { n: 1 }, { n: 1 }]);
  await finish(driver, connection);
});

test("call_answers_with_the_client_result", async (t) => {
  const [driver, connection] = await connected(t);
  connection.onRequest(
    "client/add",
    ({ a, b }: { a: number; b: number }) => a + b,
  );
  connection.listen();
  const outcome = await connection.sendRequest("test/call", {
    method: "client/add",
    params: { a: 1, b: 2 },
  });
  assert.deepEqual(outcome, { result: 3 });
  await finish(driver, connection);
});

test("call_answers_with_the_client_error", async (t) => {
  const [driver, connection] = await connected(t);
  connection.onRequest(
    "client/refuse",
    () => new ResponseError(-7, "refused", { why: "test" }),
  );
  connection.listen();
  const outcome = await connection.sendRequest("test/call", {
    method: "client/refuse",
    params: {},
  });
  assert.deepEqual(outcome, {
    error: { code: -7, message: "refused", data: { why: "test" } },
  });
  await finish(driver, connection);
});

test("call_timing_out_cancels_the_client_request", async (t) => {
  const [driver, connection] = await connected(t);
  // The client answers once cancelled, which the driver no longer waits for.
  driver.expectLog(/^\[warn\] orphan response for id=1$/);
  const cancelled = Promise.withResolvers<void>();
  // The $/cancelRequest may be read before the request is handled, when the
  // token is cancelled already and onCancellationRequested never fires.
  connection.onRequest("client/stall", (_, token) => {
    return new Promise((resolve) => {
      const answer = () => {
        cancelled.resolve();
        resolve(null);
      };
      if (token.isCancellationRequested) {
        answer();
      } else {
        token.onCancellationRequested(answer);
      }
    });
  });
  connection.listen();
  const outcome = await connection.sendRequest("test/call", {
    method: "client/stall",
    params: {},
    timeoutMs: 20,
  });
  assert.deepEqual(outcome, {
    error: { code: -32800, message: "request timed out", data: null },
  });
  await cancelled.promise;
  await finish(driver, connection);
});

test("answers_in_flight_at_input_end_are_delivered", async (t) => {
  const [driver, connection] = await connected(t);
  connection.listen();
  const sleeps = Promise.all(
    [1, 2, 3].map((ms) => connection.sendRequest("test/sleep", { ms })),
  );
  connection.end();
  assert.deepEqual(await sleeps, [null, null, null]);
  await driver.expectExit(0);
});

// On the raw wire.

const echo = (id: number) => ({
  jsonrpc: "2.0",
  id,
  method: "test/echo",
  params: [id],
});

async function wired(t: TestContext): Promise<[Driver, Session]> {
  const driver = await Driver.spawn(t, "jsonrpc_driver");
  return [driver, new Session(driver.raw())];
}

/** The driver's answers to `payloads`, which it gives in order, before the probe's. */
async function answersTo(
  session: Session,
  payloads: string[],
): Promise<Message[]> {
  for (const payload of payloads) {
    await session.wire.write(frameText(payload));
  }
  assert.deepEqual((await session.request("test/echo", ["probe"])).result, [
    "probe",
  ]);
  return session.strays.splice(0);
}

async function end(driver: Driver, session: Session): Promise<void> {
  session.wire.end();
  await session.ended;
  await driver.expectExit(0);
}

test("close_output_ends_the_output", async (t) => {
  const [driver, session] = await wired(t);
  const first = session.expect(1);
  session.wire.send(echo(1));
  assert.deepEqual((await first).result, [1]);
  session.notify("test/closeOutput");
  await session.ended;
  await end(driver, session);
});

test("answers_queued_before_close_output_are_delivered", async (t) => {
  const [driver, session] = await wired(t);
  const answers = [1, 2, 3].map((id) => session.expect(id));
  await session.wire.write(
    Buffer.concat([
      ...[1, 2, 3].map((id) => frame(echo(id))),
      frame({ jsonrpc: "2.0", method: "test/closeOutput" }),
    ]),
  );
  assert.deepEqual(
    (await Promise.all(answers)).map((answer) => answer.result),
    [[1], [2], [3]],
  );
  await session.ended;
  await end(driver, session);
});

test("bad_json_answers_parse_error_with_null_id", async (t) => {
  const [driver, session] = await wired(t);
  driver.expectLog(/^\[error\] error response: /);
  const [answer] = await answersTo(session, ["{"]);
  assert.deepEqual(
    [answer?.id, (answer?.error as { code?: unknown })?.code],
    [null, -32700],
  );
  await end(driver, session);
});

// Answered with the id when the message has one it can be answered by, and
// with null otherwise.
test("json_that_is_no_request_answers_invalid_request", async (t) => {
  const [driver, session] = await wired(t);
  driver.expectLog(/^\[error\] error response: /);
  const answered: [string, number | null][] = [
    ["42", null],
    ['"text"', null],
    ["[]", null],
    [JSON.stringify([echo(1)]), null],
    [JSON.stringify({ jsonrpc: "2.0", id: true, method: "test/echo" }), null],
    [JSON.stringify({ jsonrpc: "2.0", id: 1.5, method: "test/echo" }), null],
    [JSON.stringify({ jsonrpc: "2.0", id: 99, method: 5 }), 99],
  ];
  const answers = await answersTo(
    session,
    answered.map(([payload]) => payload),
  );
  assert.deepEqual(
    answers.map((answer) => [
      answer.id,
      (answer.error as { code?: unknown })?.code,
    ]),
    answered.map(([, id]) => [id, -32600]),
  );
  await end(driver, session);
});

test("null_id_request_answers_invalid_request", async (t) => {
  const [driver, session] = await wired(t);
  driver.expectLog(/^\[error\] error response: /);
  const payload = JSON.stringify({
    jsonrpc: "2.0",
    id: null,
    method: "test/echo",
    params: [],
  });
  const answers = await answersTo(session, [payload]);
  assert.deepEqual(
    answers.map((answer) => [
      answer.id,
      (answer.error as { code?: unknown })?.code,
    ]),
    [[null, -32600]],
  );
  await end(driver, session);
});

test("error_response_without_id_is_not_answered", async (t) => {
  const [driver, session] = await wired(t);
  driver.expectLog(/^\[warn\] error response without an id: invalid$/);
  const error = { code: -32600, message: "invalid" };
  const answers = await answersTo(session, [
    JSON.stringify({ jsonrpc: "2.0", id: null, error }),
    JSON.stringify({ jsonrpc: "2.0", error }),
  ]);
  assert.deepEqual(answers, []);
  await end(driver, session);
});

test("malformed_error_response_fails_its_request", async (t) => {
  const driver = await Driver.spawn(t, "jsonrpc_driver");
  const session = new Session(driver.raw(), (request) => ({
    jsonrpc: "2.0",
    id: request.id,
    error: { code: "E1", message: "not an integer code" },
  }));
  const answer = await session.request("test/call", {
    method: "client/refuse",
    params: {},
  });
  assert.ok("error" in (answer.result as object));
  await end(driver, session);
});
