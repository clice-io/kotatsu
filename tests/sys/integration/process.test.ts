// What sys_probe reads of the processes it runs in and beside, as the test
// started them:
//
// - its arguments come back as they were given, whatever their characters,
//   quotes, backslashes and blanks; on Windows, where the program reads them
//   from its command line, by the CRT's rules and without wildcard expansion;
// - its environment's values, non-ASCII included, and its parent, the test;
// - its executable's path under the name the file has now, none once the
//   file was removed or replaced, and a copy of it started through
//   self_executable after the file was renamed, and on Linux removed or
//   replaced;
// - another process's priority, set and read back, and its memory;
// - on Linux and macOS the title it sets is what ps shows, and its own
//   arguments read the same after;
// - on Windows what write_console writes to a console, and the limits of a
//   job it joins.

import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import {
  copyFileSync,
  mkdtempSync,
  readFileSync,
  realpathSync,
  renameSync,
  rmSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test, type TestContext } from "node:test";

import { Driver, driverPath } from "../../harness/driver.ts";

const tricky = [
  "",
  "a b",
  'a"b',
  'a\\"b',
  "a\\",
  "a b\\",
  "tab\there",
  "café 中 \u{1F600}",
  "*.cpp",
  "'foo*'",
];

/** What the driver printed, one JSON value. */
async function printed(driver: Driver): Promise<unknown> {
  return JSON.parse(await driver.output());
}

test("arguments_come_back_as_given", async (t) => {
  const driver = await Driver.spawn(t, "sys_probe", ["arguments", ...tricky]);
  const output = (await printed(driver)) as string[];
  await driver.expectExit(0);
  assert.match(output[0] ?? "", /sys_probe(\.exe)?$/);
  assert.deepEqual(output.slice(1), ["arguments", ...tricky]);
});

test(
  "arguments_split_a_raw_windows_command_line",
  { skip: process.platform !== "win32" && "a POSIX program gets argv" },
  async (t) => {
    const driver = await Driver.spawn(
      t,
      "sys_probe",
      ['arguments "a b"c d\\"e f\\\\"g h" "" "x""y" * ?'],
      { windowsVerbatimArguments: true },
    );
    const output = (await printed(driver)) as string[];
    await driver.expectExit(0);
    // The driver's path, unquoted, may split on a blank.
    assert.deepEqual(output.slice(output.indexOf("arguments")), [
      "arguments",
      "a bc",
      'd"e',
      "f\\g h",
      "",
      'x"y',
      "*",
      "?",
    ]);
  },
);

test("env_values_come_back_as_set", async (t) => {
  const value = "café 中 \u{1F600}";
  const driver = await Driver.spawn(t, "sys_probe", ["env", "KOTA_SYS_PROBE"], {
    env: { ...process.env, KOTA_SYS_PROBE: value },
  });
  const output = await printed(driver);
  await driver.expectExit(0);
  assert.equal(output, value);
});

test("env_of_an_unset_variable_is_null", async (t) => {
  const env = { ...process.env };
  delete env["KOTA_SYS_PROBE"];
  const driver = await Driver.spawn(t, "sys_probe", ["env", "KOTA_SYS_PROBE"], {
    env,
  });
  const output = await printed(driver);
  await driver.expectExit(0);
  assert.equal(output, null);
});

test("parent_pid_is_the_test", async (t) => {
  const driver = await Driver.spawn(t, "sys_probe", ["parent"]);
  const output = await printed(driver);
  await driver.expectExit(0);
  assert.equal(output, process.pid);
});

const exe = process.platform === "win32" ? ".exe" : "";

/**
 * A copy of sys_probe, in a directory of its own, running `moved`: the test
 * renames or removes it.
 */
async function movable(
  t: TestContext,
): Promise<{ driver: Driver; path: string }> {
  const dir = mkdtempSync(join(tmpdir(), "kota-moved-"));
  // A scanner may hold a program that just ran a moment.
  t.after(() => rmSync(dir, { recursive: true, force: true, maxRetries: 5 }));
  const path = join(dir, `first${exe}`);
  copyFileSync(driverPath("sys_probe"), path);
  const driver = await Driver.spawn(t, "sys_probe", ["moved"], {
    program: path,
  });
  return { driver, path };
}

type Moved = { path: string | null; code: number | null };

test("executable_path_follows_a_rename", async (t) => {
  const { driver, path } = await movable(t);
  const lines = driver.jsonLines();
  assert.equal(await lines.receive(), "ready");
  const renamed = join(path, "..", `second${exe}`);
  renameSync(path, renamed);
  driver.stdin.end();
  const output = (await lines.receive()) as Moved;
  await driver.expectExit(0);
  assert.ok(output.path !== null);
  // The native one resolves macOS's /private and Windows's short names.
  assert.equal(realpathSync.native(output.path), realpathSync.native(renamed));
  assert.equal(output.code, 0);
});

/**
 * What a copy of sys_probe running `moved` tells once `update` removed its
 * file or put another in its place: the path names the program no more.
 */
async function afterUpdate(
  t: TestContext,
  update: (path: string) => void,
): Promise<Moved> {
  const { driver, path } = await movable(t);
  const lines = driver.jsonLines();
  assert.equal(await lines.receive(), "ready");
  update(path);
  driver.stdin.end();
  const output = (await lines.receive()) as Moved;
  await driver.expectExit(0);
  return output;
}

const noRemoval = {
  skip:
    process.platform === "win32" &&
    "Windows removes no file a program runs from",
};

// Linux starts the image itself still; macOS has nothing to start it by.
const restarted = process.platform === "linux" ? 0 : null;

test("executable_path_fails_once_the_file_is_removed", noRemoval, async (t) => {
  const output = await afterUpdate(t, (path) => rmSync(path));
  assert.equal(output.path, null);
  assert.equal(output.code, restarted);
});

test(
  "executable_path_fails_once_the_file_is_replaced",
  noRemoval,
  async (t) => {
    const output = await afterUpdate(t, (path) => {
      const update = `${path}.new`;
      copyFileSync(driverPath("sys_probe"), update);
      renameSync(update, path);
    });
    assert.equal(output.path, null);
    assert.equal(output.code, restarted);
  },
);

test("priority_of_another_process_is_set_and_read", async (t) => {
  const waiting = await Driver.spawn(t, "sys_probe", ["wait"]);
  assert.equal(await waiting.jsonLines().receive(), "ready");
  const other = await Driver.spawn(t, "sys_probe", [
    "other",
    String(waiting.pid),
  ]);
  const output = (await printed(other)) as {
    priority: number;
    resident: number;
  };
  await other.expectExit(0);
  waiting.stdin.end();
  await waiting.expectExit(0);
  // Priority::BelowNormal: nice 10, or Windows's BELOW_NORMAL class.
  assert.equal(output.priority, 1);
  assert.ok(output.resident > 0);
});

test(
  "title_is_what_ps_shows",
  { skip: process.platform === "win32" && "Windows has no process title" },
  async (t) => {
    const title = "kota-title-test-long";
    const driver = await Driver.spawn(t, "sys_probe", ["title", title, "1"]);
    // The arguments read after the title went over them are those given.
    const read = (await driver.jsonLines().receive()) as string[];
    assert.deepEqual(read.slice(1), ["title", title, "1"]);
    if (process.platform === "linux") {
      const cmdline = readFileSync(`/proc/${driver.pid}/cmdline`, "latin1");
      assert.equal(cmdline.replace(/\0+$/, ""), title);
      // top's name, cut to 15 bytes.
      const comm = readFileSync(`/proc/${driver.pid}/comm`, "latin1");
      assert.equal(comm, `${title.slice(0, 15)}\n`);
    } else {
      const command = execFileSync(
        "ps",
        ["-ww", "-o", "command=", "-p", String(driver.pid)],
        {
          encoding: "utf8",
        },
      );
      assert.equal(command.trim(), title);
    }
    driver.stdin.end();
    await driver.expectExit(0);
  },
);

test(
  "title_is_cut_to_the_arguments",
  {
    skip:
      process.platform !== "linux" && "only Linux tells the arguments' bytes",
  },
  async (t) => {
    const args = ["title", "kota", "1000"];
    const driver = await Driver.spawn(t, "sys_probe", args);
    await driver.jsonLines().receive();
    // The arguments' strings, each with its NUL, the program's path first.
    const program = process.env["KOTA_SYS_PROBE"] ?? "";
    const area = [program, ...args].reduce(
      (size, arg) => size + Buffer.byteLength(arg) + 1,
      0,
    );
    const cmdline = readFileSync(`/proc/${driver.pid}/cmdline`, "latin1");
    assert.equal(cmdline.length, area);
    assert.equal(
      cmdline.replace(/\0+$/, ""),
      "kota".repeat(1000).slice(0, area - 1),
    );
    driver.stdin.end();
    await driver.expectExit(0);
  },
);

test(
  "write_console_writes_utf16_to_a_console",
  { skip: process.platform !== "win32" && "only Windows consoles take UTF-16" },
  async (t) => {
    // The second runs past the 2048 units write_console converts at a time.
    for (const text of ["café ☃ ", "0123456789".repeat(250)]) {
      const driver = await Driver.spawn(t, "sys_probe", ["console", text]);
      const output = await printed(driver);
      await driver.expectExit(0);
      // The byte that is no UTF-8 shows as U+FFFD.
      assert.equal(output, `${text}\u{FFFD}!`);
    }
  },
);

test(
  "a_job_limits_memory_and_parallelism",
  { skip: process.platform !== "win32" && "only Windows has job objects" },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["job"]);
    const output = (await printed(driver)) as {
      limit: number;
      parallelism: number;
    };
    await driver.expectExit(0);
    assert.equal(output.limit, 2 ** 30);
    assert.equal(output.parallelism, 1);
  },
);
