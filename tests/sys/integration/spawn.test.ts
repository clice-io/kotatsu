// What becomes of the children sys::spawn starts in sys_probe:
//
// - one started exit_with_parent dies with sys_probe, killed (Linux, Windows;
//   macOS has no way to do it);
// - on Linux it dies too with the thread that started it, sys_probe running
//   on;
// - on Windows it is born in a job nested in the one sys_probe is in, as a CI
//   runner puts its processes in one, and inherits no handle of sys_probe's
//   but its standard ones.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { setTimeout as delay } from "node:timers/promises";

import { Driver } from "../../harness/driver.ts";

/**
 * Whether process `pid` runs. A zombie does not, on Linux: a child killed
 * while sys_probe runs on stays one until sys_probe exits.
 */
function exists(pid: number): boolean {
  try {
    process.kill(pid, 0);
  } catch {
    return false;
  }
  if (process.platform !== "linux") {
    return true;
  }
  try {
    const stat = readFileSync(`/proc/${pid}/stat`, "utf8");
    return stat[stat.lastIndexOf(")") + 2] !== "Z";
  } catch {
    return false;
  }
}

/**
 * Whether process `pid` is gone within the test's time. Nothing tells of
 * another process's end: the test looks until it has, a killed child left
 * to init to reap.
 */
async function gone(pid: number): Promise<boolean> {
  for (let tries = 0; tries < 1000; ++tries) {
    if (!exists(pid)) {
      return true;
    }
    await delay(10);
  }
  return false;
}

test(
  "child_exits_with_its_parent",
  { skip: process.platform === "darwin" && "macOS has no way to do it" },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["orphan"]);
    const { pid } = (await driver.jsonLines().receive()) as { pid: number };
    assert.ok(exists(pid));
    await driver.kill();
    assert.ok(await gone(pid));
  },
);

test(
  "child_dies_with_the_thread_that_started_it",
  {
    skip:
      process.platform !== "linux" &&
      "only Linux ties PR_SET_PDEATHSIG to a thread",
  },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["orphan-thread"]);
    const { pid } = (await driver.jsonLines().receive()) as { pid: number };
    assert.ok(await gone(pid));
    driver.stdin.end();
    await driver.expectExit(0);
  },
);

test(
  "child_nests_in_the_job_its_parent_is_in",
  { skip: process.platform !== "win32" && "Windows has jobs" },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["nested"]);
    const output = (await driver.jsonLines().receive()) as { in_job: boolean };
    await driver.expectExit(0);
    assert.equal(output.in_job, true);
  },
);

test(
  "child_inherits_no_handle_but_its_standard_ones",
  { skip: process.platform !== "win32" && "Windows lists the handles" },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["handles"]);
    const output = (await driver.jsonLines().receive()) as {
      inherited: boolean;
    };
    await driver.expectExit(0);
    assert.equal(output.inherited, false);
  },
);
