// A raw JSON-RPC client that keeps what a driver writes for the test to
// check: it pairs each response with the request it answers, keeps the
// driver's notifications, and answers the driver's own requests through a
// handler. A response it cannot pair, a second one to an id or one to an id
// never sent, is kept as a stray.

import type { Message, RawChannel } from "./raw.ts";

/** Answers a request from the driver with a message, or leaves it unanswered. */
export type Answer = (request: Message) => object | undefined;

export class Session {
  readonly wire: RawChannel;
  readonly notifications: Message[] = [];
  readonly strays: Message[] = [];
  /** Resolves once the driver's output ends. */
  readonly ended: Promise<void>;
  readonly #answer: Answer;
  readonly #waiting = new Map<string, PromiseWithResolvers<Message>>();
  #nextId = 1;

  constructor(wire: RawChannel, answer: Answer = () => undefined) {
    this.wire = wire;
    this.#answer = answer;
    this.ended = this.#read();
  }

  /** Sends a request with a fresh id; `response` is its response. */
  start(
    method: string,
    params?: unknown,
  ): { id: number; response: Promise<Message> } {
    const id = this.#nextId++;
    const response = this.expect(id);
    this.wire.send({ jsonrpc: "2.0", id, method, params });
    return { id, response };
  }

  /** Sends a request with a fresh id and waits for its response. */
  request(method: string, params?: unknown): Promise<Message> {
    return this.start(method, params).response;
  }

  notify(method: string, params?: unknown): void {
    this.wire.send({ jsonrpc: "2.0", method, params });
  }

  /**
   * The next response to `id`, sent before or after this call; it fails if
   * the driver's output ends first.
   */
  expect(id: number | string | null): Promise<Message> {
    const key = JSON.stringify(id);
    let waiting = this.#waiting.get(key);
    if (waiting === undefined) {
      waiting = Promise.withResolvers();
      // Handled by the caller, which may await it only later.
      waiting.promise.catch(() => {});
      this.#waiting.set(key, waiting);
    }
    return waiting.promise;
  }

  async #read(): Promise<void> {
    for (
      let message = await this.wire.receive();
      message;
      message = await this.wire.receive()
    ) {
      this.#dispatch(message);
    }
    for (const waiting of this.#waiting.values()) {
      waiting.reject(
        new Error("the driver's output ended before the response"),
      );
    }
  }

  #dispatch(message: Message): void {
    if (typeof message.method === "string") {
      if ("id" in message) {
        const reply = this.#answer(message);
        if (reply !== undefined) {
          this.wire.send(reply);
        }
      } else {
        this.notifications.push(message);
      }
      return;
    }
    const key = JSON.stringify(message.id ?? null);
    const waiting = this.#waiting.get(key);
    if (waiting === undefined) {
      this.strays.push(message);
      return;
    }
    this.#waiting.delete(key);
    waiting.resolve(message);
  }
}
