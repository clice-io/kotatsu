// jsonrpc_driver against the client VS Code uses, vscode-jsonrpc: each of the
// driver's methods as that client sees it. What no conforming client sends,
// and close_output, are checked on a raw channel.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import {
  CancellationTokenSource,
  ErrorCodes,
  ResponseError,
  type MessageConnection,
} from "vscode-jsonrpc/node";

import { Driver } from "../../harness/driver.ts";
import { connect } from "../harness/connection.ts";
import { echo, probe } from "../harness/jsonrpc_driver.ts";
import {
  errorOf,
  frame,
  frameText,
  resultOf,
  type Message,
} from "../harness/raw.ts";
import { Session } from "../harness/session.ts";

async function connected(t: TestContext): Promise<[Driver, MessageConnection]> {
  const driver = await Driver.spawn(t, "jsonrpc_driver");
  return [driver, connect(driver)];
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
  // The client answers once cancelled, which the driver no longer waits for:
  // it drops the answer, logging it below warn.
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
    error: { code: -32800, message: "request timed out" },
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

// On a raw channel.

function spawn(t: TestContext) {
  return Session.spawn(t, "jsonrpc_driver");
}

/** The driver's answers to `payloads`, which it gives in order, before the probe's. */
async function answersTo(
  session: Session,
  payloads: string[],
): Promise<Message[]> {
  for (const payload of payloads) {
    await session.channel.write(frameText(payload));
  }
  return probe(session);
}

test("close_output_ends_the_output", async (t) => {
  const [driver, session] = await spawn(t);
  const first = session.expect(1);
  session.channel.send(echo(1));
  assert.deepEqual(resultOf(await first), [1]);
  session.notify("test/closeOutput");
  await session.ended;
  await session.finish(driver);
});

// The three handlers start together, once the frames read with them are
// dispatched, and their answers are queued together; the first one out shows
// the others are queued or out when close_output comes.
test("answers_queued_before_close_output_are_delivered", async (t) => {
  const [driver, session] = await spawn(t);
  const answers = [1, 2, 3].map((id) => session.expect(id));
  await session.channel.write(
    Buffer.concat([1, 2, 3].map((id) => frame(echo(id)))),
  );
  assert.deepEqual(resultOf(await answers[0]), [1]);
  session.notify("test/closeOutput");
  assert.deepEqual((await Promise.all(answers)).map(resultOf), [[1], [2], [3]]);
  await session.ended;
  await session.finish(driver);
});

// A handler starts once the frames read with its request are dispatched, so
// a close_output read with the requests closes the output before they are
// answered.
test("close_output_read_with_requests_comes_before_their_answers", async (t) => {
  const [driver, session] = await spawn(t);
  await session.channel.write(
    Buffer.concat([
      ...[1, 2, 3].map((id) => frame(echo(id))),
      frame({ jsonrpc: "2.0", method: "test/closeOutput" }),
    ]),
  );
  await session.ended;
  await session.finish(driver);
  assert.deepEqual(session.strays, []);
});

test("bad_json_answers_parse_error_with_null_id", async (t) => {
  const [driver, session] = await spawn(t);
  driver.expectLog(/^\[error\] error response: /);
  const [answer] = await answersTo(session, ["{"]);
  assert.deepEqual(
    [answer?.id, answer && errorOf(answer).code],
    [null, -32700],
  );
  await session.finish(driver);
});

// Answered with the id when the message has one it can be answered by, and
// with null otherwise.
test("json_that_is_no_request_answers_invalid_request", async (t) => {
  const [driver, session] = await spawn(t);
  const answered: [string, number | null][] = [
    ["42", null],
    ['"text"', null],
    ["[]", null],
    [JSON.stringify([echo(1)]), null],
    [JSON.stringify({ jsonrpc: "2.0", id: true, method: "test/echo" }), null],
    [JSON.stringify({ jsonrpc: "2.0", id: 1.5, method: "test/echo" }), null],
    [JSON.stringify({ jsonrpc: "2.0", id: 99, method: 5 }), 99],
  ];
  driver.expectLog(/^\[error\] error response: /, answered.length);
  const answers = await answersTo(
    session,
    answered.map(([payload]) => payload),
  );
  assert.deepEqual(
    answers.map((answer) => [answer.id, errorOf(answer).code]),
    answered.map(([, id]) => [id, -32600]),
  );
  await session.finish(driver);
});

test("null_id_request_answers_invalid_request", async (t) => {
  const [driver, session] = await spawn(t);
  driver.expectLog(/^\[error\] error response: /);
  const payload = JSON.stringify({
    jsonrpc: "2.0",
    id: null,
    method: "test/echo",
    params: [],
  });
  const answers = await answersTo(session, [payload]);
  assert.deepEqual(
    answers.map((answer) => [answer.id, errorOf(answer).code]),
    [[null, -32600]],
  );
  await session.finish(driver);
});

test("error_response_without_id_is_not_answered", async (t) => {
  const [driver, session] = await spawn(t);
  driver.expectLog(/^\[warn\] error response without an id: invalid$/, 2);
  const error = { code: -32600, message: "invalid" };
  const answers = await answersTo(session, [
    JSON.stringify({ jsonrpc: "2.0", id: null, error }),
    JSON.stringify({ jsonrpc: "2.0", error }),
  ]);
  assert.deepEqual(answers, []);
  await session.finish(driver);
});

test("malformed_error_response_fails_its_request", async (t) => {
  const [driver, session] = await Session.spawn(t, "jsonrpc_driver", {
    answer: (request) => ({
      jsonrpc: "2.0",
      id: request.id,
      error: { code: "E1", message: "not an integer code" },
    }),
  });
  const answer = await session.request("test/call", {
    method: "client/refuse",
    params: {},
  });
  assert.ok("error" in (resultOf(answer) as object));
  await session.finish(driver);
});
