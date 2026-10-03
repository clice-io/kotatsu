// LineMap's positions against VS Code's (vscode-languageserver-textdocument),
// through lsp_probe, in UTF-16 code units as LSP counts by default:
//
// - the position of every code point boundary of a text;
// - the offset of any position, in or past the text, by to_offset and by
//   to_offset_clamped.
//
// Where LineMap answers otherwise, known_deviations.ts says so, and those
// inputs are not drawn.

import assert from "node:assert/strict";
import { test, type TestContext } from "node:test";

import fc from "fast-check";
import { TextDocument } from "vscode-languageserver-textdocument";

import { Driver } from "../../../harness/driver.ts";
import { FUZZ_TIMEOUT, fuzz, within } from "../../../harness/fuzz.ts";
import type { JsonLines } from "../../../harness/jsonl.ts";
import { deviations } from "../harness/known_deviations.ts";

// Text made of ASCII, a two-byte and a four-byte (surrogate pair) character,
// and line breaks.
const text = fc
  .array(
    fc.constantFrom(
      "a",
      "é",
      "😀",
      " ",
      "\n",
      "\r\n",
      ...(deviations.loneCarriageReturn ? [] : ["\r"]),
    ),
    { maxLength: 12 },
  )
  .map((pieces) => pieces.join(""));

type Boundary = { bytes: number; units: number };

function boundaries(content: string): Boundary[] {
  const result = [{ bytes: 0, units: 0 }];
  for (const character of content) {
    const { bytes, units } = result[result.length - 1];
    result.push({
      bytes: bytes + Buffer.byteLength(character),
      units: units + character.length,
    });
  }
  return result;
}

function units(content: string, bytes: number): number {
  return Buffer.from(content).subarray(0, bytes).toString().length;
}

async function ask(
  lines: JsonLines,
  question: object,
): Promise<Record<string, number>> {
  lines.send(question);
  const answer = await within(lines.receive(), "lsp_probe's answer");
  assert.ok(answer !== undefined, "lsp_probe ended");
  return answer as Record<string, number>;
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

test("positions_of_offsets_match_vscode", { timeout: FUZZ_TIMEOUT }, (t) =>
  withProbe(t, (lines) =>
    fuzz(
      fc.asyncProperty(text, fc.nat(), async (content, pick) => {
        const all = boundaries(content);
        const { bytes, units: offset } = all[pick % all.length];
        const document = TextDocument.create(
          "file:///text",
          "plaintext",
          0,
          content,
        );
        assert.deepEqual(
          await ask(lines, { text: content, offset: bytes }),
          document.positionAt(offset),
          `the position of ${offset} in ${JSON.stringify(content)}`,
        );
      }),
    ),
  ),
);

test("offsets_of_positions_match_vscode", { timeout: FUZZ_TIMEOUT }, (t) =>
  withProbe(t, (lines) =>
    fuzz(
      fc.asyncProperty(
        text,
        fc.nat({ max: 8 }),
        fc.nat({ max: 8 }),
        async (content, line, character) => {
          const document = TextDocument.create(
            "file:///text",
            "plaintext",
            0,
            content,
          );
          if (deviations.linePastEnd) {
            fc.pre(line < document.lineCount);
          }
          const start = document.offsetAt({ line, character: 0 });
          const length =
            document.offsetAt({ line, character: Number.MAX_SAFE_INTEGER }) -
            start;
          if (deviations.insideSurrogatePair) {
            const code = content.charCodeAt(start + character);
            fc.pre(!(character < length && code >= 0xdc00 && code <= 0xdfff));
          }
          const answer = await ask(lines, { text: content, line, character });
          assert.ok(
            answer.offset !== undefined,
            `no offset for ${line}:${character}`,
          );
          assert.equal(
            units(content, answer.offset),
            document.offsetAt({ line, character }),
            `the offset of ${line}:${character} in ${JSON.stringify(content)}`,
          );
        },
      ),
    ),
  ),
);

test(
  "clamped_offsets_of_positions_match_vscode",
  { timeout: FUZZ_TIMEOUT },
  (t) =>
    withProbe(t, (lines) =>
      fuzz(
        fc.asyncProperty(
          text,
          fc.nat({ max: 8 }),
          fc.nat({ max: 8 }),
          async (content, line, character) => {
            const document = TextDocument.create(
              "file:///text",
              "plaintext",
              0,
              content,
            );
            if (deviations.insideSurrogatePair && line < document.lineCount) {
              const start = document.offsetAt({ line, character: 0 });
              const code = content.charCodeAt(start + character);
              const length =
                document.offsetAt({
                  line,
                  character: Number.MAX_SAFE_INTEGER,
                }) - start;
              fc.pre(!(character < length && code >= 0xdc00 && code <= 0xdfff));
            }
            const answer = await ask(lines, {
              text: content,
              line,
              character,
              clamped: true,
            });
            assert.ok(
              answer.offset !== undefined,
              `no clamped offset for ${line}:${character}`,
            );
            assert.equal(
              units(content, answer.offset),
              document.offsetAt({ line, character }),
              `the clamped offset of ${line}:${character} in ${JSON.stringify(content)}`,
            );
          },
        ),
      ),
    ),
);
