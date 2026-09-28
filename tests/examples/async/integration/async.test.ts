// The async examples, run end to end: each finishes, exits with 0 and prints
// what it shows.

import assert from "node:assert/strict";
import { test } from "node:test";

import { Driver } from "../../../harness/driver.ts";

test("async_basics_runs_every_demo", async (t) => {
  const example = await Driver.spawn(t, "async_basics");
  const output = await example.output();
  await example.expectExit(0);
  for (const line of [
    "compute() = 13",
    "when_all result = 33",
    "  all workers done, total = 150",
    "  6a: caught cancellation (expected)",
    "=== done ===",
  ]) {
    assert.ok(
      output.split(/\r?\n/).includes(line),
      `no line ${JSON.stringify(line)} in:\n${output}`,
    );
  }
});

test("dump_dot_prints_a_graph", async (t) => {
  const example = await Driver.spawn(t, "dump_dot");
  const output = (await example.output()).trim();
  await example.expectExit(0);
  assert.match(output, /^digraph .*\}$/s);
});
