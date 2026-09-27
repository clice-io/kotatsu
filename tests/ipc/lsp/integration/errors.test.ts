// Requests that fail reach the client as error responses.

import assert from "node:assert/strict";
import { test } from "node:test";

import {
  ErrorCodes,
  HoverRequest,
  SemanticTokensRequest,
} from "vscode-languageserver-protocol";

import { withStub } from "./stub_client.ts";

const POSITION = { line: 0, character: 0 };

test(
  "hover_error_fails",
  withStub(async (stub) => {
    // The stub's hover fails for this URI.
    const hover = stub.connection.sendRequest(HoverRequest.type, {
      textDocument: { uri: "file:///error" },
      position: POSITION,
    });
    await assert.rejects(hover, {
      code: ErrorCodes.InvalidRequest,
      message: "hover error triggered",
    });
  }),
);

test(
  "unregistered_method_fails",
  withStub(async (stub) => {
    const tokens = stub.connection.sendRequest(SemanticTokensRequest.type, {
      textDocument: { uri: "file:///tmp/test.cpp" },
    });
    await assert.rejects(tokens, { code: ErrorCodes.MethodNotFound });
  }),
);
