// What the tests of jsonrpc_driver (ipc/integration/drivers) share on a raw
// channel.

import assert from "node:assert/strict";

import { within } from "../../harness/fuzz.ts";
import { resultOf, type Message } from "./raw.ts";
import type { Session } from "./session.ts";

/** A request the driver answers with its params, `[id]`. */
export function echo(id: number): Message {
  return { jsonrpc: "2.0", id, method: "test/echo", params: [id] };
}

/**
 * Asks the driver to echo a probe, which it answers after everything sent
 * before, and takes the strays that came first: its answers to messages the
 * session sent no request for.
 */
export async function probe(session: Session): Promise<Message[]> {
  const response = await within(
    session.request("test/echo", ["probe"]),
    "probe answer",
  );
  assert.deepEqual(resultOf(response), ["probe"]);
  return session.strays.splice(0);
}
