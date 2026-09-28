// JSON lines, one value a line each way, for a driver that is no JSON-RPC
// peer.

import assert from "node:assert/strict";
import type { Readable, Writable } from "node:stream";

export class JsonLines {
  readonly #output: Writable;
  #input = "";
  #ended = false;
  #wake: (() => void) | undefined;

  constructor(input: Readable, output: Writable) {
    this.#output = output;
    input.setEncoding("utf8");
    input.on("data", (text: string) => {
      this.#input += text;
      this.#wake?.();
    });
    input.on("end", () => {
      this.#ended = true;
      this.#wake?.();
    });
  }

  send(value: unknown): void {
    this.#output.write(`${JSON.stringify(value)}\n`);
  }

  /** The next value, or undefined once the input ends. One call at a time. */
  async receive(): Promise<unknown> {
    for (;;) {
      const end = this.#input.indexOf("\n");
      if (end >= 0) {
        // A driver on Windows ends its lines with \r\n.
        const line = this.#input.slice(0, end).replace(/\r$/, "");
        this.#input = this.#input.slice(end + 1);
        return JSON.parse(line);
      }
      if (this.#ended) {
        assert.equal(this.#input, "", "the input ended inside a line");
        return undefined;
      }
      await new Promise<void>((resolve) => (this.#wake = resolve));
    }
  }

  /** Ends the output, which the driver reads as the end of its input. */
  end(): void {
    this.#output.end();
  }
}
