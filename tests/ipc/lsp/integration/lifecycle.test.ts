// The server's lifecycle: what initialize reports, and how shutdown and exit
// end the server. Those two are watched on the raw wire, where what the server
// does not send shows too.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import type { ResponseMessage } from "vscode-jsonrpc";
import { TextDocumentSyncKind } from "vscode-languageserver-protocol";

import { Driver } from "../../harness/driver.ts";
import type { RawChannel } from "../../harness/raw.ts";
import { withStub } from "./stub_client.ts";

async function initialized(t: TestContext): Promise<[Driver, RawChannel]> {
  const driver = await Driver.spawn(t, "lsp_stub_server");
  const wire = driver.raw();
  wire.send({
    jsonrpc: "2.0",
    id: 1,
    method: "initialize",
    params: { processId: null, rootUri: null, capabilities: {} },
  });
  const { id, result } = (await wire.receive()) as ResponseMessage;
  assert.equal(id, 1);
  assert.ok(result);
  wire.send({ jsonrpc: "2.0", method: "initialized", params: {} });
  return [driver, wire];
}

test(
  "initialize_reports_capabilities",
  withStub(async (stub) => {
    const { capabilities, serverInfo } = stub.initializeResult;
    assert.ok(capabilities.hoverProvider);
    assert.ok(capabilities.completionProvider);
    assert.ok(capabilities.definitionProvider);
    assert.ok(capabilities.referencesProvider);
    assert.ok(capabilities.documentSymbolProvider);
    assert.ok(capabilities.documentFormattingProvider);
    assert.ok(capabilities.codeActionProvider);
    assert.equal(capabilities.textDocumentSync, TextDocumentSyncKind.Full);
    assert.equal(serverInfo?.name, "stub-server");
  }),
);

test("exit_after_shutdown_exits_zero", async (t) => {
  const [driver, wire] = await initialized(t);
  wire.send({ jsonrpc: "2.0", id: 2, method: "shutdown" });
  assert.deepEqual(await wire.receive(), {
    jsonrpc: "2.0",
    id: 2,
    result: null,
  });
  wire.send({ jsonrpc: "2.0", method: "exit" });
  // No answer: the server closes its output and ends.
  assert.equal(await wire.receive(), undefined);
  await driver.expectExit(0);
});

test("exit_without_shutdown_exits_one", async (t) => {
  const [driver, wire] = await initialized(t);
  wire.send({ jsonrpc: "2.0", method: "exit" });
  assert.equal(await wire.receive(), undefined);
  await driver.expectExit(1);
});
