// Document notifications: opening a document publishes the stub's
// diagnostics, and the others leave the server serving requests. A
// notification the server cannot decode shows as a warning in its log, which
// fails the case.

import assert from "node:assert/strict";
import { test } from "node:test";

import {
  DiagnosticSeverity,
  DidChangeTextDocumentNotification,
  DidCloseTextDocumentNotification,
  DidOpenTextDocumentNotification,
  DidSaveTextDocumentNotification,
} from "vscode-languageserver-protocol";

import { POSITION, TEST_URI, withStub } from "../harness/stub_client.ts";

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
    const diagnostics = await stub.diagnostics(TEST_URI);
    assert.equal(diagnostics.length, 1);
    assert.equal(diagnostics[0].message, "stub warning");
    assert.equal(diagnostics[0].severity, DiagnosticSeverity.Warning);
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
    assert.notEqual(await stub.hover(), null);
  }),
);

test(
  "did_close_keeps_serving",
  withStub(async (stub) => {
    await stub.connection.sendNotification(
      DidCloseTextDocumentNotification.type,
      { textDocument: { uri: TEST_URI } },
    );
    assert.notEqual(await stub.hover(), null);
  }),
);

test(
  "did_save_keeps_serving",
  withStub(async (stub) => {
    await stub.connection.sendNotification(
      DidSaveTextDocumentNotification.type,
      { textDocument: { uri: TEST_URI } },
    );
    assert.notEqual(await stub.hover(), null);
  }),
);
