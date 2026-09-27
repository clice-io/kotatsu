// A driver as the client VS Code uses, vscode-jsonrpc, sees it.

import {
  createMessageConnection,
  StreamMessageReader,
  StreamMessageWriter,
  type MessageConnection,
  type MessageReader,
  type MessageWriter,
} from "vscode-jsonrpc/node";

import type { Driver } from "../../harness/driver.ts";

/**
 * A JSON-RPC connection over `driver`'s stdio, not yet listening. Its end()
 * closes the driver's input after everything sent before it. Once the
 * driver's output ends and every message it sent has been handled, the
 * connection is disposed, which fails the requests still waiting for an
 * answer.
 */
export function connect(driver: Driver): MessageConnection {
  let received = 0;
  let handled = 0;
  let ended = false;
  const reader = new StreamMessageReader(driver.stdout);
  const counted: MessageReader = {
    onError: reader.onError,
    onClose: reader.onClose,
    onPartialMessage: reader.onPartialMessage,
    listen: (callback) =>
      reader.listen((message) => {
        received += 1;
        callback(message);
      }),
    dispose: () => reader.dispose(),
  };
  // vscode-jsonrpc writes each message on a later tick, one after another,
  // but ends the stream at once; waiting for the last write keeps end() from
  // cutting off what was sent before it.
  const writer = new StreamMessageWriter(driver.stdin);
  let written = Promise.resolve();
  const ordered: MessageWriter = {
    onError: writer.onError,
    onClose: writer.onClose,
    write: (message) => (written = writer.write(message)),
    end: () => {
      const end = () => writer.end();
      written.then(end, end);
    },
    dispose: () => writer.dispose(),
  };
  const connection = createMessageConnection(counted, ordered, undefined, {
    messageStrategy: {
      handleMessage: (message, next) => {
        try {
          next(message);
        } finally {
          handled += 1;
          disposeIfDrained();
        }
      },
    },
  });
  // The connection's own onClose also fires when the driver's input closes,
  // and messages may still wait in its queue; only the reader tells that
  // nothing more comes.
  reader.onClose(() => {
    ended = true;
    disposeIfDrained();
  });
  function disposeIfDrained() {
    if (ended && handled === received) {
      connection.dispose();
    }
  }
  return connection;
}
