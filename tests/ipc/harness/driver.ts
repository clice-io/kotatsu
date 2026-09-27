// Drivers are the programs integration tests spawn. kota_add_integration_tests
// (tests/CMakeLists.txt) builds each one and passes its path in KOTA_<NAME>. A
// test talks to a driver over its stdio, through a JSON-RPC connection or on
// the raw wire, and ends by checking how the driver exited.

import assert from "node:assert/strict";
import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { once } from "node:events";
import { existsSync } from "node:fs";
import type { TestContext } from "node:test";

import {
  createMessageConnection,
  StreamMessageReader,
  StreamMessageWriter,
  type MessageConnection,
} from "vscode-jsonrpc/node";

import { RawChannel } from "./raw.ts";

// Each sanitizer ends a report with its SUMMARY line; UBSan starts one with
// "runtime error:".
const SANITIZER_REPORT = /SUMMARY: \w*Sanitizer|runtime error:/;

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
   * exit, because it failed, the driver is killed and its stderr printed.
   */
  static async spawn(t: TestContext, name: string): Promise<Driver> {
    const driver = new Driver(name, spawn(driverPath(name), { stdio: "pipe" }));
    t.after(() => driver.#abandon());
    await once(driver.#child, "spawn");
    return driver;
  }

  /** A JSON-RPC connection over the driver's stdio, not yet listening. */
  connect(): MessageConnection {
    const connection = createMessageConnection(
      new StreamMessageReader(this.#child.stdout),
      new StreamMessageWriter(this.#child.stdin),
    );
    // Pending requests fail only when the connection is disposed; disposing
    // it as soon as the driver closes its output fails them at once rather
    // than at the test's timeout.
    connection.onClose(() => connection.dispose());
    return connection;
  }

  raw(): RawChannel {
    return new RawChannel(this.#child.stdout, this.#child.stdin);
  }

  /** Waits for the driver to exit, and checks it exited with `code` and without a sanitizer report. */
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
    assert.deepEqual(
      exit,
      { code, signal: null },
      `exited ${JSON.stringify(exit)}; ${stderr}`,
    );
  }

  #abandon(): void {
    if (this.#checked) {
      return;
    }
    this.#child.kill();
    process.stderr.write(`${this.name}'s stderr:\n${this.#stderr}\n`);
  }
}
