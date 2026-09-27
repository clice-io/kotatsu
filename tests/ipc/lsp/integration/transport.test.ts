// The transport under load: a large message, requests in flight together, and
// notifications interleaved with requests.

import assert from "node:assert/strict";
import { test } from "node:test";

import {
  CompletionRequest,
  DidChangeTextDocumentNotification,
  DidOpenTextDocumentNotification,
  HoverRequest,
  MarkupKind,
} from "vscode-languageserver-protocol";

import { withStub, type StubClient } from "./stub_client.ts";

const POSITION = { line: 0, character: 0 };
const STUB_HOVER = { kind: MarkupKind.Markdown, value: "stub hover" };

function hover(stub: StubClient, uri: string) {
  return stub.connection.sendRequest(HoverRequest.type, {
    textDocument: { uri },
    position: POSITION,
  });
}

test(
  "large_uri_is_served",
  withStub(async (stub) => {
    const answer = await hover(stub, `file:///${"a".repeat(4000)}`);
    assert.deepEqual(answer?.contents, STUB_HOVER);
  }),
);

test(
  "concurrent_requests_are_all_answered",
  withStub(async (stub) => {
    const uris = Array.from({ length: 20 }, (_, i) => `file:///test_${i}.cpp`);
    const answers = await Promise.all(uris.map((uri) => hover(stub, uri)));
    assert.deepEqual(
      answers.map((answer) => answer?.contents),
      uris.map(() => STUB_HOVER),
    );
  }),
);

test(
  "interleaved_messages_are_all_served",
  withStub(async (stub) => {
    const uri = "file:///interleave.cpp";
    await stub.connection.sendNotification(
      DidOpenTextDocumentNotification.type,
      {
        textDocument: {
          uri,
          languageId: "cpp",
          version: 1,
          text: "void f() {}",
        },
      },
    );
    assert.notEqual(await hover(stub, uri), null);
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
    const [diagnostic] = await stub.diagnostics(uri);
    assert.equal(diagnostic.message, "stub warning");
  }),
);
