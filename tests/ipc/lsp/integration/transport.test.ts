// The transport under load: a large message, requests in flight together,
// notifications interleaved with requests, and what the server sends as its
// input ends.

import assert from "node:assert/strict";
import { test } from "node:test";

import {
  CompletionRequest,
  DidChangeTextDocumentNotification,
  DidOpenTextDocumentNotification,
  MarkupKind,
} from "vscode-languageserver-protocol";

import { POSITION, StubClient, withStub } from "../harness/stub_client.ts";

const STUB_HOVER = { kind: MarkupKind.Markdown, value: "stub hover" };

function didOpen(stub: StubClient, uri: string) {
  return stub.connection.sendNotification(
    DidOpenTextDocumentNotification.type,
    {
      textDocument: { uri, languageId: "cpp", version: 1, text: "void f() {}" },
    },
  );
}

test(
  "large_uri_is_served",
  withStub(async (stub) => {
    const hover = await stub.hover(`file:///${"a".repeat(4000)}`);
    assert.deepEqual(hover?.contents, STUB_HOVER);
  }),
);

test(
  "concurrent_requests_are_all_answered",
  withStub(async (stub) => {
    const uris = Array.from({ length: 20 }, (_, i) => `file:///test_${i}.cpp`);
    const hovers = await Promise.all(uris.map((uri) => stub.hover(uri)));
    assert.deepEqual(
      hovers.map((hover) => hover?.contents),
      uris.map(() => STUB_HOVER),
    );
  }),
);

test(
  "interleaved_messages_are_all_served",
  withStub(async (stub) => {
    const uri = "file:///interleave.cpp";
    await didOpen(stub, uri);
    assert.notEqual(await stub.hover(uri), null);
    await stub.connection.sendNotification(
      DidChangeTextDocumentNotification.type,
      {
        textDocument: { uri, version: 2 },
        contentChanges: [
          { range: { start: POSITION, end: POSITION }, text: "// added\n" },
        ],
      },
    );
    const list = await stub.connection.sendRequest(CompletionRequest.type, {
      textDocument: { uri },
      position: POSITION,
    });
    assert.notEqual(list, null);
    const diagnostics = await stub.diagnostics(uri);
    assert.equal(diagnostics[0].message, "stub warning");
  }),
);

// The server answers what it read before its input ended, then exits; without
// a shutdown request, with 1.

test("answers_at_input_end_are_delivered", async (t) => {
  const stub = await StubClient.start(t);
  const uris = Array.from({ length: 20 }, (_, i) => `file:///test_${i}.cpp`);
  const hovers = Promise.all(uris.map((uri) => stub.hover(uri)));
  stub.connection.end();
  assert.deepEqual(
    (await hovers).map((hover) => hover?.contents),
    uris.map(() => STUB_HOVER),
  );
  await stub.driver.expectExit(1);
});

test("notifications_at_input_end_are_delivered", async (t) => {
  const stub = await StubClient.start(t);
  const uri = "file:///last.cpp";
  await didOpen(stub, uri);
  stub.connection.end();
  const diagnostics = await stub.diagnostics(uri);
  assert.equal(diagnostics[0].message, "stub warning");
  await stub.driver.expectExit(1);
});
