// Document notifications: opening a document publishes the stub's
// diagnostics, and the others leave the server serving requests.

import assert from "node:assert/strict";
import { test } from "node:test";

import {
  DiagnosticSeverity,
  DidChangeTextDocumentNotification,
  DidCloseTextDocumentNotification,
  DidOpenTextDocumentNotification,
  DidSaveTextDocumentNotification,
  HoverRequest,
} from "vscode-languageserver-protocol";

import { withStub, type StubClient } from "./stub_client.ts";

const TEST_URI = "file:///tmp/test.cpp";
const POSITION = { line: 0, character: 0 };

function hover(stub: StubClient) {
  return stub.connection.sendRequest(HoverRequest.type, {
    textDocument: { uri: TEST_URI },
    position: POSITION,
  });
}

test(
  "did_open_publishes_diagnostics",
  withStub(async (stub) => {
    await stub.connection.sendNotification(
      DidOpenTextDocumentNotification.type,
      {
        textDocument: {
          uri: TEST_URI,
          languageId: "cpp",
          version: 1,
          text: "int main() {}",
        },
      },
    );
    const [diagnostic] = await stub.diagnostics(TEST_URI);
    assert.equal(diagnostic.message, "stub warning");
    assert.equal(diagnostic.severity, DiagnosticSeverity.Warning);
  }),
);

test(
  "did_change_keeps_serving",
  withStub(async (stub) => {
    await stub.connection.sendNotification(
      DidChangeTextDocumentNotification.type,
      {
        textDocument: { uri: TEST_URI, version: 2 },
        contentChanges: [
          { range: { start: POSITION, end: POSITION }, text: "// changed\n" },
        ],
      },
    );
    assert.notEqual(await hover(stub), null);
  }),
);

test(
  "did_close_keeps_serving",
  withStub(async (stub) => {
    await stub.connection.sendNotification(
      DidCloseTextDocumentNotification.type,
      {
        textDocument: { uri: TEST_URI },
      },
    );
    assert.notEqual(await hover(stub), null);
  }),
);

test(
  "did_save_keeps_serving",
  withStub(async (stub) => {
    await stub.connection.sendNotification(
      DidSaveTextDocumentNotification.type,
      {
        textDocument: { uri: TEST_URI },
      },
    );
    assert.notEqual(await hover(stub), null);
  }),
);
