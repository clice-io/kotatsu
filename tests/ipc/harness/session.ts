// A raw JSON-RPC client that keeps what a driver writes for the test to
// check: it pairs each response with the request it answers, keeps the
// driver's notifications, and answers the driver's own requests through a
// handler. A response it cannot pair, a second one to an id or one to an id
// never sent, is kept as a stray.

import type { TestContext } from "node:test";

import { Driver } from "../../harness/driver.ts";
import { within } from "../../harness/fuzz.ts";
import { RawChannel, type Message } from "./raw.ts";

/** Answers a request from the driver with a message, or leaves it unanswered. */
export type Answer = (request: Message) => object | undefined;

export class Session {
  readonly channel: RawChannel;
  readonly notifications: Message[] = [];
  readonly strays: Message[] = [];
  /** Resolves once the driver's output ends. */
  readonly ended: Promise<void>;
  readonly #answer: Answer;
  readonly #waiting = new Map<string, PromiseWithResolvers<Message>>();
  #nextId = 1;

  constructor(channel: RawChannel, answer: Answer = () => undefined) {
    this.channel = channel;
    this.#answer = answer;
    this.ended = this.#read();
  }

  /** Spawns the driver `name` with `args`, and a session over its stdio. */
  static async spawn(
    t: TestContext,
    name: string,
    { args = [], answer }: { args?: string[]; answer?: Answer } = {},
  ): Promise<[Driver, Session]> {
    const driver = await Driver.spawn(t, name, args);
    return [driver, new Session(RawChannel.of(driver), answer)];
  }

  /** Sends a request with a fresh id; `response` is its response. */
  start(
    method: string,
    params?: unknown,
  ): { id: number; response: Promise<Message> } {
    const id = this.#nextId++;
    const response = this.expect(id);
    this.channel.send({ jsonrpc: "2.0", id, method, params });
    return { id, response };
  }

  /** Sends a request with a fresh id and waits for its response. */
  request(method: string, params?: unknown): Promise<Message> {
    return this.start(method, params).response;
  }

  notify(method: string, params?: unknown): void {
    this.channel.send({ jsonrpc: "2.0", method, params });
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

  /**
   * Ends the driver's input, then waits for its output to end and checks
   * that it exits with 0.
   */
  async finish(driver: Driver): Promise<void> {
    this.channel.end();
    await within(this.ended, "end of the driver's output");
    await driver.expectExit(0);
  }

  async #read(): Promise<void> {
    for (
      let message = await this.channel.receive();
      message;
      message = await this.channel.receive()
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
          this.channel.send(reply);
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
