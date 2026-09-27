// A client of lsp_stub_server that stands in for an editor: it initializes the
// server, answers its requests to create progress, and keeps the diagnostics
// and progress the server sends.

import type { TestContext } from "node:test";

import type { MessageConnection } from "vscode-jsonrpc/node";
import {
  ExitNotification,
  InitializedNotification,
  InitializeRequest,
  PublishDiagnosticsNotification,
  ShutdownRequest,
  WorkDoneProgress,
  WorkDoneProgressCreateRequest,
  type Diagnostic,
  type InitializeResult,
  type ProgressToken,
  type WorkDoneProgressBegin,
  type WorkDoneProgressEnd,
  type WorkDoneProgressReport,
} from "vscode-languageserver-protocol";

import { Driver } from "../../harness/driver.ts";

type Progress =
  WorkDoneProgressBegin | WorkDoneProgressReport | WorkDoneProgressEnd;

export class StubClient {
  readonly #driver: Driver;
  readonly connection: MessageConnection;
  initializeResult!: InitializeResult;
  readonly #diagnostics = new Map<string, PromiseWithResolvers<Diagnostic[]>>();
  readonly #progress = new Map<
    ProgressToken,
    PromiseWithResolvers<Progress[]>
  >();

  private constructor(driver: Driver, connection: MessageConnection) {
    this.#driver = driver;
    this.connection = connection;
    connection.onNotification(
      PublishDiagnosticsNotification.type,
      ({ uri, diagnostics }) =>
        promiseFor(this.#diagnostics, uri).resolve(diagnostics),
    );
    connection.onRequest(WorkDoneProgressCreateRequest.type, ({ token }) => {
      const reported: Progress[] = [];
      connection.onProgress(WorkDoneProgress.type, token, (value) => {
        reported.push(value);
        if (value.kind === "end") {
          promiseFor(this.#progress, token).resolve(reported);
        }
      });
    });
  }

  /** Spawns the server and initializes it. */
  static async start(t: TestContext): Promise<StubClient> {
    const driver = await Driver.spawn(t, "lsp_stub_server");
    const client = new StubClient(driver, driver.connect());
    client.connection.listen();
    client.initializeResult = await client.connection.sendRequest(
      InitializeRequest.type,
      {
        processId: null,
        rootUri: null,
        capabilities: {},
      },
    );
    await client.connection.sendNotification(InitializedNotification.type, {});
    return client;
  }

  /** The diagnostics the server publishes first for `uri`. */
  diagnostics(uri: string): Promise<Diagnostic[]> {
    return promiseFor(this.#diagnostics, uri).promise;
  }

  /** What the server reports on the progress `token` it creates, once it ends. */
  progress(token: ProgressToken): Promise<Progress[]> {
    return promiseFor(this.#progress, token).promise;
  }

  /** Shuts the server down and checks it exits with 0. */
  async stop(): Promise<void> {
    await this.connection.sendRequest(ShutdownRequest.type);
    await this.connection.sendNotification(ExitNotification.type);
    await this.#driver.expectExit(0);
  }
}

/**
 * A test body that runs against a started server; the test stops the server
 * after the body, and checks that it exits with 0.
 */
export function withStub(body: (stub: StubClient) => Promise<void>) {
  return async (t: TestContext) => {
    const stub = await StubClient.start(t);
    await body(stub);
    await stub.stop();
  };
}

// The promise under `key`, created by whichever comes first: the server sending
// what resolves it, or the test waiting for it.
function promiseFor<K, V>(
  map: Map<K, PromiseWithResolvers<V>>,
  key: K,
): PromiseWithResolvers<V> {
  let value = map.get(key);
  if (value === undefined) {
    value = Promise.withResolvers();
    map.set(key, value);
  }
  return value;
}
