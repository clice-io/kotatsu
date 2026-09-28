// Measures how much of kotatsu's own code the tests reach; `pixi run coverage`
// builds the `coverage` preset and runs this. It runs every ctest stage, its
// arguments passed on to ctest, and each process writes its profile into
// build/coverage/profiles (LLVM_PROFILE_FILE in the `coverage` test preset).
// It then merges the profiles and reports on include/kota and src over every
// program built, into build/coverage/report:
//
//   summary.md     lines, functions and regions covered per module, a module
//                  being include/kota/<module> together with src/<module>
//   html/          the sources annotated with their counts
//   coverage.lcov  the same data in lcov's format
//
// The report is written even when a test fails; the exit code is ctest's.

import { spawnSync } from "node:child_process";
import {
  mkdirSync,
  readdirSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { join, relative, sep } from "node:path";

const ROOT = join(import.meta.dirname, "..");
const BUILD = join(ROOT, "build", "coverage");
const PROFILES = join(BUILD, "profiles");
const PROFDATA = join(BUILD, "coverage.profdata");
const REPORT = join(BUILD, "report");

// A program records each inline function it does not use as a placeholder of
// hash 0, which llvm-cov warns of as mismatched data where another program
// uses the function. The profiles are this run's own, so the warning says
// nothing and is dropped.
const MISMATCH = /.*functions have mismatched data\n(?:\x1b\[0m)?/g;

const KINDS = ["lines", "functions", "regions"] as const;
type Counts = { count: number; covered: number };
type Summary = Record<(typeof KINDS)[number], Counts>;

// Runs an LLVM tool that has to succeed and returns its standard output.
function llvm(tool: string, args: string[]): string {
  const result = spawnSync(tool, args, {
    stdio: ["ignore", "pipe", "pipe"],
    encoding: "utf8",
    maxBuffer: Infinity,
  });
  if (result.error) {
    throw result.error;
  }
  process.stderr.write(result.stderr.replace(MISMATCH, ""));
  if (result.status !== 0) {
    throw new Error(
      `${tool} ${args[0]} failed with exit code ${result.status}`,
    );
  }
  return result.stdout;
}

function percent({ count, covered }: Counts): string {
  return `${((100 * covered) / count).toFixed(1)}% (${covered}/${count})`;
}

// Profiles left from an earlier run would be merged into this one's.
rmSync(PROFILES, { recursive: true, force: true });
rmSync(REPORT, { recursive: true, force: true });
mkdirSync(REPORT, { recursive: true });

const tests = spawnSync(
  "ctest",
  ["--preset", "coverage", ...process.argv.slice(2)],
  { cwd: ROOT, stdio: "inherit" },
);
if (tests.error) {
  throw tests.error;
}

// A process killed while writing its profile leaves it cut short, which the
// merge skips with a warning instead of failing on.
llvm("llvm-profdata", [
  "merge",
  "-sparse",
  "--failure-mode=all",
  "-o",
  PROFDATA,
  PROFILES,
]);

// Every program in the build links kotatsu, which instruments it: the test
// binaries and zest's runner fixture, the integration drivers, and the
// examples, those no test runs included, so that code only they instantiate
// counts as not covered instead of going missing.
//
// llvm-cov keeps the first record it reads of each function. It skips most
// placeholders as mismatched data, but takes one as never run when the real
// function's hash has the bit LLVM marks context-sensitive profiles with, so
// the order matters. The largest programs instantiate the most: reading them
// first takes nearly every function from a program that uses it.
const programs = [BUILD, join(BUILD, "examples")]
  .flatMap((dir) => readdirSync(dir).map((name) => join(dir, name)))
  .map((path) => ({ path, stat: statSync(path) }))
  .filter(({ stat }) => stat.isFile() && (stat.mode & 0o111) !== 0)
  .sort((a, b) => b.stat.size - a.stat.size)
  .map(({ path }) => path);
// Naming include/kota and src as the sources leaves out the tests, the
// examples, generated files and every dependency.
const inputs = [
  `-instr-profile=${PROFDATA}`,
  programs[0],
  ...programs.slice(1).flatMap((program) => ["-object", program]),
  join(ROOT, "include", "kota"),
  join(ROOT, "src"),
];

writeFileSync(
  join(REPORT, "coverage.lcov"),
  llvm("llvm-cov", ["export", "-format=lcov", ...inputs]),
);
llvm("llvm-cov", [
  "show",
  "-format=html",
  `-output-dir=${join(REPORT, "html")}`,
  "-show-directory-coverage",
  "-show-instantiations=false",
  ...inputs,
]);

const exported = JSON.parse(
  llvm("llvm-cov", ["export", "-summary-only", ...inputs]),
) as {
  data: [{ files: { filename: string; summary: Summary }[]; totals: Summary }];
};
const modules = new Map<string, Summary>();
for (const { filename, summary } of exported.data[0].files) {
  const [top, ...rest] = relative(ROOT, filename).split(sep);
  // include/kota/<module>/... or src/<module>/...
  const name = top === "include" ? rest[1] : rest[0];
  const sum = modules.get(name);
  if (sum === undefined) {
    modules.set(name, summary);
    continue;
  }
  for (const kind of KINDS) {
    sum[kind].count += summary[kind].count;
    sum[kind].covered += summary[kind].covered;
  }
}
const rows = [...modules]
  .sort(([a], [b]) => a.localeCompare(b))
  .concat([["**total**", exported.data[0].totals]]);
const table = [
  `| module | ${KINDS.join(" | ")} |`,
  `| :-- | ${KINDS.map(() => "--:").join(" | ")} |`,
  ...rows.map(
    ([name, summary]) =>
      `| ${name} | ${KINDS.map((kind) => percent(summary[kind])).join(" | ")} |`,
  ),
].join("\n");
writeFileSync(join(REPORT, "summary.md"), `${table}\n`);

console.log(`\n${table}\n\nReport in ${relative(ROOT, REPORT)}`);
process.exitCode = tests.status ?? 1;
