// Randomized integration tests run on fast-check with a fixed seed, which a
// failure reports with its shrunk counterexample. KOTA_FUZZ_SEED and
// KOTA_FUZZ_RUNS override the seed and the number of runs, for long runs by
// hand; CI runs the defaults.

import fc from "fast-check";

const SEED = Number(process.env.KOTA_FUZZ_SEED ?? 20260927);
/** A long run by hand: KOTA_FUZZ_RUNS is set. */
const BY_HAND = process.env.KOTA_FUZZ_RUNS !== undefined;
/** The runs of a property, 100 unless overridden. */
export const RUNS = Number(process.env.KOTA_FUZZ_RUNS ?? 100);

/**
 * A fuzz test's timeout, which leaves room for the default runs; a long run
 * by hand has none.
 */
export const FUZZ_TIMEOUT = BY_HAND ? Infinity : 180_000;

/**
 * Checks `property` over `runs` runs, the run still going once `timeLimit`
 * ms have passed cut short: by default 60% of the test's timeout, which
 * leaves the rest for a slow leg's drivers to end. A failure found by then is
 * reported with its counterexample, shrunk as far as the time allowed; with
 * none, a leg too slow for all the runs passes on those it made. A long run
 * by hand has no time limit.
 */
export async function fuzz<T>(
  property: fc.IAsyncPropertyWithHooks<T>,
  runs = RUNS,
  timeLimit = FUZZ_TIMEOUT * 0.6,
): Promise<void> {
  await fc.assert(property, {
    seed: SEED,
    numRuns: runs,
    ...(Number.isFinite(timeLimit)
      ? { interruptAfterTimeLimit: timeLimit, markInterruptAsFailure: false }
      : {}),
  });
}

/**
 * A character for fc.string's unit and fc.jsonValue's stringUnit: half the
 * time ASCII, control characters, quotes, backslashes and brackets among
 * it, and half any code point but a lone surrogate. `unit: "binary"` alone
 * almost never draws ASCII.
 */
export const anyChar: fc.Arbitrary<string> = fc.oneof(
  fc.string({ unit: "binary-ascii", minLength: 1, maxLength: 1 }),
  fc.string({ unit: "binary", minLength: 1, maxLength: 1 }),
);

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
