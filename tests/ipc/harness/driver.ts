// Drivers are the programs integration tests spawn. kota_add_integration_tests
// (tests/CMakeLists.txt) builds each one and passes its path in KOTA_<NAME>. A
// test talks to a driver over its stdio, through a JSON-RPC connection or on
// the raw wire, and ends by checking how the driver exited.

import assert from "node:assert/strict";
import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { once } from "node:events";
import { existsSync } from "node:fs";
import type { TestContext } from "node:test";
import { setTimeout as delay } from "node:timers/promises";

import {
  createMessageConnection,
  StreamMessageReader,
  StreamMessageWriter,
  type MessageConnection,
  type MessageReader,
  type MessageWriter,
} from "vscode-jsonrpc/node";

import { RawChannel } from "./raw.ts";

// Each sanitizer ends a report with its SUMMARY line; UBSan starts one with
// the source location and "runtime error:".
const SANITIZER_REPORT =
  /^SUMMARY: \w*Sanitizer|^\S+:\d+:\d+: runtime error: /m;

// A driver logs through test::stderr_logger (ipc/harness/stderr_logger.h).
const PROBLEM = /^\[(?:warn|error)\] .*$/gm;

function driverPath(name: string): string {
  const variable = `KOTA_${name.toUpperCase()}`;
  const path = process.env[variable];
  if (path === undefined) {
    throw new Error(
      `${variable} is not set; run the integration tests through ctest (pixi run integration-test)`,
    );
  }
  if (!existsSync(path)) {
    throw new Error(
      `${variable} is ${path}, which does not exist; build ${name} first`,
    );
  }
  return path;
}

type Exit = { code: number | null; signal: NodeJS.Signals | null };

export class Driver {
  readonly name: string;
  readonly #child: ChildProcessWithoutNullStreams;
  readonly #exited: Promise<Exit>;
  readonly #stderrEnded: Promise<unknown>;
  readonly #expectedLogs: RegExp[] = [];
  #stderr = "";
  #checked = false;

  private constructor(name: string, child: ChildProcessWithoutNullStreams) {
    this.name = name;
    this.#child = child;
    this.#exited = new Promise((resolve) =>
      child.once("exit", (code, signal) => resolve({ code, signal })),
    );
    child.stderr.setEncoding("utf8");
    child.stderr.on("data", (text: string) => (this.#stderr += text));
    this.#stderrEnded = once(child.stderr, "end");
    // A driver that exits before reading all its input breaks the pipe; how
    // it exited is what the test checks.
    child.stdin.on("error", () => {});
  }

  /**
   * Spawns the driver `name`. If the test ends before it checked the driver's
   * exit, because it failed, the driver is killed and how it ended printed.
   */
  static async spawn(t: TestContext, name: string): Promise<Driver> {
    const driver = new Driver(name, spawn(driverPath(name), { stdio: "pipe" }));
    t.after(() => driver.#abandon());
    await once(driver.#child, "spawn");
    return driver;
  }

  /**
   * A JSON-RPC connection over the driver's stdio, not yet listening. Its
   * end() closes the driver's input after everything sent before it. Once
   * the driver's output ends and every message it sent has been handled, the
   * connection is disposed, which fails the requests still waiting for an
   * answer.
   */
  connect(): MessageConnection {
    let received = 0;
    let handled = 0;
    let ended = false;
    const reader = new StreamMessageReader(this.#child.stdout);
    const counted: MessageReader = {
      onError: reader.onError,
      onClose: reader.onClose,
      onPartialMessage: reader.onPartialMessage,
      listen: (callback) =>
        reader.listen((message) => {
          received += 1;
          callback(message);
        }),
      dispose: () => reader.dispose(),
    };
    // vscode-jsonrpc writes each message on a later tick, one after another,
    // but ends the stream at once; waiting for the last write keeps end() from
    // cutting off what was sent before it.
    const writer = new StreamMessageWriter(this.#child.stdin);
    let written = Promise.resolve();
    const ordered: MessageWriter = {
      onError: writer.onError,
      onClose: writer.onClose,
      write: (message) => (written = writer.write(message)),
      end: () => {
        const end = () => writer.end();
        written.then(end, end);
      },
      dispose: () => writer.dispose(),
    };
    const connection = createMessageConnection(counted, ordered, undefined, {
      messageStrategy: {
        handleMessage: (message, next) => {
          try {
            next(message);
          } finally {
            handled += 1;
            disposeIfDrained();
          }
        },
      },
    });
    // The connection's own onClose also fires when the driver's input closes,
    // and messages may still wait in its queue; only the reader tells that
    // nothing more comes.
    reader.onClose(() => {
      ended = true;
      disposeIfDrained();
    });
    function disposeIfDrained() {
      if (ended && handled === received) {
        connection.dispose();
      }
    }
    return connection;
  }

  raw(): RawChannel {
    return new RawChannel(this.#child.stdout, this.#child.stdin);
  }

  /** Lets the driver log a warn or error line matching `pattern`. */
  expectLog(pattern: RegExp): void {
    this.#expectedLogs.push(pattern);
  }

  /**
   * Waits for the driver to exit, and checks it exited with `code`, without a
   * sanitizer report and without a warn or error line it was not expected to
   * log.
   */
  async expectExit(code: number): Promise<void> {
    const exit = await this.#exited;
    await this.#stderrEnded;
    this.#checked = true;
    const stderr = `${this.name}'s stderr:\n${this.#stderr}`;
    assert.doesNotMatch(
      this.#stderr,
      SANITIZER_REPORT,
      `a sanitizer report; ${stderr}`,
    );
    const unexpected = (this.#stderr.match(PROBLEM) ?? []).filter(
      (line) => !this.#expectedLogs.some((pattern) => pattern.test(line)),
    );
    assert.deepEqual(unexpected, [], `unexpected log lines; ${stderr}`);
    assert.deepEqual(
      exit,
      { code, signal: null },
      `exited ${JSON.stringify(exit)}; ${stderr}`,
    );
  }

  /** What the driver has written to stderr so far. */
  get stderr(): string {
    return this.#stderr;
  }

  /**
   * Kills the driver, for a test that reports its failure itself, and waits
   * for it to end.
   */
  async kill(): Promise<void> {
    this.#checked = true;
    await this.#end();
  }

  async #abandon(): Promise<void> {
    if (this.#checked) {
      return;
    }
    const exit = await this.#end();
    process.stderr.write(
      `${this.name}, left unchecked by its test: ${JSON.stringify(exit)}; its stderr:\n${this.#stderr}\n`,
    );
  }

  async #end(): Promise<Exit | string> {
    this.#child.kill("SIGKILL");
    // Bounded: something the driver started may hold its stderr open.
    const timeout = delay(5000, "still running", { ref: false });
    const exit = await Promise.race([this.#exited, timeout]);
    await Promise.race([this.#stderrEnded, timeout]);
    return exit;
  }
}
