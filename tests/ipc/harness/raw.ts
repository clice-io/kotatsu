// JSON-RPC's base protocol at the byte level, for tests that watch the bytes
// a driver reads and writes rather than go through a client: each message is
// framed by a Content-Length header.

import assert from "node:assert/strict";
import type { Readable, Writable } from "node:stream";

import type { Driver } from "../../harness/driver.ts";

const SEPARATOR = "\r\n\r\n";

/** A JSON-RPC message as it came, its members not yet checked. */
export type Message = Record<string, unknown>;

/** The result of `response`, which must be a success. */
export function resultOf(response: Message): unknown {
  assert.ok(!("error" in response), `an error: ${JSON.stringify(response)}`);
  assert.ok("result" in response, `no result: ${JSON.stringify(response)}`);
  return response.result;
}

/** The error of `response`, which must be an error response. */
export function errorOf(response: Message): Record<string, unknown> {
  assert.ok(!("result" in response), `a result: ${JSON.stringify(response)}`);
  const error = response.error;
  assert.ok(
    typeof error === "object" && error !== null,
    `no error: ${JSON.stringify(response)}`,
  );
  return error as Record<string, unknown>;
}

/** `payload` in a frame as kotatsu writes one. */
export function frameText(payload: string): Buffer {
  const bytes = Buffer.from(payload);
  return Buffer.concat([
    Buffer.from(`Content-Length: ${bytes.length}${SEPARATOR}`),
    bytes,
  ]);
}

/** `message` in its frame. */
export function frame(message: object): Buffer {
  return frameText(JSON.stringify(message));
}

export class RawChannel {
  readonly #output: Writable;
  #input = Buffer.alloc(0);
  #ended = false;
  #wake: (() => void) | undefined;

  /** A channel over `driver`'s stdio. */
  static of(driver: Driver): RawChannel {
    return new RawChannel(driver.stdout, driver.stdin);
  }

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
    this.#output.write(frame(message));
  }

  /** Writes `bytes` as they are, framed or not; resolves once they are written. */
  write(bytes: Uint8Array): Promise<void> {
    // An error means the driver has exited, which the test checks.
    return new Promise((resolve) => this.#output.write(bytes, () => resolve()));
  }

  /** Ends the output, which the driver reads as the end of its input. */
  end(): void {
    this.#output.end();
  }

  /**
   * The next message, or undefined once the input ends between two frames.
   * One call at a time.
   */
  async receive(): Promise<Message | undefined> {
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

  #take(): Message | undefined {
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
    const message: unknown = JSON.parse(payload);
    assert.ok(
      typeof message === "object" &&
        message !== null &&
        !Array.isArray(message),
      `not a message: ${payload}`,
    );
    return message as Message;
  }
}
