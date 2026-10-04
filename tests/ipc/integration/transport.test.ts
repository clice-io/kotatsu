// A stdio transport: what it opens over, and its close, as stdio_close does it
// on pipes of its own: closing with a write to stdout pending, while another
// descriptor holds stdout's pipe, leaves the loop idle.

import assert from "node:assert/strict";
import {
  closeSync,
  mkdtempSync,
  openSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";

import { Driver } from "../../harness/driver.ts";

// open_stdio takes a pipe, a console or a socket as stdin: a file or a
// device fails it, whether or not the loop could wait on it.
function expectOpenStdioFailed(driver: Driver): Promise<void> {
  driver.expectLog(/^\[error\] open_stdio: /);
  return driver.expectExit(1);
}

test("open_stdio_over_a_file_fails", async (t) => {
  const dir = mkdtempSync(join(tmpdir(), "kota-transport-"));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const path = join(dir, "input.txt");
  writeFileSync(path, "");
  const file = openSync(path, "r");
  // The child has its own copy once it runs.
  const driver = await Driver.spawn(t, "jsonrpc_driver", [], { stdin: file });
  closeSync(file);
  await expectOpenStdioFailed(driver);
});

test("open_stdio_over_the_null_device_fails", async (t) => {
  const driver = await Driver.spawn(t, "jsonrpc_driver", [], {
    stdin: "ignore",
  });
  await expectOpenStdioFailed(driver);
});

// stdout is only written to: the null device, which node opens for reading
// and writing, takes it.
test(
  "open_stdio_with_stdout_on_the_null_device_runs",
  {
    skip:
      process.platform === "win32"
        ? "Windows opens a pipe's handle only"
        : false,
  },
  async (t) => {
    const driver = await Driver.spawn(t, "jsonrpc_driver", [], {
      stdout: "ignore",
    });
    driver.stdin.end();
    await driver.expectExit(0);
  },
);

// The driver sets its pipes up with POSIX calls.
const skip = process.platform === "win32" ? "POSIX pipes only" : false;

test("close_with_a_write_pending_leaves_the_loop_idle", { skip }, async (t) => {
  const driver = await Driver.spawn(t, "stdio_close");
  await driver.expectExit(0);
  const idle = /^idle cpu: (\d+) ms$/m.exec(driver.stderr);
  assert.ok(idle, `no idle cpu time in:\n${driver.stderr}`);
  // A loop woken for good spends the whole 300 ms wait on the CPU.
  assert.ok(Number(idle[1]) < 100, driver.stderr);
});
