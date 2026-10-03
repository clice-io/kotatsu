// A stdio transport: what it opens over, and its close, as stdio_close does it
// on pipes of its own: closing with a write to stdout pending, while another
// descriptor holds stdout's pipe, leaves the loop idle.

import assert from "node:assert/strict";
import { closeSync, mkdtempSync, openSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";

import { Driver } from "../../harness/driver.ts";

// A stdin the loop cannot watch, such as the null device or a regular file,
// fails open_stdio on Linux and Windows; macOS reads it to its end.
async function expectStdinNotWatched(driver: Driver): Promise<void> {
  if (process.platform === "darwin") {
    await driver.expectExit(0);
    return;
  }
  driver.expectLog(/^\[error\] open_stdio: /);
  await driver.expectExit(1);
}

test("open_stdio_over_the_null_device_ends_the_driver", async (t) => {
  const driver = await Driver.spawn(t, "jsonrpc_driver", [], {
    stdin: "ignore",
  });
  await expectStdinNotWatched(driver);
});

test("open_stdio_over_a_file_ends_the_driver", async (t) => {
  const dir = mkdtempSync(join(tmpdir(), "kota-transport-"));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const path = join(dir, "input.txt");
  writeFileSync(path, "");
  const file = openSync(path, "r");
  t.after(() => closeSync(file));

  const driver = await Driver.spawn(t, "jsonrpc_driver", [], { stdin: file });
  await expectStdinNotWatched(driver);
});

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
