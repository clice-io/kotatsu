#pragma once

#include "kota/zest/assert/check.h"
#include "kota/zest/assert/predicates.h"
#include "kota/zest/assert/trace.h"
#include "kota/zest/macro.h"
#include "kota/zest/runner/run.h"
#include "kota/zest/runner/suite.h"
#include "kota/zest/snapshot/snapshot.h"

// No JSON codec include here on purpose. zest does not depend on it — the
// EXPECT_SNAPSHOT_JSON family just names ::kota::codec::json in its expansion,
// so a test that wants those macros includes "kota/codec/json/json.h" itself.
