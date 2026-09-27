// Randomized integration tests run on fast-check with a fixed seed, which a
// failure reports with its shrunk counterexample. KOTA_FUZZ_SEED and
// KOTA_FUZZ_RUNS override the seed and the number of runs, for long runs by
// hand; CI runs the defaults.

import fc from "fast-check";

const SEED = Number(process.env.KOTA_FUZZ_SEED ?? 20260927);
const RUNS = Number(process.env.KOTA_FUZZ_RUNS ?? 100);

/** A test's timeout that leaves room for the default runs of a fuzz test. */
export const FUZZ_TIMEOUT = 180_000;

/**
 * Checks `property` over the runs. Shrinking a failure stops after about
 * half the test's timeout, so the failure is reported rather than timed out.
 */
export async function fuzz<T>(
  property: fc.IAsyncPropertyWithHooks<T>,
): Promise<void> {
  await fc.assert(property, {
    seed: SEED,
    numRuns: RUNS,
    interruptAfterTimeLimit: FUZZ_TIMEOUT / 2,
    markInterruptAsFailure: true,
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
