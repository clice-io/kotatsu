#pragma once

// missing_pid, the ID of no process.

namespace kotatsu::test {

/// A process ID no process has: above the largest Linux allows (2^22) and
/// macOS uses, and far above those Windows gives, whose OpenProcess ignores
/// the two low bits of an ID.
constexpr int missing_pid = 999'999'999;

}  // namespace kotatsu::test
