// lsp_stub_server under random use: fast-check draws an initialize request and
// a sequence of client messages, every method the metaModel lets a client
// send with params drawn from its metaModel type (protocol_values.ts), and
// checks on a raw channel that:
//
// - every request gets exactly one response, with its id: a result of the
//   method's metaModel result type for what the stub serves, MethodNotFound
//   for the rest;
// - the notifications the stub serves decode, with no warning in its log, and
//   didOpen publishes diagnostics of their metaModel type;
// - the stub stays alive, a probe after each message is answered, and after
//   shutdown and exit it exits with 0.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import fc from "fast-check";

import {
  loadMetaModel,
  Schema,
  type Type,
} from "../../../../scripts/lsp/metamodel.ts";
import type { Driver } from "../../../harness/driver.ts";
import { FUZZ_TIMEOUT, fuzz, within } from "../../../harness/fuzz.ts";
import { errorOf } from "../../harness/raw.ts";
import { Session } from "../../harness/session.ts";
import { ProtocolValues } from "../harness/protocol_values.ts";

const model = await loadMetaModel();
const schema = new Schema(model);
const values = new ProtocolValues(schema);

// What lsp_stub_server answers, besides the lifecycle.
const SERVED_REQUESTS = new Set([
  "textDocument/codeAction",
  "textDocument/codeLens",
  "textDocument/completion",
  "textDocument/declaration",
  "textDocument/definition",
  "textDocument/documentHighlight",
  "textDocument/documentLink",
  "textDocument/documentSymbol",
  "textDocument/foldingRange",
  "textDocument/formatting",
  "textDocument/hover",
  "textDocument/implementation",
  "textDocument/inlayHint",
  "textDocument/prepareRename",
  "textDocument/rangeFormatting",
  "textDocument/references",
  "textDocument/rename",
  "textDocument/selectionRange",
  "textDocument/signatureHelp",
  "textDocument/typeDefinition",
  "workspace/symbol",
]);
const SERVED_NOTIFICATIONS = new Set([
  "textDocument/didOpen",
  "textDocument/didChange",
  "textDocument/didClose",
  "textDocument/didSave",
]);
const LIFECYCLE = new Set(["initialize", "initialized", "shutdown", "exit"]);

function resultType(method: string): Type {
  const request = schema.requests.find((item) => item.method === method);
  assert.ok(request?.result);
  return request.result;
}

const reference = (name: string): Type => ({ kind: "reference", name });

type Sent = { method: string; params: unknown; request: boolean };

// A message a client may send, with params of its type.
const message: fc.Arbitrary<Sent> = fc.oneof(
  ...[
    ...model.requests.map((item) => ({ ...item, request: true })),
    ...model.notifications.map((item) => ({ ...item, request: false })),
  ]
    .filter(
      (item) =>
        item.messageDirection !== "serverToClient" &&
        !LIFECYCLE.has(item.method),
    )
    .map(({ method, params, request }) => {
      assert.ok(!Array.isArray(params));
      const drawn =
        params === undefined ? fc.constant(undefined) : values.of(params);
      return drawn.map((value): Sent => ({ method, params: value, request }));
    }),
);

async function probe(session: Session): Promise<void> {
  // A notification's decoding fails in the log only; the probe comes after.
  const response = await within(
    session.request("workspace/symbol", { query: "" }),
    "probe",
  );
  assert.ok(values.reads(resultType("workspace/symbol"), response.result));
  assert.deepEqual(session.strays, []);
}

async function send(
  session: Session,
  driver: Driver,
  sent: Sent,
): Promise<void> {
  const where = `${sent.method} ${JSON.stringify(sent.params)}`;
  if (!sent.request) {
    if (!SERVED_NOTIFICATIONS.has(sent.method)) {
      driver.expectLog(/^\[warn\] unhandled notification: /);
    }
    const published = session.notifications.length;
    session.notify(sent.method, sent.params);
    await probe(session);
    for (const note of session.notifications.slice(published)) {
      assert.equal(note.method, "textDocument/publishDiagnostics", where);
      assert.ok(
        values.reads(reference("PublishDiagnosticsParams"), note.params),
        where,
      );
    }
    return;
  }
  const response = await within(
    session.request(sent.method, sent.params),
    `answer to ${where}`,
  );
  if (SERVED_REQUESTS.has(sent.method)) {
    assert.ok(!("error" in response), `${where}: ${JSON.stringify(response)}`);
    assert.ok(
      values.reads(resultType(sent.method), response.result),
      `${where}: the result ${JSON.stringify(response.result)} is not of the method's type`,
    );
  } else {
    driver.expectLog(/^\[error\] error response: method not found: /);
    assert.equal(errorOf(response).code, -32601, where);
  }
  await probe(session);
}

async function run(
  t: TestContext,
  initialize: unknown,
  messages: Sent[],
): Promise<void> {
  // The stub asks the client to create progress only for file:///progress,
  // which the drawn text is too short to be, so the session answers nothing.
  const [driver, session] = await Session.spawn(t, "lsp_stub_server");
  await driver.reported(async () => {
    const initialized = await within(
      session.request("initialize", initialize),
      "initialize",
    );
    assert.ok(values.reads(reference("InitializeResult"), initialized.result));
    session.notify("initialized", {});
    for (const sent of messages) {
      await send(session, driver, sent);
    }
    const shutdown = await within(session.request("shutdown"), "shutdown");
    assert.equal(shutdown.result, null);
    session.notify("exit");
    await within(session.ended, "end of the stub's output");
    await driver.expectExit(0);
  });
}

test("stub_serves_random_messages", { timeout: FUZZ_TIMEOUT }, (t) =>
  fuzz(
    fc.asyncProperty(
      values.of(reference("InitializeParams")),
      fc.array(message, { maxLength: 8 }),
      (initialize, messages) => run(t, initialize, messages),
    ),
  ),
);
