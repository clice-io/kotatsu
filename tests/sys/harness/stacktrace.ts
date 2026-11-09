// What tests/sys's integration tests share about stacktrace_probe: running
// it into a crash, parsing what it printed, and decoding the executable's
// frames with llvm-symbolizer where it is on PATH.

import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, delimiter, join } from "node:path";
import { type TestContext } from "node:test";

import { Driver } from "../../harness/driver.ts";

const LINE =
  /^0x([0-9A-F]{16})(?:, (.+)\(0x([0-9A-F]{16})\) \+ 0x(0|[1-9A-F][0-9A-F]*) byte\(s\)| <unknown module>)( \(interrupted\))?$/;

export type Frame = {
  address: bigint;
  path?: string;
  base?: bigint;
  offset?: bigint;
  interrupted: boolean;
};

export type Image = {
  executable: string;
  base: string;
  low: string;
  high: string;
  debug: "pdb" | "dwarf";
};

export type Crash = {
  driver: Driver;
  image: Image;
  /** What on_crash's handler was told of: the signal, on Windows the exception's code. */
  signal?: number;
  code?: number;
  fault?: bigint;
  returns: bigint[];
  library?: string;
  loadedBefore?: boolean;
  libraryBase?: bigint;
  frames: Frame[];
};

/** How many return addresses the probes of each command record, but 2. */
const RECORDED: Record<string, number> = {
  abort: 1,
  call: 1,
  crash_trap: 1,
  crash_abort: 1,
  crash_overflow: 0,
  crash_overflow_on_thread: 0,
  crash_two: 0,
};

/** What the probe printed as `command` made it crash. */
export async function crash(
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
    if (line.signal !== undefined) {
      result.signal = line.signal;
      result.code = line.code === undefined ? undefined : Number(line.code);
    }
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
  assert.equal(result.returns.length, RECORDED[command] ?? 2);
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
export function samePath(a: string | undefined, b: string): boolean {
  return process.platform === "win32"
    ? a?.toLowerCase() === b.toLowerCase()
    : a === b;
}

/** The executable's frames, after checking their base and offsets. */
export function executableFrames({ image, frames }: Crash): Frame[] {
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

export const symbolizer = onPath("llvm-symbolizer");

export type Decoded = { names: string[]; hasLines: boolean };

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
export function decode(
  t: TestContext,
  crashed: Crash,
  first: string | null = "end",
): Decoded {
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
  if (first !== null) {
    assert.equal(names[0], first);
  }
  assert.ok(!names.includes("write_stacktrace"));
  return {
    names,
    hasLines: entries.some((entry) =>
      entry.Symbol.some((symbol) => (symbol.Line ?? 0) > 0),
    ),
  };
}

/** Checks that `names` holds exactly the names in `expected`, in order. */
export function assertCalls(names: string[], expected: string[]): void {
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

/**
 * `faulting` once, then probe_caller, through probe_inlined, whose name is
 * required with DWARF lines, and ordered when present.
 */
export function assertFaultCalls(
  { image }: Crash,
  { names, hasLines }: Decoded,
  faulting = "probe_fault",
): void {
  const inlined =
    (image.debug === "dwarf" && hasLines) || names.includes("probe_inlined");
  assert.equal(
    names.filter((name) => name === faulting).length,
    1,
    JSON.stringify(names),
  );
  assertCalls(names, [
    faulting,
    ...(inlined ? ["probe_inlined"] : []),
    "probe_caller",
  ]);
}

/** Checks that one frame is marked, at the address the context says. */
export function assertInterrupted(crashed: Crash): void {
  const marked = crashed.frames.filter((frame) => frame.interrupted);
  assert.equal(marked.length, 1);
  assert.ok(crashed.fault !== undefined);
  assert.equal(marked[0]?.address, crashed.fault);
  assert.ok(samePath(marked[0]?.path, crashed.image.executable));
}
