// The ipc examples, run end to end.

import assert from "node:assert/strict";
import { test } from "node:test";

import { Driver } from "../../ipc/harness/driver.ts";

test("jsonrpc_server_serves_its_methods", async (t) => {
  const example = await Driver.spawn(t, "jsonrpc_server");
  const connection = example.connect();
  connection.listen();
  assert.deepEqual(
    await connection.sendRequest("example/add", { a: 2, b: 3 }),
    { sum: 5 },
  );
  await connection.sendNotification("example/log", { text: "hello" });
  connection.end();
  await example.expectExit(0);
  assert.ok(
    example.stderr.split(/\r?\n/).includes("[example/log] hello"),
    example.stderr,
  );
});

test("jsonrpc_roundtrip_writes_its_script", async (t) => {
  const example = await Driver.spawn(t, "jsonrpc_roundtrip");
  const output = await example.output();
  await example.expectExit(0);
  assert.deepEqual(output.trimEnd().split(/\r?\n/), [
    "Outgoing messages:",
    '{"jsonrpc":"2.0","method":"example/note","params":{"text":"handling request"}}',
    '{"jsonrpc":"2.0","id":1,"method":"client/add","params":{"a":3,"b":1}}',
    '{"jsonrpc":"2.0","id":7,"result":{"sum":9}}',
  ]);
});

test("spawn_worker_runs_its_workers", async (t) => {
  const example = await Driver.spawn(t, "spawn_worker");
  const output = await example.output();
  await example.expectExit(0);
  const lines = output.split(/\r?\n/);
  for (const [worker, source, header] of [
    ["worker-1", "main.cpp", "vector"],
    ["worker-2", "lib.cpp", "string"],
    ["worker-3", "tool.cpp", "memory"],
  ]) {
    const include = "/opt/kotatsu/example/include";
    for (const line of [
      `[${worker}] worker command: clang++ -c src/${source} -I${include}`,
      `[${worker}] resolved header: ${include}/${header}`,
    ]) {
      assert.ok(
        lines.includes(line),
        `no line ${JSON.stringify(line)} in:\n${output}`,
      );
    }
  }
});
