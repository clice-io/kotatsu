// A stdio transport's close, as stdio_close does it on pipes of its own:
// closing with a write to stdout pending, while another descriptor holds
// stdout's pipe, leaves the loop idle.

import assert from "node:assert/strict";
import { test } from "node:test";

import { Driver } from "../../harness/driver.ts";

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
