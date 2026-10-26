// What becomes of the children sys::spawn starts in sys_probe:
//
// - one started exit_with_parent dies with sys_probe, killed (Linux, Windows;
//   macOS has no way to do it);
// - on Linux it dies too with the thread that started it, sys_probe running
//   on;
// - its arguments, argv[0] among them, environment and working directory
//   reach it as given, whatever their characters;
// - its streams may be any of sys_probe's, swapped ones too, and one closed
//   in it alone, sys_probe keeping its own;
// - on Linux it is started through the execve a hook preloaded into
//   sys_probe would see, and a stop its tracer is told of ends nothing;
// - on Windows it is born in a job nested in the one sys_probe is in, as a CI
//   runner puts its processes in one, and inherits no handle of sys_probe's
//   but its standard ones, unless it is to inherit every inheritable one;
// - on Windows a batch file gets its arguments as given, whatever cmd makes
//   of their characters.

import assert from "node:assert/strict";
import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, join } from "node:path";
import { test } from "node:test";

import { Driver, driverPath } from "../../harness/driver.ts";

// sys_probe's child holds sys_probe's stdout, which ends only once both are
// gone.
test(
  "child_exits_with_its_parent",
  { skip: process.platform === "darwin" && "macOS has no way to do it" },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["orphan"]);
    const lines = driver.jsonLines();
    const { pid } = (await lines.receive()) as { pid: number };
    let ended = false;
    // A child the kill did not reach goes with the test.
    t.after(() => {
      if (!ended) {
        process.kill(pid, "SIGKILL");
      }
    });
    await driver.kill();
    assert.equal(await lines.receive(), undefined);
    ended = true;
  },
);

// It would run on for good.
test(
  "child_dies_with_the_thread_that_started_it",
  {
    skip:
      process.platform !== "linux" &&
      "only Linux ties PR_SET_PDEATHSIG to a thread",
  },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["orphan-thread"]);
    const output = (await driver.jsonLines().receive()) as { signal: number };
    await driver.expectExit(0);
    assert.equal(output.signal, 9);
  },
);

test("arguments_environment_and_directory_keep_their_characters", async (t) => {
  const cwd = mkdtempSync(join(tmpdir(), "kota-ünï-測試-"));
  t.after(() => rmSync(cwd, { recursive: true, force: true }));
  const value = "välue 値 ✓";
  const argument = 'ärgüment 引数 "quoted" \\';
  // Windows writes argv[0] by a rule that has no quotes.
  const arg0 = "ärgv zéro 零";
  const driver = await Driver.spawn(t, "sys_probe", [
    "relay",
    cwd,
    value,
    argument,
    arg0,
  ]);
  const output = (await driver.jsonLines().receive()) as {
    arguments: string[];
    cwd: string;
    value: string;
  };
  await driver.expectExit(0);
  assert.deepEqual(output.arguments, [arg0, "report", argument]);
  assert.equal(output.value, value);
  // The directory's own name: the path to it may be spelled otherwise, a
  // symlink resolved or a short name of Windows's.
  assert.equal(basename(output.cwd), basename(cwd));
});

// Streams 1 and 2 swapped: each is given the number the other takes.
test("streams_may_swap_descriptors", async (t) => {
  const driver = await Driver.spawn(t, "sys_probe", ["swapped"]);
  const output = await driver.output();
  await driver.expectExit(0);
  assert.equal(output.trim(), '"to-stderr"');
  assert.equal(driver.stderr.trim(), '"to-stdout"');
});

// Each in turn: the child has the other two.
test("closed_stream_is_closed_in_the_child_alone", async (t) => {
  for (const index of [0, 1, 2]) {
    const driver = await Driver.spawn(t, "sys_probe", [
      "closed",
      String(index),
    ]);
    const output = await driver.jsonLines().receive();
    await driver.expectExit(0);
    assert.deepEqual(output, { open: 0b111 & ~(1 << index), kept: true });
  }
});

test(
  "child_is_started_through_execve",
  { skip: process.platform !== "linux" && "a Linux hook" },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["hooked"]);
    const output = await driver.jsonLines().receive();
    await driver.expectExit(0);
    assert.equal(output, "1");
  },
);

// A traced child's stop, which wait4 reports to its tracer, ends nothing.
test(
  "traced_child_that_stops_has_not_exited",
  { skip: process.platform !== "linux" && "a Linux tracer" },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["traced"]);
    const output = (await driver.jsonLines().receive()) as {
      running: boolean;
      signal: number;
    };
    await driver.expectExit(0);
    assert.equal(output.running, true);
    assert.equal(output.signal, 9);
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

test(
  "child_inherits_every_inheritable_handle_when_asked",
  { skip: process.platform !== "win32" && "a Windows option" },
  async (t) => {
    const driver = await Driver.spawn(t, "sys_probe", ["handles", "all"]);
    const output = (await driver.jsonLines().receive()) as {
      inherited: boolean;
    };
    await driver.expectExit(0);
    assert.equal(output.inherited, true);
  },
);

// Rust's cases (tests/ui/std/windows-bat-args.rs), and more of cmd's
// characters. None ends with an empty argument, which the third script
// cannot tell from none.
const BATCH_ARGUMENTS = [
  ["a", "b"],
  ["c is for cat", "d is for dog"],
  ['"', ' "'],
  ["\\", "\\"],
  [">file.txt"],
  ["whoami.exe"],
  ["&a.exe"],
  ["&echo hello "],
  ["&echo hello", "&whoami", ">file.txt"],
  ["!TMP!"],
  ["key=value"],
  ['"key=value"'],
  ["key = value"],
  ['key=["value"]'],
  ["", "a=b"],
  ['key="foo bar"'],
  ['key=["my_value]'],
  ['key=["my_value","other-value"]'],
  ["key\\=value"],
  ['key="&whoami"'],
  ['key="value"=5'],
  ['key=[">file.txt"]'],
  ["%hello"],
  ["%PATH%"],
  ["%%cd:~,%"],
  ["%PATH%PATH%"],
  ['">file.txt'],
  ['abc"&echo hello'],
  ['123">file.txt'],
  ['"&echo hello&whoami.exe'],
  ['hello^"world', "hello &echo oh no >file.txt"],
  ["%%", "^", "<", ">", "|", "(", ")", "!", "a b"],
  ["trailing\\", "", "C:\\dir with space\\"],
  ['a\\"b', '\\"', 'a\\\\"b c'],
  ["a\tb", "a,b", "a;b", "="],
  ["ünï 測試 ✓", "caf\u00e9\u0085", "測試"],
];

// Each passes on what it was given as Rust's tests' scripts do: all at once,
// one by one, and one by one, quoted; the last runs by its name with the
// dots and spaces after it that Windows drops.
const BATCH_SCRIPTS = [
  { file: "all.bat", runAs: "all.bat", passed: "%*" },
  { file: "each.bat", runAs: "each.bat", passed: "%1 %2 %3 %4 %5 %6 %7 %8 %9" },
  {
    file: "quoted.cmd",
    runAs: "quoted.cmd",
    passed: '"%~1" "%~2" "%~3" "%~4" "%~5" "%~6" "%~7" "%~8" "%~9"',
  },
  { file: "dotted.bat", runAs: "dotted.bat .. ", passed: "%*" },
];

for (const { file, runAs, passed } of BATCH_SCRIPTS) {
  test(
    `batch_file_gets_its_arguments_as_given_${file.replace(".", "_")}`,
    { skip: process.platform !== "win32" && "Windows runs batch files" },
    async (t) => {
      const directory = mkdtempSync(join(tmpdir(), "kota-batch-"));
      t.after(() => rmSync(directory, { recursive: true, force: true }));
      writeFileSync(
        join(directory, file),
        `@"${driverPath("sys_probe")}" arguments ${passed}\r\n`,
      );
      for (const args of BATCH_ARGUMENTS) {
        const driver = await Driver.spawn(
          t,
          "sys_probe",
          ["spawn", join(directory, runAs), ...args],
          { cwd: directory },
        );
        const output = await driver.output();
        await driver.expectExit(0);
        // One line, of the probe the script started: cmd ran nothing else.
        const lines = output.split("\n").filter((line) => line !== "");
        assert.equal(lines.length, 1, `output of ${JSON.stringify(args)}`);
        const received = (JSON.parse(lines[0]) as string[]).slice(2);
        // The script that quotes each passes on nine.
        while (received.length > args.length && received.at(-1) === "") {
          received.pop();
        }
        assert.deepEqual(received, args);
        // Where the commands of Rust's cases would write.
        assert.equal(existsSync(join(directory, "file.txt")), false);
      }
    },
  );
}
