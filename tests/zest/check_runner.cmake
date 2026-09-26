# Runs zest_runner_fixture (path in FIXTURE; scratch space in WORK_DIR) and
# checks that every misbehaving test fails on its own while the run completes.

function(run_fixture)
    execute_process(
        COMMAND ${ARGN}
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
        RESULT_VARIABLE code
    )
    # Windows writes text with CRLF line ends.
    string(REPLACE "\r" "" output "${output}")
    set(output "${output}" PARENT_SCOPE)
    set(code "${code}" PARENT_SCOPE)
endfunction()

function(expect_code expected)
    if(NOT code STREQUAL expected)
        message(FATAL_ERROR "expected exit code ${expected}, got ${code}:\n${output}")
    endif()
endfunction()

function(expect_output text)
    string(FIND "${output}" "${text}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "expected `${text}` in the output:\n${output}")
    endif()
endfunction()

function(expect_no_output text)
    string(FIND "${output}" "${text}" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "expected no `${text}` in the output:\n${output}")
    endif()
endfunction()

# `text`, printed by a test, is shown before the status line starting with
# `status` and after every other test's status line.
function(expect_output_of status text)
    string(FIND "${output}" "${text}" text_at)
    string(FIND "${output}" "${status}" status_at)
    if(text_at EQUAL -1 OR status_at EQUAL -1 OR text_at GREATER status_at)
        message(FATAL_ERROR "expected `${text}` before `${status}`:\n${output}")
    endif()
    math(EXPR length "${status_at} - ${text_at}")
    string(SUBSTRING "${output}" ${text_at} ${length} between)
    string(FIND "${between}" "] fixture" other)
    if(NOT other EQUAL -1)
        message(FATAL_ERROR "`${text}` is not attributed to `${status}`:\n${output}")
    endif()
endfunction()

string(ASCII 27 escape)
set(ok "${escape}[32m[       OK ]")
set(snapshots "${WORK_DIR}/snapshots")
file(REMOVE_RECURSE "${WORK_DIR}")

run_fixture("${FIXTURE}" --list-tests)
string(FIND "${output}" "fixture.throws" at)
if(at EQUAL -1)
    set(failures 4)
else()
    set(failures 5)
endif()

# With two workers, each outlives some of the tests that kill workers, so
# replacements must pick up where they left off.
run_fixture("${FIXTURE}" --test-filter=fixture.* --jobs=2 --timeout=30 --verbose
    "--snapshot-dir=${snapshots}" --cleanup-snapshots)
expect_code(1)
expect_output("${ok} fixture.passes (")
expect_output_of("${ok} fixture.prints (" "printed by fixture.prints")
expect_output("${ok} fixture.case_0 (")
expect_output("${ok} fixture.case_1 (")
expect_output("${ok} fixture.case_2 (")
expect_output("[ SKIPPED  ] fixture.skips")
expect_output("[   FAILED ] fixture.fails (")
expect_output("[   FAILED ] fixture.fails_on_thread (")
expect_output_of("[  CRASHED ] fixture.aborts (" "printed by fixture.aborts")
expect_output("[  CRASHED ] fixture.exits_early (")
expect_output("exit code 0 before the test finished")
if(failures EQUAL 5)
    expect_output("[   FAILED ] fixture.throws (")
    expect_output("thrown by the test")
endif()
expect_output("[   WORKER ] a worker ended with exit code 3 after its last test")
expect_output("[snapshot] cleanup skipped: some tests failed")
expect_output("[  PASSED  ] 6 tests.")
expect_output("[  SKIPPED ] 1 tests.")
expect_output("[  FAILED  ] ${failures} tests, listed below:")
# The serial test runs once everything else is done.
set(serial_line "${ok} fixture.fails_at_exit (")
string(FIND "${output}" "${serial_line}" serial_at)
string(FIND "${output}" "Global test environment tear-down" teardown_at)
string(LENGTH "${serial_line}" serial_length)
math(EXPR after_serial "${serial_at} + ${serial_length}")
math(EXPR between "${teardown_at} - ${after_serial}")
string(SUBSTRING "${output}" ${after_serial} ${between} after)
string(FIND "${after}" "] fixture." later)
if(serial_at EQUAL -1 OR NOT later EQUAL -1)
    message(FATAL_ERROR "fixture.fails_at_exit did not run last:\n${output}")
endif()

# One worker: the hang must not keep the next test from running.
run_fixture("${FIXTURE}" --test-filter=fixture_hang.* --jobs=1 --timeout=2)
expect_code(1)
expect_output_of("[  TIMEOUT ] fixture_hang.hangs (" "printed by fixture_hang.hangs")
expect_output("[  PASSED  ] 1 tests.")

# Snapshots checked by different workers are all counted as checked: the
# stale one is rewritten, and only the orphan is cleaned up.
file(WRITE "${snapshots}/fixture_snapshot/checked.snap.yml" "stale")
file(WRITE "${snapshots}/fixture_snapshot/orphan.snap.yml" "orphan")
run_fixture("${FIXTURE}" --test-filter=fixture_snapshot.* --jobs=2 "--snapshot-dir=${snapshots}"
    --update-snapshots --cleanup-snapshots)
expect_code(0)
expect_output("[snapshot] cleaned up 1 orphaned file")
file(READ "${snapshots}/fixture_snapshot/checked.snap.yml" checked)
string(FIND "${checked}" "fresh" fresh_at)
if(fresh_at EQUAL -1 OR NOT EXISTS "${snapshots}/fixture_snapshot/also_checked.snap.yml"
   OR EXISTS "${snapshots}/fixture_snapshot/orphan.snap.yml")
    message(FATAL_ERROR "snapshots not updated and cleaned up as expected:\n${output}")
endif()

# A failed check shows its operands, a predicate's inputs, an unexpected's
# error and the contexts in scope, outermost first; a failed ASSERT ends its
# test, a failed STATIC_EXPECT is reported like any other check, and a failed
# snapshot shows its contexts too.
run_fixture("${FIXTURE}" --list-tests --test-filter=fixture_report.*)
string(FIND "${output}" "fixture_report.throws_nothing" at)
if(at EQUAL -1)
    set(report_failures 8)
else()
    set(report_failures 9)
endif()
run_fixture("${FIXTURE}" --test-filter=fixture_report.* --jobs=1)
expect_code(1)
expect_output("[ expect ] std::string(\"left\") == \"right\"")
expect_output("lhs: \"left\"")
expect_output("rhs: \"right\"")
expect_output("haystack: \"haystack\"")
expect_output("needle: \"needle\"")
expect_output("got: \"boom\"")
expect_output("context: while checking 42\n           context: inner")
expect_output("[ expect ] !contains(std::string(\"haystack\"), \"hay\")")
expect_output("needle: \"hay\"")
expect_output("[ expect ] 1 + 1 == 3")
expect_output("lhs: 2")
expect_no_output("printed after a failed assert")
expect_output("context: while taking a snapshot")
if(report_failures EQUAL 9)
    expect_output("[ expect ] std::string(\"no exception\")")
    expect_output("expected to throw")
endif()
expect_output("[  FAILED  ] ${report_failures} tests, listed below:")

# The program's own flag reaches the workers, which here refuse to start.
run_fixture("${FIXTURE}" --test-filter=fixture.passes --fail-worker-start)
expect_code(1)
expect_output("Error: a worker ended while starting with exit code 7")

run_fixture("${CMAKE_COMMAND}" -E env ZEST_FIXTURE_DUPLICATE=1 "${FIXTURE}")
expect_code(1)
expect_output("more than one test is named fixture.passes")

run_fixture("${FIXTURE}" --no-isolation --test-filter=fixture.fails_on_thread)
expect_code(1)
expect_output("[   FAILED ] fixture.fails_on_thread (")
