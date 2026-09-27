// A client of lsp_stub_server (ipc/lsp/integration/drivers) that stands in for
// an editor: it initializes the server, answers its requests to create
// progress, and keeps the diagnostics and progress the server sends.

import type { TestContext } from "node:test";

import type { MessageConnection } from "vscode-jsonrpc/node";
import {
  ExitNotification,
  HoverRequest,
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

export const TEST_URI = "file:///tmp/test.cpp";
export const POSITION = { line: 0, character: 0 };

type Progress =
  WorkDoneProgressBegin | WorkDoneProgressReport | WorkDoneProgressEnd;

export class StubClient {
  readonly driver: Driver;
  readonly connection: MessageConnection;
  initializeResult!: InitializeResult;
  readonly #diagnostics = new Mailbox<string, Diagnostic[]>();
  readonly #progress = new Mailbox<ProgressToken, Progress[]>();

  private constructor(driver: Driver) {
    this.driver = driver;
    this.connection = driver.connect();
    this.connection.onNotification(
      PublishDiagnosticsNotification.type,
      ({ uri, diagnostics }) => this.#diagnostics.deliver(uri, diagnostics),
    );
    this.connection.onRequest(
      WorkDoneProgressCreateRequest.type,
      ({ token }) => {
        const reported: Progress[] = [];
        this.connection.onProgress(WorkDoneProgress.type, token, (value) => {
          reported.push(value);
          if (value.kind === "end") {
            this.#progress.deliver(token, reported);
          }
        });
      },
    );
    this.connection.onDispose(() => {
      this.#diagnostics.close();
      this.#progress.close();
    });
    this.connection.listen();
  }

  /** Spawns the server and initializes it. */
  static async start(t: TestContext): Promise<StubClient> {
    const client = new StubClient(await Driver.spawn(t, "lsp_stub_server"));
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

  /** The stub's hover, the same for every document but file:///error. */
  hover(uri = TEST_URI) {
    return this.connection.sendRequest(HoverRequest.type, {
      textDocument: { uri },
      position: POSITION,
    });
  }

  /** The diagnostics the server publishes first for `uri`. */
  diagnostics(uri: string): Promise<Diagnostic[]> {
    return this.#diagnostics.wait(uri);
  }

  /** What the server reports on the progress `token` it creates, once it ends. */
  progress(token: ProgressToken): Promise<Progress[]> {
    return this.#progress.wait(token);
  }

  /** Shuts the server down and checks it exits with 0. */
  async stop(): Promise<void> {
    await this.connection.sendRequest(ShutdownRequest.type);
    await this.connection.sendNotification(ExitNotification.type);
    await this.driver.expectExit(0);
  }
}

/**
 * A test body run against a started server, which is stopped after the body
 * and must exit with 0.
 */
export function withStub(body: (stub: StubClient) => Promise<void>) {
  return async (t: TestContext) => {
    const stub = await StubClient.start(t);
    await body(stub);
    await stub.stop();
  };
}

// What the server sends under a key, and the test's wait for it, which may
// come first. Once the server's output ends, a wait for what has not come
// fails.
class Mailbox<K, V> {
  readonly #entries = new Map<K, PromiseWithResolvers<V>>();
  #closed = false;

  deliver(key: K, value: V): void {
    this.#entry(key).resolve(value);
  }

  wait(key: K): Promise<V> {
    return this.#entry(key).promise;
  }

  close(): void {
    this.#closed = true;
    for (const entry of this.#entries.values()) {
      entry.reject(new Error("the server's output ended first"));
    }
  }

  #entry(key: K): PromiseWithResolvers<V> {
    let entry = this.#entries.get(key);
    if (entry === undefined) {
      entry = Promise.withResolvers();
      // Handled by the wait, which may start after the rejection.
      entry.promise.catch(() => {});
      if (this.#closed) {
        entry.reject(new Error("the server's output ended first"));
      }
      this.#entries.set(key, entry);
    }
    return entry;
  }
}
