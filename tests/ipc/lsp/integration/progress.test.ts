// Work done progress the server reports to the client while it answers.

import assert from "node:assert/strict";
import { test } from "node:test";

import { CompletionRequest } from "vscode-languageserver-protocol";

import { withStub } from "../harness/stub_client.ts";

test(
  "completion_reports_progress",
  withStub(async (stub) => {
    const progress = stub.progress("test-progress");
    // The stub's completion creates the progress "test-progress" for this URI,
    // and reports on it before it answers.
    const list = await stub.connection.sendRequest(CompletionRequest.type, {
      textDocument: { uri: "file:///progress" },
      position: { line: 0, character: 0 },
    });
    assert.notEqual(list, null);
    const kinds = (await progress).map(({ kind }) => kind);
    assert.deepEqual(kinds, ["begin", "report", "end"]);
  }),
);
