// What stacktrace_probe writes with write_stacktrace from the handler it
// crashes into:
//
// - a line for each frame, in its format, the first the handler's own;
// - the executable's frames name it with the base its loader tells and
//   offsets inside its image;
// - but on macOS, which walks frame pointers, the frame the fault interrupted
//   is marked, at the address the handler's context says; on Windows past
//   ntdll's exception dispatcher;
// - llvm-symbolizer, where it is on PATH, decodes the executable's frames
//   into the functions they are in, the inlined one too, from the one that
//   faulted to main;
// - on Windows the frames of a DLL loaded after the start name it.

import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, delimiter, join } from "node:path";
import { test, type TestContext } from "node:test";

import { Driver } from "../../harness/driver.ts";

const LINE =
  /^0x([0-9A-F]{16})(?:, (.+)\(0x([0-9A-F]{16})\) \+ 0x([0-9A-F]+) byte\(s\)| <unknown module>)( \(interrupted\))?$/;

type Frame = {
  address: bigint;
  path?: string;
  base?: bigint;
  offset?: bigint;
  interrupted: boolean;
};

type Image = { executable: string; base: string; low: string; high: string };

type Crash = {
  image: Image;
  fault?: bigint;
  library?: string;
  frames: Frame[];
};

/** What the probe printed as `command` made it crash. */
async function crash(t: TestContext, command: string): Promise<Crash> {
  const driver = await Driver.spawn(t, "stacktrace_probe", [command]);
  const lines = (await driver.output())
    .split("\n")
    .map((line) => line.replace(/\r$/, ""))
    .filter((line) => line !== "");
  await driver.expectExit(3);
  const result: Crash = { image: JSON.parse(lines.shift() ?? ""), frames: [] };
  while (lines[0]?.startsWith("{")) {
    const line = JSON.parse(lines.shift() ?? "");
    if (line.fault !== undefined) {
      result.fault = BigInt(line.fault);
    }
    if (line.library !== undefined) {
      result.library = line.library;
    }
  }
  for (const line of lines) {
    const match = LINE.exec(line);
    assert.ok(match !== null, `not a frame: ${line}`);
    const [, address, path, base, offset, interrupted] = match;
    result.frames.push({
      address: BigInt(`0x${address}`),
      path,
      base: base === undefined ? undefined : BigInt(`0x${base}`),
      offset: offset === undefined ? undefined : BigInt(`0x${offset}`),
      interrupted: interrupted !== undefined,
    });
  }
  return result;
}

/** Whether two paths name one file, as the host compares names. */
function samePath(a: string | undefined, b: string): boolean {
  return process.platform === "win32"
    ? a?.toLowerCase() === b.toLowerCase()
    : a === b;
}

/** The executable's frames, after checking their base and offsets. */
function executableFrames({ image, frames }: Crash): Frame[] {
  const own = frames.filter((frame) => samePath(frame.path, image.executable));
  assert.ok(own.length > 0, "no frame names the executable");
  for (const frame of own) {
    assert.equal(frame.base, BigInt(image.base));
    assert.ok(frame.offset !== undefined);
    assert.ok(frame.offset >= BigInt(image.low), `offset ${frame.offset}`);
    assert.ok(frame.offset < BigInt(image.high), `offset ${frame.offset}`);
  }
  return own;
}

function onPath(name: string): string | undefined {
  const suffix = process.platform === "win32" ? ".exe" : "";
  for (const directory of (process.env.PATH ?? "").split(delimiter)) {
    const candidate = join(directory, name + suffix);
    if (directory !== "" && existsSync(candidate)) {
      return candidate;
    }
  }
  return undefined;
}

const symbolizer = onPath("llvm-symbolizer");

/**
 * The functions llvm-symbolizer finds the executable's frames in, each
 * frame's inlined ones first: looked up at the address before a return
 * address, which may be the start of what follows the call.
 */
function decode(t: TestContext, crashed: Crash): string[] {
  assert.ok(symbolizer !== undefined);
  const executable = crashed.image.executable;
  const frames = executableFrames(crashed);
  let object = executable;
  const options: string[] = [];
  if (process.platform === "darwin") {
    // The DWARF stays in the object files, which dsymutil gathers.
    const directory = mkdtempSync(join(tmpdir(), "kotatsu-stacktrace-"));
    t.after(() => rmSync(directory, { recursive: true, force: true }));
    const bundle = join(directory, "probe.dSYM");
    execFileSync(onPath("dsymutil") ?? "dsymutil", [executable, "-o", bundle]);
    object = join(
      bundle,
      "Contents",
      "Resources",
      "DWARF",
      basename(executable),
    );
  }
  if (process.platform === "win32") {
    // The offset is an RVA; llvm-symbolizer takes a PE file's addresses at
    // its preferred base otherwise.
    options.push("--relative-address");
  }
  const addresses = frames.map(
    (frame) =>
      `0x${((frame.offset ?? 0n) - (frame.interrupted ? 0n : 1n)).toString(16)}`,
  );
  const output = execFileSync(
    symbolizer,
    ["--output-style=JSON", `--obj=${object}`, ...options, ...addresses],
    { encoding: "utf8" },
  );
  // An entry for each address, its inlined functions first in Symbol.
  const entries = JSON.parse(output) as {
    Symbol: { FunctionName: string }[];
  }[];
  return entries.flatMap((entry) =>
    entry.Symbol.map((symbol) => symbol.FunctionName),
  );
}

/** Checks that `names` holds functions named after `expected`, in order. */
function assertCalls(names: string[], expected: string[]): void {
  let next = 0;
  for (const name of names) {
    if (next < expected.length && name.includes(expected[next] ?? "")) {
      next += 1;
    }
  }
  assert.equal(
    next,
    expected.length,
    `${expected.join(" < ")} in ${JSON.stringify(names)}`,
  );
}

const walksFramePointers = process.platform === "darwin";

test("stacktrace_of_a_fault_names_the_executable", async (t) => {
  const crashed = await crash(t, "fault");
  // The handler, which called write_stacktrace.
  assert.ok(samePath(crashed.frames[0]?.path, crashed.image.executable));
  executableFrames(crashed);
});

test(
  "stacktrace_of_a_fault_marks_the_address_interrupted",
  { skip: walksFramePointers && "macOS walks frame pointers past the fault" },
  async (t) => {
    const crashed = await crash(t, "fault");
    const marked = crashed.frames.filter((frame) => frame.interrupted);
    assert.equal(marked.length, 1);
    assert.equal(marked[0]?.address, crashed.fault);
    assert.ok(samePath(marked[0]?.path, crashed.image.executable));
  },
);

test(
  "stacktrace_of_a_fault_walks_past_the_exception_dispatcher",
  {
    skip: process.platform !== "win32" && "only Windows dispatches exceptions",
  },
  async (t) => {
    const { frames } = await crash(t, "fault");
    // KiUserExceptionDispatcher's frame, then the one it was called over.
    const marked = frames.findIndex((frame) => frame.interrupted);
    assert.ok(marked > 0);
    const dispatcher = frames[marked - 1]?.path?.toLowerCase() ?? "";
    assert.ok(dispatcher.endsWith("\\ntdll.dll"), dispatcher);
  },
);

test(
  "stacktrace_of_a_fault_decodes_to_its_calls",
  { skip: symbolizer === undefined && "no llvm-symbolizer on PATH" },
  async (t) => {
    const names = decode(t, await crash(t, "fault"));
    assertCalls(names, [
      ...(walksFramePointers ? [] : ["probe_fault"]),
      "probe_inlined",
      "probe_caller",
      "main",
    ]);
  },
);

test(
  "stacktrace_of_an_abort_decodes_to_its_calls",
  { skip: symbolizer === undefined && "no llvm-symbolizer on PATH" },
  async (t) => {
    const crashed = await crash(t, "abort");
    assertCalls(decode(t, crashed), ["probe_abort", "main"]);
  },
);

test(
  "stacktrace_names_a_dll_loaded_after_the_start",
  { skip: process.platform !== "win32" && "the late DLL is Windows'" },
  async (t) => {
    const crashed = await crash(t, "late");
    assert.ok(crashed.library !== undefined);
    // probe_callback's frame, then the DLL's that called it.
    const library = crashed.frames.findIndex((frame) =>
      samePath(frame.path, crashed.library ?? ""),
    );
    assert.ok(library > 0, "no frame names the DLL");
    if (symbolizer !== undefined) {
      assertCalls(decode(t, crashed), ["probe_callback", "probe_late", "main"]);
    }
  },
);
