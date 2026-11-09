// What stacktrace_probe's handler, set up with on_crash, is told of and
// writes with write_stacktrace(fd, crash) as it crashes, and how it ends:
//
// - the trace starts at the address the context says the crash
//   interrupted, the one frame marked, in the executable, and decodes to the
//   calls that led there;
// - a fault, a trap and a stack overflow, on the main thread or a Thread,
//   end the process with the signal, on Windows with the exception's code;
// - an abort ends it with SIGABRT, on Windows as abort() does without a
//   handler, where the trace starts in the handler;
// - of two threads that fault at once, one is reported, once, and the
//   process ends with its signal.

import assert from "node:assert/strict";
import { constants } from "node:os";
import { test } from "node:test";

import {
  assertCalls,
  assertFaultCalls,
  assertInterrupted,
  crash,
  type Crash,
  decode,
  symbolizer,
} from "../harness/stacktrace.ts";

const windows = process.platform === "win32";

/** abort()'s exit codes on Windows: a fast fail's, or 3 without one. */
const ABORTED = [0xc0000409, 3];

/** Checks that the process ended as the crash it reported ends it. */
async function expectEnded(crashed: Crash): Promise<void> {
  if (windows) {
    await crashed.driver.expectExit(crashed.code ?? ABORTED);
    return;
  }
  const signal = Object.entries(constants.signals).find(
    ([, number]) => number === crashed.signal,
  )?.[0];
  assert.ok(signal !== undefined, `signal ${crashed.signal}`);
  await crashed.driver.expectSignal(signal as NodeJS.Signals);
}

/** Checks that the trace starts at the address the crash interrupted. */
function assertStartsInterrupted(crashed: Crash): void {
  assertInterrupted(crashed);
  assert.equal(crashed.frames[0]?.interrupted, true);
}

test("crash_of_a_fault_starts_at_its_address", async (t) => {
  const crashed = await crash(t, "crash_fault");
  assertStartsInterrupted(crashed);
  if (symbolizer !== undefined) {
    const decoded = decode(t, crashed, null);
    assertFaultCalls(crashed, decoded);
    assertCalls(decoded.names, ["probe_caller", "main"]);
  }
  await expectEnded(crashed);
});

test("crash_of_a_trap_ends_with_its_signal", async (t) => {
  const crashed = await crash(t, "crash_trap");
  assertStartsInterrupted(crashed);
  if (symbolizer !== undefined) {
    assertCalls(decode(t, crashed, null).names, ["probe_trap", "main"]);
  }
  await expectEnded(crashed);
});

test("crash_of_an_abort_ends_as_abort_does", async (t) => {
  const crashed = await crash(t, "crash_abort");
  assert.equal(crashed.signal, constants.signals.SIGABRT);
  // Windows' SIGABRT has no context: the trace starts in the handler.
  if (!windows) {
    assert.equal(crashed.frames[0]?.interrupted, true);
  }
  if (symbolizer !== undefined) {
    assertCalls(decode(t, crashed, windows ? "reported" : null).names, [
      "probe_abort",
      "main",
    ]);
  }
  await expectEnded(crashed);
});

for (const command of ["crash_overflow", "crash_overflow_on_thread"]) {
  test(`${command}_starts_where_the_stack_overflowed`, async (t) => {
    const crashed = await crash(t, command);
    assertStartsInterrupted(crashed);
    // The probe's recursion fills the trace.
    assert.equal(crashed.frames.length, 256);
    if (symbolizer !== undefined) {
      assertCalls(decode(t, crashed, null).names, ["probe_recurse"]);
    }
    await expectEnded(crashed);
  });
}

test("crash_of_two_threads_at_once_reports_one", async (t) => {
  const crashed = await crash(t, "crash_two");
  assertStartsInterrupted(crashed);
  if (symbolizer !== undefined) {
    assertCalls(decode(t, crashed, null).names, ["probe_fault_together"]);
  }
  await expectEnded(crashed);
});
