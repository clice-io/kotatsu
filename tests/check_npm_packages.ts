// Loaded before every integration test file (see kota_add_integration_tests
// in tests/CMakeLists.txt), so that a checkout whose npm packages are missing
// or older than package-lock.json fails with the fix instead of a module
// resolution error.

import { readFileSync } from "node:fs";

type Lock = { packages: Record<string, { version: string }> };

function readJson(path: string): unknown {
  try {
    return JSON.parse(readFileSync(path, "utf8"));
  } catch {
    return undefined;
  }
}

const manifest = readJson("package.json") as {
  devDependencies: Record<string, string>;
};
const lock = readJson("package-lock.json") as Lock;
const stale = Object.keys(manifest.devDependencies).filter((name) => {
  const installed = readJson(`node_modules/${name}/package.json`) as {
    version?: string;
  };
  return installed?.version !== lock.packages[`node_modules/${name}`]?.version;
});
if (stale.length > 0) {
  console.error(
    `npm packages missing or stale (${stale.join(", ")}); run \`pixi run npm-ci\``,
  );
  process.exit(1);
}
