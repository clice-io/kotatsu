// kotatsu::ipc::lsp::URI's file URIs against VS Code's (vscode-uri), through
// lsp_probe, for the absolute paths of the platform: POSIX paths, or drive
// paths on Windows, and UNC paths on both:
//
// - the URI kotatsu makes of a path reads back as the path, in kotatsu and in
//   VS Code;
// - the URI VS Code makes of a path reads as the path in kotatsu.
//
// The two encode differently (VS Code escapes ':' and more), which the checks
// leave to each; what matters is that each reads the other's. Both write a
// drive letter in lower case, and on POSIX keep `\` as a character of a name;
// on Windows kotatsu separates names with '/', and VS Code with '\'.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import fc from "fast-check";
import { URI } from "vscode-uri";

import { Driver } from "../../../harness/driver.ts";
import { FUZZ_TIMEOUT, fuzz, within } from "../../../harness/fuzz.ts";
import type { JsonLines } from "../../../harness/jsonl.ts";

const windows = process.platform === "win32";
const separator = windows ? "\\" : "/";

// Path characters, among them every one a URI reserves or escapes, and on
// POSIX `\`, which is a character of a name there.
const segment = fc.string({
  unit: fc.constantFrom(
    ...("aZ09-._~ #?%:@&=+;,!$'()*[]é😀".match(/./gsu) as string[]),
    ...(windows ? [] : ["\\"]),
  ),
  minLength: 1,
  maxLength: 6,
});

// vscode-uri reads a path that starts with /X: as a Windows drive on every
// platform, so such paths are left out.
const posixPath = fc
  .array(segment, { maxLength: 4 })
  .map((segments) => `/${segments.join("/")}`)
  .filter((drawn) => !/^\/[A-Za-z]:/.test(drawn));

const drivePath = fc
  .tuple(fc.constantFrom(..."ACZacz"), fc.array(segment, { maxLength: 4 }))
  .map(([drive, segments]) => `${drive}:\\${segments.join("\\")}`);

// \\server\share\... on Windows, //server/share/... elsewhere. vscode-uri
// writes a share that starts with X: in lower case, as if a drive, so such
// shares are left out.
const uncPath = fc
  .tuple(
    fc.stringMatching(/^[a-z0-9][a-z0-9.-]{0,7}$/),
    fc.array(segment, { minLength: 1, maxLength: 4 }),
  )
  .filter(([, [share]]) => !/^[A-Za-z]:/.test(share))
  .map(
    ([server, segments]) =>
      `${separator}${separator}${[server, ...segments].join(separator)}`,
  );

const path = fc.oneof(windows ? drivePath : posixPath, uncPath);

/** `path` with its drive letter, if it has one, in lower case. */
function lowerDrive(path: string): string {
  return /^[A-Za-z]:/.test(path) ? path[0].toLowerCase() + path.slice(1) : path;
}

/** `drawn` as kotatsu writes a path back: on Windows its names separated by '/'. */
function kotatsuPath(drawn: string): string {
  return lowerDrive(windows ? drawn.replaceAll("\\", "/") : drawn);
}

/** `drawn` as VS Code's fsPath writes a path. */
function vscodePath(drawn: string): string {
  return lowerDrive(drawn);
}

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

test("kotatsu_uris_read_back_as_their_paths", { timeout: FUZZ_TIMEOUT }, (t) =>
  withProbe(t, (lines) =>
    fuzz(
      fc.asyncProperty(path, async (drawn) => {
        const { uri } = await ask(lines, { fromPath: drawn });
        assert.ok(uri, `no URI for ${JSON.stringify(drawn)}`);
        assert.deepEqual(await ask(lines, { toPath: uri }), {
          path: kotatsuPath(drawn),
        });
        assert.equal(
          URI.parse(uri).fsPath,
          vscodePath(drawn),
          `VS Code reading ${uri}`,
        );
      }),
    ),
  ),
);

test("vscode_uris_read_as_their_paths", { timeout: FUZZ_TIMEOUT }, (t) =>
  withProbe(t, (lines) =>
    fuzz(
      fc.asyncProperty(path, async (drawn) => {
        const uri = URI.file(drawn).toString();
        assert.deepEqual(
          await ask(lines, { toPath: uri }),
          { path: kotatsuPath(drawn) },
          `reading ${uri}`,
        );
      }),
    ),
  ),
);
