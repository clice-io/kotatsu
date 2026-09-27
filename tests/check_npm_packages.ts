// Loaded before every integration test file (see kota_add_integration_tests
// in tests/CMakeLists.txt), so that a checkout without its npm packages fails
// with the fix instead of a module resolution error.

import { existsSync } from "node:fs";

if (!existsSync("node_modules")) {
  console.error("node_modules is missing; run `pixi run npm-ci` first");
  process.exit(1);
}
