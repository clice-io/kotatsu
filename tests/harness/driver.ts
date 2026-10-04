// Drivers are the programs integration tests spawn. kota_add_integration_tests
// (tests/CMakeLists.txt) builds each one and passes its path in KOTA_<NAME>. A
// test talks to a driver over its stdio, in the driver's protocol (ipc's
// harness has JSON-RPC's) or in JSON lines, and ends by checking how the
// driver exited.

import assert from "node:assert/strict";
import { spawn, type ChildProcess } from "node:child_process";
import { once } from "node:events";
import { existsSync } from "node:fs";
import type { Readable, Writable } from "node:stream";
import type { TestContext } from "node:test";
import { setTimeout as delay } from "node:timers/promises";

import { JsonLines } from "./jsonl.ts";

// Each sanitizer ends a report with its SUMMARY line; UBSan starts one with
// the source location and "runtime error:".
const SANITIZER_REPORT =
  /^SUMMARY: \w*Sanitizer|^\S+:\d+:\d+: runtime error: /m;

// A driver logs a line to stderr as "[level] message", as ipc's
// test::stderr_logger (ipc/harness/stderr_logger.h) does.
const PROBLEM = /^\[(?:warn|error)\] .*$/gm;

/** Warn or error lines a test lets the driver log. */
type Allowance = { pattern: RegExp; left: number };

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

/**
 * The driver's stdin or stdout: a pipe the test uses, the null device
 * ("ignore"), or a descriptor of the test's, such as a file it opened.
 */
type Stdio = "pipe" | "ignore" | number;

export class Driver {
  readonly name: string;
  readonly #child: ChildProcess;
  readonly #exited: Promise<Exit>;
  readonly #stderrEnded: Promise<unknown>;
  readonly #allowances: Allowance[] = [];
  #stderr = "";
  #checked = false;

  private constructor(name: string, child: ChildProcess) {
    assert.ok(child.stderr !== null);
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
    child.stdin?.on("error", () => {});
  }

  /**
   * Spawns the driver `name` with `args`, its stdin `stdin` and its stdout
   * `stdout`. If the test ends before it checked the driver's exit, because
   * it failed, the driver is killed and how it ended printed.
   */
  static async spawn(
    t: TestContext,
    name: string,
    args: string[] = [],
    { stdin = "pipe", stdout = "pipe" }: { stdin?: Stdio; stdout?: Stdio } = {},
  ): Promise<Driver> {
    const driver = new Driver(
      name,
      spawn(driverPath(name), args, { stdio: [stdin, stdout, "pipe"] }),
    );
    t.after(() => driver.#abandon());
    await once(driver.#child, "spawn");
    return driver;
  }

  /** The driver's input, for a channel in its protocol. */
  get stdin(): Writable {
    assert.ok(this.#child.stdin !== null, "the driver's stdin is no pipe");
    return this.#child.stdin;
  }

  /** The driver's output, for a channel in its protocol. */
  get stdout(): Readable {
    assert.ok(this.#child.stdout !== null, "the driver's stdout is no pipe");
    return this.#child.stdout;
  }

  jsonLines(): JsonLines {
    return new JsonLines(this.stdout, this.stdin);
  }

  /**
   * Lets the driver log `count` warn or error lines matching `pattern`: one
   * for what the test is about to do, unless it says more, and Infinity for
   * input that may make the driver log anything. A line takes the first
   * allowance it matches that has one left.
   */
  expectLog(pattern: RegExp, count = 1): void {
    this.#allowances.push({ pattern, left: count });
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
    const unexpected = (this.#stderr.match(PROBLEM) ?? []).filter((line) => {
      const allowance = this.#allowances.find(
        ({ pattern, left }) => left > 0 && pattern.test(line),
      );
      if (allowance === undefined) {
        return true;
      }
      allowance.left -= 1;
      return false;
    });
    assert.deepEqual(unexpected, [], `unexpected log lines; ${stderr}`);
    assert.deepEqual(
      exit,
      { code, signal: null },
      `exited ${JSON.stringify(exit)}; ${stderr}`,
    );
  }

  /** Everything the driver writes to stdout, once it closes it. */
  async output(): Promise<string> {
    const stdout = this.stdout;
    stdout.setEncoding("utf8");
    let text = "";
    for await (const chunk of stdout) {
      text += chunk;
    }
    return text;
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

  /**
   * Runs `body`, which ends with expectExit(); if it fails, kills the driver
   * and fails with its stderr, for a test the driver outlives, such as a run
   * of a fuzz test.
   */
  async reported<T>(body: () => Promise<T>): Promise<T> {
    try {
      return await body();
    } catch (error) {
      await this.kill();
      throw new Error(
        `${String(error)}\n${this.name}'s stderr:\n${this.#stderr}`,
        {
          cause: error,
        },
      );
    }
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
