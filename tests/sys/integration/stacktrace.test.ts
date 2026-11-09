// What stacktrace_probe writes with write_stacktrace from the handler it
// crashes into:
//
// - a line for each frame, at most 256, the first in end(), which called
//   write_stacktrace;
// - the executable's frames name it with the base its loader tells and
//   offsets inside its executable code;
// - each host marks the interrupted frame at the context's address, right
//   past ntdll's exception dispatcher on Windows and _sigtramp on macOS,
//   with the handler on an alternate stack or the thread's own;
// - recorded return addresses appear in order after faults, aborts and
//   calls through a null pointer, even without a symbolizer;
// - llvm-symbolizer, where it is on PATH, decodes the executable's frames
//   exactly, including the inlined call when DWARF has line information,
//   from a leaf that faulted, which on arm64 made no frame record, and from
//   a function that faulted after a call, each named once;
// - a null call marks address zero as unknown and continues through
//   probe_call to main;
// - a Linux fault before static initialization names the executable through
//   /proc/self/maps and decodes through probe_fault and probe_caller;
// - Windows names a DLL first loaded after startup with its module handle.

import assert from "node:assert/strict";
import { test } from "node:test";

import {
  assertCalls,
  assertFaultCalls,
  assertInterrupted,
  crash,
  decode,
  executableFrames,
  samePath,
  symbolizer,
} from "../harness/stacktrace.ts";

test("stacktrace_of_a_fault_names_the_executable", async (t) => {
  const crashed = await crash(t, "fault");
  // The handler, which called write_stacktrace.
  assert.ok(samePath(crashed.frames[0]?.path, crashed.image.executable));
  executableFrames(crashed);
  await crashed.driver.expectExit(3);
});

test("stacktrace_of_a_fault_marks_the_address_interrupted", async (t) => {
  const crashed = await crash(t, "fault");
  assertInterrupted(crashed);
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

test(
  "stacktrace_of_a_fault_on_the_thread_stack_decodes_to_its_calls",
  {
    skip:
      process.platform === "win32" &&
      "only a POSIX handler may run on an alternate stack",
  },
  async (t) => {
    const crashed = await crash(t, "fault_on_stack");
    assertInterrupted(crashed);
    if (symbolizer !== undefined) {
      const decoded = decode(t, crashed);
      assertFaultCalls(crashed, decoded);
      assertCalls(decoded.names, ["probe_caller", "main"]);
    }
    await crashed.driver.expectExit(3);
  },
);

test("stacktrace_of_a_fault_after_a_call_decodes_to_its_calls", async (t) => {
  const crashed = await crash(t, "fault_after_call");
  assertInterrupted(crashed);
  if (symbolizer !== undefined) {
    const decoded = decode(t, crashed);
    assertFaultCalls(crashed, decoded, "probe_fault_after_call");
    assertCalls(decoded.names, ["probe_caller", "main"]);
  }
  await crashed.driver.expectExit(3);
});

test("stacktrace_of_an_abort_preserves_its_callers", async (t) => {
  const crashed = await crash(t, "abort");
  if (symbolizer !== undefined) {
    assertCalls(decode(t, crashed).names, ["probe_abort", "main"]);
  }
  await crashed.driver.expectExit(3);
});

test("stacktrace_of_a_null_call_walks_past_address_zero", async (t) => {
  const crashed = await crash(t, "call");
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
