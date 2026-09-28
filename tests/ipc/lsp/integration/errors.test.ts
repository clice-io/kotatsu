// Requests that fail reach the client as error responses.

import assert from "node:assert/strict";
import { test } from "node:test";

import {
  ErrorCodes,
  LSPErrorCodes,
  SemanticTokensRequest,
} from "vscode-languageserver-protocol";

import { TEST_URI, withStub } from "../harness/stub_client.ts";

test(
  "hover_error_fails",
  withStub(async (stub) => {
    stub.driver.expectLog(/^\[error\] error response: hover error triggered$/);
    // The stub's hover fails for this URI.
    await assert.rejects(stub.hover("file:///error"), {
      code: LSPErrorCodes.RequestFailed,
      message: "hover error triggered",
    });
  }),
);

test(
  "unregistered_method_fails",
  withStub(async (stub) => {
    stub.driver.expectLog(/^\[error\] error response: method not found: /);
    const tokens = stub.connection.sendRequest(SemanticTokensRequest.type, {
      textDocument: { uri: TEST_URI },
    });
    await assert.rejects(tokens, { code: ErrorCodes.MethodNotFound });
  }),
);
