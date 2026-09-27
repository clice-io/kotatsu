// Randomized integration tests run on fast-check with a fixed seed, which a
// failure reports with its shrunk counterexample. KOTA_FUZZ_SEED and
// KOTA_FUZZ_RUNS override the seed and the number of runs, for long runs by
// hand; CI runs the defaults.

import fc from "fast-check";

const SEED = Number(process.env.KOTA_FUZZ_SEED ?? 20260927);
/** The runs of a property, 100 unless overridden. */
export const RUNS = Number(process.env.KOTA_FUZZ_RUNS ?? 100);

/** A test's timeout that leaves room for the default runs of a fuzz test. */
export const FUZZ_TIMEOUT = 180_000;

/**
 * Checks `property` over `runs` runs, within the test's timeout: no run, and
 * no step of shrinking a failure, starts after 40% of it, and one still going
 * at 60% is cut short, which leaves the rest for a slow leg's drivers to start
 * and end. A failure found by then is reported with its counterexample, shrunk
 * as far as the time allowed; a leg too slow for all the runs passes on those
 * it made.
 */
export async function fuzz<T>(
  property: fc.IAsyncPropertyWithHooks<T>,
  runs = RUNS,
): Promise<void> {
  await fc.assert(property, {
    seed: SEED,
    numRuns: runs,
    skipAllAfterTimeLimit: FUZZ_TIMEOUT * 0.4,
    interruptAfterTimeLimit: FUZZ_TIMEOUT * 0.6,
    markInterruptAsFailure: false,
  });
}

/** `value` as the other side reads it: what JSON.stringify writes. */
export function roundtrip(value: unknown): unknown {
  return JSON.parse(JSON.stringify(value));
}

/**
 * `promise`, or a failure naming `what` if it takes longer than `ms`: a fuzz
 * run that stalls fails on its own, and can be shrunk, before the test times
 * out.
 */
export async function within<T>(
  promise: Promise<T>,
  what: string,
  ms = 10_000,
): Promise<T> {
  let timer: NodeJS.Timeout | undefined;
  const timeout = new Promise<never>((_, reject) => {
    timer = setTimeout(
      () => reject(new Error(`no ${what} within ${ms} ms`)),
      ms,
    );
  });
  try {
    return await Promise.race([promise, timeout]);
  } finally {
    clearTimeout(timer);
  }
}
