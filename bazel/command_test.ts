// Runs the command of a command_test (bazel/command_test.bzl), given the path
// of its configuration; extra arguments go after the command's own. Bazel
// starts a test in its runfiles, where every path of the configuration points.

import { spawnSync } from "node:child_process";
import { readFileSync, realpathSync } from "node:fs";
import { dirname, resolve } from "node:path";

type Config = {
  command: string[];
  env: Record<string, string>;
  source_tree: boolean;
};

const config = JSON.parse(readFileSync(process.argv[2], "utf8")) as Config;

const env = { ...process.env };
for (const [name, path] of Object.entries(config.env)) {
  env[name] = resolve(path);
}

// MODULE.bazel in the runfiles links to the source tree's.
const cwd = config.source_tree
  ? dirname(realpathSync.native("MODULE.bazel"))
  : process.cwd();

// ${TEST_TMPDIR} and the like name a variable of the test's environment.
const expand = (arg: string) =>
  arg.replace(/\$\{(\w+)\}/g, (_, name: string) => env[name] ?? "");

const [program, ...args] = config.command.map(expand);
const result = spawnSync(
  program === "node" ? process.execPath : program,
  [...args, ...process.argv.slice(3)],
  { cwd, env, stdio: "inherit" },
);
if (result.error !== undefined) {
  console.error(`cannot run ${program}: ${result.error.message}`);
}
process.exit(result.status ?? 1);
