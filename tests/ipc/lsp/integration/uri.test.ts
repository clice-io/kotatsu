// kota::ipc::lsp::URI's file URIs against VS Code's (vscode-uri), through
// lsp_probe, for absolute POSIX paths:
//
// - the URI kotatsu makes of a path reads back as the path, in kotatsu and in
//   VS Code;
// - the URI VS Code makes of a path reads as the path in kotatsu.
//
// The two encode differently (VS Code escapes ':' and more), which the checks
// leave to each; what matters is that each reads the other's.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import fc from "fast-check";
import { URI } from "vscode-uri";

import { Driver } from "../../harness/driver.ts";
import { FUZZ_TIMEOUT, fuzz, within } from "../../harness/fuzz.ts";
import type { JsonLines } from "../../harness/jsonl.ts";

// Windows paths (drive letters, backslashes, what fsPath makes of them) are
// not drawn yet.
const skip = process.platform === "win32" ? "POSIX paths only" : false;

// Path characters, among them every one a URI reserves or escapes.
const segment = fc.string({
  unit: fc.constantFrom(
    ...("aZ09-._~ #?%:@&=+;,!$'()*[]é😀".match(/./gsu) as string[]),
  ),
  minLength: 1,
  maxLength: 6,
});

// vscode-uri reads a path that starts with /X: as a Windows drive on every
// platform, so such paths are left out.
const path = fc
  .array(segment, { maxLength: 4 })
  .map((segments) => `/${segments.join("/")}`)
  .filter((drawn) => !/^\/[A-Za-z]:/.test(drawn));

async function ask(
  lines: JsonLines,
  question: object,
): Promise<Record<string, string>> {
  lines.send(question);
  const answer = await within(lines.receive(), "lsp_probe's answer");
  assert.ok(answer !== undefined, "lsp_probe ended");
  return answer as Record<string, string>;
}

async function withProbe(
  t: TestContext,
  body: (lines: JsonLines) => Promise<void>,
) {
  const driver = await Driver.spawn(t, "lsp_probe");
  const lines = driver.jsonLines();
  await body(lines);
  lines.end();
  await driver.expectExit(0);
}

test(
  "kotatsu_uris_read_back_as_their_paths",
  { skip, timeout: FUZZ_TIMEOUT },
  (t) =>
    withProbe(t, (lines) =>
      fuzz(
        fc.asyncProperty(path, async (drawn) => {
          const { uri } = await ask(lines, { fromFilePath: drawn });
          assert.ok(uri, `no URI for ${JSON.stringify(drawn)}`);
          assert.deepEqual(await ask(lines, { filePath: uri }), {
            path: drawn,
          });
          assert.equal(URI.parse(uri).fsPath, drawn, `VS Code reading ${uri}`);
        }),
      ),
    ),
);

test("vscode_uris_read_as_their_paths", { skip, timeout: FUZZ_TIMEOUT }, (t) =>
  withProbe(t, (lines) =>
    fuzz(
      fc.asyncProperty(path, async (drawn) => {
        const uri = URI.file(drawn).toString();
        assert.deepEqual(
          await ask(lines, { filePath: uri }),
          { path: drawn },
          `reading ${uri}`,
        );
      }),
    ),
  ),
);
