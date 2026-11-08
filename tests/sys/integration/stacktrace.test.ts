// What stacktrace_probe writes with write_stacktrace from the handler it
// crashes into:
//
// - a line for each frame, at most 256, the first in end(), which called
//   write_stacktrace;
// - the executable's frames name it with the base its loader tells and
//   offsets inside its executable code;
// - Linux and Windows mark the interrupted frame at the context's address,
//   right past ntdll's exception dispatcher on Windows; macOS walks frame
//   pointers and marks none;
// - recorded return addresses appear in order after faults, aborts and
//   calls through a null pointer, even without a symbolizer;
// - llvm-symbolizer, where it is on PATH, decodes the executable's frames
//   exactly, including the inlined call when DWARF has line information;
// - a null call on Linux and Windows marks address zero as unknown and
//   continues through probe_call to main;
// - a Linux fault before static initialization names the executable through
//   /proc/self/maps and decodes through probe_fault and probe_caller;
// - Windows names a DLL first loaded after startup with its module handle.

import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, delimiter, join } from "node:path";
import { test, type TestContext } from "node:test";

import { Driver } from "../../harness/driver.ts";

const LINE =
  /^0x([0-9A-F]{16})(?:, (.+)\(0x([0-9A-F]{16})\) \+ 0x(0|[1-9A-F][0-9A-F]*) byte\(s\)| <unknown module>)( \(interrupted\))?$/;

type Frame = {
  address: bigint;
  path?: string;
  base?: bigint;
  offset?: bigint;
  interrupted: boolean;
};

type Image = {
  executable: string;
  base: string;
  low: string;
  high: string;
  debug: "pdb" | "dwarf";
};

type Crash = {
  driver: Driver;
  image: Image;
  fault?: bigint;
  returns: bigint[];
  library?: string;
  loadedBefore?: boolean;
  libraryBase?: bigint;
  frames: Frame[];
};

/** What the probe printed as `command` made it crash. */
async function crash(
  t: TestContext,
  command: string,
  env?: NodeJS.ProcessEnv,
): Promise<Crash> {
  const driver = await Driver.spawn(t, "stacktrace_probe", [command], { env });
  const lines = (await driver.output())
    .split("\n")
    .map((line) => line.replace(/\r$/, ""))
    .filter((line) => line !== "");
  const result: Crash = {
    driver,
    image: JSON.parse(lines.shift() ?? ""),
    returns: [],
    frames: [],
  };
  assert.ok(["pdb", "dwarf"].includes(result.image.debug));
  while (lines[0]?.startsWith("{")) {
    const line = JSON.parse(lines.shift() ?? "");
    if (line.fault !== undefined) {
      result.fault = BigInt(line.fault);
    }
    if (line.returns !== undefined) {
      result.returns = line.returns.map((address: string) => BigInt(address));
    }
    if (line.library !== undefined) {
      result.library = line.library;
      result.loadedBefore = line.loaded_before;
      result.libraryBase = BigInt(line.library_base);
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
  assert.ok(result.frames.length <= 256);
  assert.equal(
    result.returns.length,
    command === "abort" || command === "call" ? 1 : 2,
  );
  let next = 0;
  for (const address of result.returns) {
    const index = result.frames.findIndex(
      (frame, index) => index >= next && frame.address === address,
    );
    assert.ok(
      index >= next,
      `missing return address 0x${address.toString(16)}`,
    );
    next = index + 1;
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

type Decoded = { names: string[]; hasLines: boolean };

/** Just the function's name, without its namespace, type or parameters. */
function functionName(name: string): string {
  return (
    name
      .replace(/(?:\(anonymous namespace\)|`anonymous namespace')::/g, "")
      .replace(/\(.*$/, "")
      .trim()
      .split(/\s+/)
      .at(-1)
      ?.split("::")
      .at(-1) ?? ""
  );
}

/**
 * The functions llvm-symbolizer finds the executable's frames in, each
 * frame's inlined ones first: looked up at the address before a return
 * address, which may be the start of what follows the call.
 */
function decode(t: TestContext, crashed: Crash): Decoded {
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
    execFileSync("dsymutil", [executable, "-o", bundle]);
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
    Error?: unknown;
    Symbol: { FunctionName: string; Line?: number }[];
  }[];
  for (const entry of entries) {
    assert.ok(!("Error" in entry), JSON.stringify(entry));
  }
  const names = entries.flatMap((entry) =>
    entry.Symbol.map((symbol) => functionName(symbol.FunctionName)),
  );
  assert.equal(names[0], "end");
  assert.ok(!names.includes("write_stacktrace"));
  return {
    names,
    hasLines: entries.some((entry) =>
      entry.Symbol.some((symbol) => (symbol.Line ?? 0) > 0),
    ),
  };
}

/** Checks that `names` holds exactly the names in `expected`, in order. */
function assertCalls(names: string[], expected: string[]): void {
  let next = 0;
  for (const name of names) {
    if (next < expected.length && name === expected[next]) {
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

/** Inline names are required with DWARF lines, and ordered when present. */
function assertFaultCalls(
  { image }: Crash,
  { names, hasLines }: Decoded,
): void {
  const inlined =
    (image.debug === "dwarf" && hasLines) || names.includes("probe_inlined");
  assertCalls(names, [
    ...(walksFramePointers ? [] : ["probe_fault"]),
    ...(inlined ? ["probe_inlined"] : []),
    "probe_caller",
  ]);
}

test("stacktrace_of_a_fault_names_the_executable", async (t) => {
  const crashed = await crash(t, "fault");
  // The handler, which called write_stacktrace.
  assert.ok(samePath(crashed.frames[0]?.path, crashed.image.executable));
  executableFrames(crashed);
  await crashed.driver.expectExit(3);
});

test("stacktrace_of_a_fault_marks_the_address_interrupted", async (t) => {
  const crashed = await crash(t, "fault");
  const marked = crashed.frames.filter((frame) => frame.interrupted);
  if (walksFramePointers) {
    assert.equal(marked.length, 0);
  } else {
    assert.equal(marked.length, 1);
    assert.ok(crashed.fault !== undefined);
    assert.equal(marked[0]?.address, crashed.fault);
    assert.ok(samePath(marked[0]?.path, crashed.image.executable));
  }
  await crashed.driver.expectExit(3);
});

test(
  "stacktrace_of_a_fault_walks_past_the_exception_dispatcher",
  {
    skip: process.platform !== "win32" && "only Windows dispatches exceptions",
  },
  async (t) => {
    const crashed = await crash(t, "fault");
    const { frames } = crashed;
    // KiUserExceptionDispatcher's frame, then the one it was called over.
    const marked = frames.findIndex((frame) => frame.interrupted);
    assert.ok(marked > 0);
    const dispatcher = frames[marked - 1]?.path?.toLowerCase() ?? "";
    assert.ok(dispatcher.endsWith("\\ntdll.dll"), dispatcher);
    await crashed.driver.expectExit(3);
  },
);

test(
  "stacktrace_of_a_fault_decodes_to_its_calls",
  { skip: symbolizer === undefined && "no llvm-symbolizer on PATH" },
  async (t) => {
    const crashed = await crash(t, "fault");
    const decoded = decode(t, crashed);
    assertFaultCalls(crashed, decoded);
    assertCalls(decoded.names, ["probe_caller", "main"]);
    await crashed.driver.expectExit(3);
  },
);

test("stacktrace_of_an_abort_preserves_its_callers", async (t) => {
  const crashed = await crash(t, "abort");
  if (symbolizer !== undefined) {
    assertCalls(decode(t, crashed).names, ["probe_abort", "main"]);
  }
  await crashed.driver.expectExit(3);
});

test("stacktrace_of_a_null_call_walks_past_address_zero", async (t) => {
  const crashed = await crash(t, "call");
  // macOS's frame-pointer walk only promises the recorded return address.
  if (!walksFramePointers) {
    const marked = crashed.frames.filter((frame) => frame.interrupted);
    assert.equal(marked.length, 1);
    assert.equal(crashed.fault, 0n);
    assert.equal(marked[0]?.address, 0n);
    assert.equal(marked[0]?.path, undefined);
    const interrupted = crashed.frames.findIndex((frame) => frame.interrupted);
    assert.ok(
      crashed.frames
        .slice(interrupted + 1)
        .some((frame) => frame.address === crashed.returns[0]),
    );
    if (symbolizer !== undefined) {
      assertCalls(decode(t, crashed).names, ["probe_call", "main"]);
    }
  }
  await crashed.driver.expectExit(3);
});

test(
  "stacktrace_before_static_initialization_names_its_calls",
  { skip: process.platform !== "linux" && "only Linux reads /proc/self/maps" },
  async (t) => {
    const crashed = await crash(t, "early", {
      ...process.env,
      KOTATSU_PROBE_EARLY: "1",
    });
    const marked = crashed.frames.filter((frame) => frame.interrupted);
    assert.equal(marked.length, 1);
    assert.ok(crashed.fault !== undefined);
    assert.equal(marked[0]?.address, crashed.fault);
    assert.ok(samePath(marked[0]?.path, crashed.image.executable));
    executableFrames(crashed);
    if (symbolizer !== undefined) {
      assertFaultCalls(crashed, decode(t, crashed));
    }
    await crashed.driver.expectExit(3);
  },
);

test(
  "stacktrace_names_a_dll_loaded_after_the_start",
  { skip: process.platform !== "win32" && "the late DLL is Windows'" },
  async (t) => {
    const crashed = await crash(t, "late");
    assert.ok(crashed.library !== undefined);
    // ASan's runtime loads dbghelp at the start, for its own reports.
    if (crashed.loadedBefore === true) {
      t.skip("dbghelp.dll was loaded at the start");
      return;
    }
    assert.ok(crashed.libraryBase !== undefined);
    // probe_callback's frame, then the DLL's that called it.
    const library = crashed.frames.findIndex((frame) =>
      samePath(frame.path, crashed.library ?? ""),
    );
    assert.ok(library > 0, "no frame names the DLL");
    assert.equal(crashed.frames[library]?.base, crashed.libraryBase);
    if (symbolizer !== undefined) {
      assertCalls(decode(t, crashed).names, [
        "probe_callback",
        "probe_late",
        "main",
      ]);
    }
    await crashed.driver.expectExit(3);
  },
);
