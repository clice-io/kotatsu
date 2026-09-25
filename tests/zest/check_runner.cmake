# Runs zest_runner_fixture (path in FIXTURE) and checks that every misbehaving
# test fails on its own while the run still completes.

function(run_fixture)
    execute_process(
        COMMAND "${FIXTURE}" ${ARGN}
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
        RESULT_VARIABLE code
    )
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

run_fixture(--list-tests)
string(FIND "${output}" "fixture.throws" at)
if(at EQUAL -1)
    set(failures 5)
else()
    set(failures 6)
endif()

# With two workers, each outlives some of the tests that
# kill workers, so replacements must pick up where they left off.
run_fixture(--jobs=2 --timeout=2 --verbose)
expect_code(1)
expect_output("[       OK ] fixture.passes (")
string(ASCII 27 escape)
expect_output("printed by fixture.prints\n${escape}[32m[       OK ] fixture.prints (")
expect_output("[       OK ] fixture.case_0 (")
expect_output("[       OK ] fixture.case_1 (")
expect_output("[       OK ] fixture.case_2 (")
expect_output("[ SKIPPED  ] fixture.skips")
expect_output("[   FAILED ] fixture.fails (")
expect_output("[   FAILED ] fixture.fails_on_thread (")
expect_output("[  CRASHED ] fixture.aborts (")
expect_output("[  CRASHED ] fixture.exits_early (")
expect_output("exit code 0 before the test finished")
expect_output("[  TIMEOUT ] fixture.hangs (")
if(failures EQUAL 6)
    expect_output("[   FAILED ] fixture.throws (")
    expect_output("thrown by the test")
endif()
expect_output("[  PASSED  ] 6 tests.")
expect_output("[   WORKER ] a worker ended with exit code 3 after its last test")
expect_output("[  SKIPPED ] 1 tests.")
expect_output("[  FAILED  ] ${failures} tests, listed below:")

run_fixture(--no-isolation --test-filter=fixture.fails_on_thread)
expect_code(1)
expect_output("[   FAILED ] fixture.fails_on_thread (")
