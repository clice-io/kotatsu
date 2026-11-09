// What stacktrace_probe's handler, set up with on_crash, is told of and
// writes with write_stacktrace(fd, crash) as it crashes, and how it ends:
//
// - the handler reports a crash once, with its signal, on Windows the
//   exception's code;
// - its trace starts at the address the context says the crash
//   interrupted, the one frame marked, and decodes to the calls that led
//   there;
// - a fault, a trap, a breakpoint and a stack overflow, on the main thread
//   or a Thread, end the process with the signal, on Windows with the
//   exception's code; an abort and an uncaught C++ exception end it with
//   SIGABRT, on Windows as abort() does without a handler;
// - on Windows, with windows_skip_error_reporting, the process ends with the
//   exception's code past every filter that declines it, and an abort and an
//   uncaught exception with 3, never failing fast; elsewhere as without;
// - an overflow's trace alternates between the two functions that recurse,
//   none of their frames lost or written twice;
// - of two threads that fault at once, one is reported;
// - a handler set before on_crash is given the crash after it;
// - a crash in the handler, a raised SIGSEGV, a SIGTERM and an ignored
//   SIGTRAP end the process as without on_crash.

import assert from "node:assert/strict";
import { constants } from "node:os";
import { type TestContext, test } from "node:test";

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
const posixOnly = windows && "on_crash's signals are POSIX's";

/** abort()'s exit codes on Windows: a fast fail's, or 3 without one. */
const ABORTED = [0xc0000409, 3];

/** The probe's environment, on_crash asked for windows_skip_error_reporting. */
const SKIPPING = { ...process.env, KOTATSU_PROBE_SKIP_ERROR_REPORTING: "1" };

/** The code of a C++ exception the MSVC runtime raises. */
const MSVC_EXCEPTION = 0xe06d7363;

/** The name of signal `number`. */
function signalName(number: number | undefined): NodeJS.Signals {
  const name = Object.entries(constants.signals).find(
    ([, value]) => value === number,
  )?.[0];
  assert.ok(name !== undefined, `signal ${number}`);
  return name as NodeJS.Signals;
}

/**
 * Checks that the handler reported the crash once, with `signal`, and that
 * the process ended as the crash ends it.
 */
async function expectReported(
  crashed: Crash,
  signal: number | undefined,
): Promise<void> {
  assert.equal(crashed.reports, 1);
  assert.equal(crashed.signal, signal);
  if (windows) {
    await crashed.driver.expectExit(crashed.code ?? ABORTED);
  } else {
    await crashed.driver.expectSignal(signalName(signal));
  }
}

/**
 * What crash_throw printed, its uncaught exception reported once: by the
 * unhandled exception filter where the MSVC runtime calls std::terminate
 * from its own, after on_crash's; elsewhere, MinGW's runtime included, as
 * the SIGABRT of the abort() std::terminate calls. Null, the test skipped,
 * in a build without exceptions, where the probe exits with 0.
 */
async function thrown(
  t: TestContext,
  env?: NodeJS.ProcessEnv,
): Promise<Crash | null> {
  const crashed = await crash(t, "crash_throw", env);
  if (!crashed.image.exceptions) {
    await crashed.driver.expectExit(0);
    t.skip("built without exceptions");
    return null;
  }
  assert.equal(crashed.reports, 1);
  if (windows && crashed.signal === 0) {
    assert.equal(crashed.code, MSVC_EXCEPTION);
  } else {
    assert.equal(crashed.signal, constants.signals.SIGABRT);
  }
  return crashed;
}

/** Checks that the trace starts at the address the crash interrupted. */
function assertStartsInterrupted(crashed: Crash): void {
  assertInterrupted(crashed);
  assert.equal(crashed.frames[0]?.interrupted, true);
}

test("crash_of_a_fault_starts_at_its_address", async (t) => {
  const crashed = await crash(t, "crash_fault");
  assertStartsInterrupted(crashed);
  assert.equal(crashed.code ?? 0xc0000005, 0xc0000005);
  if (symbolizer !== undefined) {
    const decoded = decode(t, crashed, null);
    assertFaultCalls(crashed, decoded);
    assertCalls(decoded.names, ["probe_caller", "main"]);
  }
  await expectReported(crashed, constants.signals.SIGSEGV);
});

test("crash_of_a_trap_ends_with_its_signal", async (t) => {
  const crashed = await crash(t, "crash_trap");
  assertStartsInterrupted(crashed);
  if (symbolizer !== undefined) {
    assertCalls(decode(t, crashed, null).names, ["probe_trap", "main"]);
  }
  // ud2 on x86-64, which Windows reports as an illegal instruction too; brk
  // on arm64.
  const expected =
    windows || process.arch === "x64"
      ? constants.signals.SIGILL
      : constants.signals.SIGTRAP;
  await expectReported(crashed, expected);
});

test("crash_of_a_breakpoint_ends_with_its_signal", async (t) => {
  const crashed = await crash(t, "crash_breakpoint");
  assertStartsInterrupted(crashed);
  if (symbolizer !== undefined) {
    assertCalls(decode(t, crashed, null).names, ["probe_breakpoint", "main"]);
  }
  // Windows has no signal for a breakpoint; past an int3 on x86-64, only a
  // signal raised again ends the process.
  assert.equal(crashed.code ?? 0x80000003, 0x80000003);
  await expectReported(crashed, windows ? 0 : constants.signals.SIGTRAP);
});

test("crash_of_an_abort_ends_as_abort_does", async (t) => {
  const crashed = await crash(t, "crash_abort");
  // From where abort() raised SIGABRT, in the C library or runtime: on
  // Windows the context of raise(), which called the handler.
  const marked = crashed.frames.filter((frame) => frame.interrupted);
  assert.equal(marked.length, 1);
  assert.equal(crashed.frames[0], marked[0]);
  if (!windows) {
    assert.equal(marked[0]?.address, crashed.fault);
  }
  if (symbolizer !== undefined) {
    assertCalls(decode(t, crashed, null).names, ["probe_abort", "main"]);
  }
  await expectReported(crashed, constants.signals.SIGABRT);
});

test("crash_of_an_uncaught_exception_ends_as_abort_does", async (t) => {
  const crashed = await thrown(t);
  if (crashed === null) {
    return;
  }
  if (windows) {
    await crashed.driver.expectExit(ABORTED);
  } else {
    await crashed.driver.expectSignal("SIGABRT");
  }
});

test("crash_of_a_fault_skipping_error_reporting_ends_with_its_code", async (t) => {
  const crashed = await crash(t, "crash_fault", SKIPPING);
  assertStartsInterrupted(crashed);
  await expectReported(crashed, constants.signals.SIGSEGV);
});

test("crash_of_an_abort_skipping_error_reporting_does_not_fail_fast", async (t) => {
  const crashed = await crash(t, "crash_abort", SKIPPING);
  assert.equal(crashed.reports, 1);
  assert.equal(crashed.signal, constants.signals.SIGABRT);
  if (windows) {
    await crashed.driver.expectExit(3);
  } else {
    await crashed.driver.expectSignal("SIGABRT");
  }
});

test("crash_of_an_uncaught_exception_skipping_error_reporting_does_not_fail_fast", async (t) => {
  const crashed = await thrown(t, SKIPPING);
  if (crashed === null) {
    return;
  }
  if (windows) {
    await crashed.driver.expectExit(3);
  } else {
    await crashed.driver.expectSignal("SIGABRT");
  }
});

test("crash_with_a_handler_before_skipping_error_reporting_goes_on_to_it", async (t) => {
  const crashed = await crash(t, "crash_after_handler", SKIPPING);
  assert.equal(crashed.reports, 1);
  assert.equal(crashed.previous, crashed.fault);
  await crashed.driver.expectExit(4);
});

for (const [name, command] of [
  ["crash_of_an_overflow", "crash_overflow"],
  ["crash_of_an_overflow_on_a_thread", "crash_overflow_on_thread"],
]) {
  test(`${name}_starts_where_the_stack_overflowed`, async (t) => {
    const crashed = await crash(t, command);
    if (command === "crash_overflow" && crashed.image.tsan) {
      t.skip("under TSan the main thread's overflow reaches no handler");
      return;
    }
    assertStartsInterrupted(crashed);
    // The recursion fills the trace, the two functions' frames alternating:
    // after the interrupted one, the return address into each, in turn.
    const { frames } = crashed;
    assert.equal(frames.length, 256);
    for (let at = 1; at + 2 < frames.length; ++at) {
      assert.notEqual(frames[at]?.address, frames[at + 1]?.address, `${at}`);
      assert.equal(frames[at]?.address, frames[at + 2]?.address, `${at}`);
    }
    if (symbolizer !== undefined) {
      // And the interrupted frame is of the other function than the first
      // return address's.
      const { names } = decode(t, crashed, null);
      for (let at = 0; at + 1 < 16; ++at) {
        assert.ok(names[at]?.startsWith("probe_recurse_"), names[at]);
        assert.notEqual(names[at], names[at + 1], JSON.stringify(names));
      }
    }
    const overflowed = windows
      ? constants.signals.SIGSEGV
      : [constants.signals.SIGSEGV, constants.signals.SIGBUS].find(
          (signal) => signal === crashed.signal,
        );
    await expectReported(crashed, overflowed);
  });
}

test("crash_of_two_threads_at_once_reports_one", async (t) => {
  const crashed = await crash(t, "crash_two");
  assertStartsInterrupted(crashed);
  if (symbolizer !== undefined) {
    assertCalls(decode(t, crashed, null).names, ["probe_fault_together"]);
  }
  await expectReported(crashed, constants.signals.SIGSEGV);
});

test("crash_with_a_handler_before_goes_on_to_it", async (t) => {
  const crashed = await crash(t, "crash_after_handler");
  assertStartsInterrupted(crashed);
  assert.equal(crashed.reports, 1);
  // With the context the crash brought.
  assert.equal(crashed.previous, crashed.fault);
  await crashed.driver.expectExit(4);
});

test(
  "crash_in_the_handler_ends_the_process",
  { skip: posixOnly },
  async (t) => {
    const crashed = await crash(t, "crash_in_handler");
    assert.equal(crashed.frames.length, 0);
    await expectReported(crashed, constants.signals.SIGSEGV);
  },
);

test(
  "crash_of_a_raised_fault_signal_ends_with_it",
  { skip: posixOnly },
  async (t) => {
    const crashed = await crash(t, "crash_raise");
    assert.equal(crashed.frames[0]?.interrupted, true);
    await expectReported(crashed, constants.signals.SIGSEGV);
  },
);

test("crash_handler_leaves_sigterm_alone", { skip: posixOnly }, async (t) => {
  const crashed = await crash(t, "crash_terminate");
  assert.equal(crashed.reports, 0);
  await crashed.driver.expectSignal("SIGTERM");
});

test(
  "crash_handler_leaves_an_ignored_signal_ignored",
  { skip: posixOnly },
  async (t) => {
    const crashed = await crash(t, "crash_ignored");
    assert.equal(crashed.reports, 0);
    await crashed.driver.expectExit(5);
  },
);
