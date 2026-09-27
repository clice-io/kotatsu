// JSON-RPC's base protocol at the byte level, for tests that watch the wire
// itself rather than go through a client: each message is framed by a
// Content-Length header.

import assert from "node:assert/strict";
import type { Readable, Writable } from "node:stream";

const SEPARATOR = "\r\n\r\n";

export class RawChannel {
  readonly #output: Writable;
  #input = Buffer.alloc(0);
  #ended = false;
  #wake: (() => void) | undefined;

  constructor(input: Readable, output: Writable) {
    this.#output = output;
    input.on("data", (chunk: Buffer) => {
      this.#input = Buffer.concat([this.#input, chunk]);
      this.#wake?.();
    });
    input.on("end", () => {
      this.#ended = true;
      this.#wake?.();
    });
  }

  /** Writes `message` in its frame. */
  send(message: object): void {
    const payload = Buffer.from(JSON.stringify(message));
    this.#output.write(`Content-Length: ${payload.length}${SEPARATOR}`);
    this.#output.write(payload);
  }

  /**
   * The next message, or undefined once the input ends between two frames.
   * One call at a time.
   */
  async receive(): Promise<unknown> {
    for (;;) {
      const message = this.#take();
      if (message !== undefined) {
        return message;
      }
      if (this.#ended) {
        assert.equal(this.#input.length, 0, "the input ended inside a frame");
        return undefined;
      }
      await new Promise<void>((resolve) => (this.#wake = resolve));
    }
  }

  #take(): unknown {
    const separator = this.#input.indexOf(SEPARATOR);
    if (separator < 0) {
      return undefined;
    }
    // kotatsu writes a Content-Length header and nothing else.
    const header = this.#input.subarray(0, separator).toString("latin1");
    const length = /^Content-Length: (\d+)$/.exec(header)?.[1];
    assert.ok(length, `malformed header ${JSON.stringify(header)}`);
    const start = separator + SEPARATOR.length;
    const end = start + Number(length);
    if (this.#input.length < end) {
      return undefined;
    }
    const payload = this.#input.subarray(start, end).toString();
    this.#input = this.#input.subarray(end);
    return JSON.parse(payload);
  }
}
