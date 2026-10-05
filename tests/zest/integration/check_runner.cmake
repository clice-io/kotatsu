# Runs zest_runner_fixture (path in FIXTURE; scratch space in WORK_DIR) and
# checks that every misbehaving test fails on its own while the run completes.
# CLI is a test program whose main() is zest's run_cli().

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

# `detail` is on the line after the status line starting with `status`.
function(expect_detail_of status detail)
    string(FIND "${output}" "${status}" status_at)
    if(status_at EQUAL -1)
        message(FATAL_ERROR "expected `${status}` in the output:\n${output}")
    endif()
    string(SUBSTRING "${output}" ${status_at} -1 rest)
    string(FIND "${rest}" "\n" end)
    math(EXPR next "${end} + 1")
    string(SUBSTRING "${rest}" ${next} -1 rest)
    string(FIND "${rest}" "\n" end)
    string(SUBSTRING "${rest}" 0 ${end} line)
    string(FIND "${line}" "${detail}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "expected `${detail}` under `${status}`:\n${output}")
    endif()
endfunction()

# Some part of the output matches the regular expression `pattern`.
function(expect_match pattern)
    string(REGEX MATCH "${pattern}" found "${output}")
    if(found STREQUAL "")
        message(FATAL_ERROR "expected a match of `${pattern}` in the output:\n${output}")
    endif()
endfunction()

# Each text is in the output after the one before it.
function(expect_in_order)
    set(from 0)
    foreach(text IN LISTS ARGN)
        string(SUBSTRING "${output}" ${from} -1 rest)
        string(FIND "${rest}" "${text}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "expected `${text}` after the text before it:\n${output}")
        endif()
        math(EXPR from "${from} + ${at}")
    endforeach()
endfunction()

string(ASCII 27 escape)
set(ok "${escape}[32m[       OK ]")
set(snapshots "${WORK_DIR}/snapshots")
file(REMOVE_RECURSE "${WORK_DIR}")

# The cases of a suite in an anonymous namespace and of one outside are all
# registered, even where the linker collected garbage (on Windows).
run_fixture("${FIXTURE}" --list-tests)
expect_output("fixture.passes")
expect_output("fixture_external.registered")
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

# One worker: the hang must not keep the next test from running. A crash test
# that only closes its channel is killed after --timeout, and does not pass.
run_fixture("${FIXTURE}" --list-tests --test-filter=fixture_hang.*)
string(FIND "${output}" "fixture_hang.closes_the_channel" closes_the_channel)
run_fixture("${FIXTURE}" --test-filter=fixture_hang.* --jobs=1 --timeout=2)
expect_code(1)
expect_output_of("[  TIMEOUT ] fixture_hang.hangs (" "printed by fixture_hang.hangs")
if(NOT closes_the_channel EQUAL -1)
    expect_detail_of("[  TIMEOUT ] fixture_hang.closes_the_channel ("
        "the worker stopped answering and did not exit")
endif()
expect_output("[  PASSED  ] 2 tests.")

# A worker that does not exit after its last test is killed once --timeout
# has passed, and reported.
run_fixture("${CMAKE_COMMAND}" -E env ZEST_FIXTURE_HANG_AT_EXIT=1
    "${FIXTURE}" --test-filter=fixture_hang.hangs_at_exit --timeout=2)
expect_code(1)
expect_output("[   WORKER ] a worker did not exit within --timeout after its last test")
expect_output("[  PASSED  ] 1 tests.")

# A LoopFixture's watchdog, set short by the test, cancels a task that would
# wait for ever, and fails the test; so does a test that stops the loop under
# run(). A task still running a period after the cancel ends the worker as a
# failed ZASSERT does.
run_fixture("${FIXTURE}" --test-filter=fixture_loop.* --jobs=1)
expect_code(1)
expect_output("[   FAILED ] fixture_loop.outlasts_the_watchdog (")
expect_output("context: tasks still running after 50ms were cancelled by the watchdog")
expect_output("[   FAILED ] fixture_loop.stopped_under_run (")
expect_output("context: run(): the loop was stopped with 1 tasks running")
expect_output("[   FAILED ] fixture_loop.outlives_its_cancel (")
expect_output("context: tasks still running 500ms after the watchdog cancelled them")
expect_no_output("printed after the watchdog ended the worker")
expect_no_output("CRASHED")

# A failed ZASSERT, in a test's body, a helper, another thread or a coroutine,
# fails its test and ends the worker after running the fatal hooks alive,
# newest first; the runner counts no crash and goes on with a fresh worker.
run_fixture("${FIXTURE}" --list-tests --test-filter=fixture_fatal*)
set(fatal_failures 7)
string(FIND "${output}" "fixture_fatal.hook_throws" hook_throws)
if(NOT hook_throws EQUAL -1)
    math(EXPR fatal_failures "${fatal_failures} + 1")
endif()
string(FIND "${output}" "fixture_fatal.report_throws" report_throws)
if(NOT report_throws EQUAL -1)
    math(EXPR fatal_failures "${fatal_failures} + 1")
endif()
run_fixture("${FIXTURE}" --test-filter=fixture_fatal* --jobs=1)
expect_code(1)
expect_output_of("[   FAILED ] fixture_fatal.in_body (" "second hook ran\nfirst hook ran")
expect_output("[   FAILED ] fixture_fatal.in_helper (")
expect_output("[   FAILED ] fixture_fatal.on_thread (")
expect_detail_of("[   FAILED ] fixture_fatal.hook_crashes (" "the fatal hooks ended the worker with ")
if(NOT hook_throws EQUAL -1)
    expect_output_of("[   FAILED ] fixture_fatal.hook_throws (" "hook older than the throw ran")
    expect_output("thrown by a hook")
endif()
if(NOT report_throws EQUAL -1)
    expect_output_of("[   FAILED ] fixture_fatal.report_throws ("
        "hook after a throwing report ran")
    expect_output("thrown by a report")
endif()
expect_output("[   FAILED ] fixture_fatal.hook_asserts (")
expect_output("[ expect ] 2 == 3")
expect_output("[   FAILED ] fixture_fatal.hook_out_of_scope (")
expect_output("[   FAILED ] fixture_fatal_loop.in_coroutine (")
expect_output("[  PASSED  ] 1 tests.")
expect_output("[  FAILED  ] ${fatal_failures} tests, listed below:")
expect_no_output("printed after a fatal")
expect_no_output("hook older than the assert ran")
expect_no_output("hook out of scope ran")
expect_no_output("CRASHED")
expect_no_output("[   WORKER ]")

# A fatal hook that does not return is given --timeout, then the worker is
# killed; the test still fails as its ZASSERT did.
run_fixture("${FIXTURE}" --test-filter=fixture_hook_hang.* --timeout=10)
expect_code(1)
expect_detail_of("[   FAILED ] fixture_hook_hang.hangs ("
    "the fatal hooks did not finish within --timeout")

# Without isolation, a failed ZASSERT ends the whole run once its hooks ran.
run_fixture("${FIXTURE}" --no-isolation --test-filter=fixture_fatal.* --verbose)
expect_code(86)
expect_in_order("[ expect ] 1 == 2" "[    FATAL ] fixture_fatal.in_body ended the run"
    "second hook ran\nfirst hook ran")
expect_no_output("fixture_fatal.in_helper")
expect_no_output("Global test environment tear-down")

# A crash test passes by killing its worker, a signal or a nonzero exit code
# alike, on a worker of its own, and fails if it finishes, exits cleanly or
# fails a check first.
run_fixture("${FIXTURE}" --test-filter=fixture_crash.* --jobs=1 --verbose)
expect_code(1)
expect_output_of("${ok} fixture_crash.aborts (" "printed by fixture_crash.aborts")
expect_detail_of("${ok} fixture_crash.exits_with_an_error (" "crashed as expected: exit code 4")
expect_output("[   FAILED ] fixture_crash.finishes (")
expect_output("fixture_crash.finishes (expected a crash)")
expect_output("[  CRASHED ] fixture_crash.exits_cleanly (")
expect_output("[   FAILED ] fixture_crash.asserts_first (")
expect_output("[ SKIPPED  ] fixture_crash.skips")
expect_output("[   FAILED ] fixture_crash.expects_then_crashes (")
expect_output("${ok} fixture_crash.marks_the_worker (")
expect_output("${ok} fixture_crash.aborts_in_a_fresh_worker (")
expect_output("[  PASSED  ] 4 tests.")
expect_output("[  FAILED  ] 4 tests, listed below:")
expect_output("[  SKIPPED ] 1 tests.")
expect_no_output("[   WORKER ]")

# Without isolation, crash tests are skipped.
run_fixture("${FIXTURE}" --no-isolation --test-filter=fixture_crash.aborts --verbose)
expect_code(0)
expect_output("[ SKIPPED  ] fixture_crash.aborts")
expect_output("a crash test runs only in a worker")

# Snapshots checked by different workers, a crash test's included, are all
# counted as checked: the stale one is rewritten, and only the orphan is
# cleaned up.
file(WRITE "${snapshots}/fixture_snapshot/checked.snap.yml" "stale")
file(WRITE "${snapshots}/fixture_snapshot/orphan.snap.yml" "orphan")
run_fixture("${FIXTURE}" --test-filter=fixture_snapshot.* --jobs=2 "--snapshot-dir=${snapshots}"
    --update-snapshots --cleanup-snapshots)
expect_code(0)
expect_output("[snapshot] cleaned up 1 orphaned file")
file(READ "${snapshots}/fixture_snapshot/checked.snap.yml" checked)
string(FIND "${checked}" "fresh" fresh_at)
if(fresh_at EQUAL -1 OR NOT EXISTS "${snapshots}/fixture_snapshot/also_checked.snap.yml"
   OR NOT EXISTS "${snapshots}/fixture_snapshot/checked_before_crashing.snap.yml"
   OR EXISTS "${snapshots}/fixture_snapshot/orphan.snap.yml")
    message(FATAL_ERROR "snapshots not updated and cleaned up as expected:\n${output}")
endif()

# A failed check shows its operands, a predicate's inputs, an unexpected's
# error and the contexts in scope, outermost first; a failed ZASSERT ends its
# test, a failed ZSTATIC_EXPECT is reported like any other check, a failed
# snapshot shows its contexts too, and throws() names what was thrown.
run_fixture("${FIXTURE}" --list-tests --test-filter=fixture_report.*)
set(report_failures 9)
string(FIND "${output}" "fixture_report.throws_nothing" throws_nothing)
if(NOT throws_nothing EQUAL -1)
    math(EXPR report_failures "${report_failures} + 1")
endif()
string(FIND "${output}" "fixture_report.throws_unexpectedly" throws_unexpectedly)
if(NOT throws_unexpectedly EQUAL -1)
    math(EXPR report_failures "${report_failures} + 1")
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
expect_output_of("[   FAILED ] fixture_report.continues_after_expect ("
    "printed after a failed expect")
expect_no_output("hook ran after an expect")
expect_output("[ expect ] snapshot(\"unchecked\")")
expect_output("no snapshot directory: run with --snapshot-dir")
expect_output("context: while taking a snapshot")
if(NOT throws_nothing EQUAL -1)
    expect_output("[ expect ] throws([] { return std::string(\"no exception\"); })")
    expect_output("nothing was thrown")
endif()
if(NOT throws_unexpectedly EQUAL -1)
    expect_match("thrown: [^\n]*: thrown on purpose")
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

# run_cli()'s command line: an option that does not parse is a usage error,
# exit code 2; --help prints the usage and runs no test.
run_fixture("${CLI}" --nope)
expect_code(2)
expect_output("Error parsing options: ")
expect_output("unknown option '--nope'")
run_fixture("${CLI}" --help)
expect_code(0)
expect_output("--test-filter")
expect_no_output("Global test environment")
