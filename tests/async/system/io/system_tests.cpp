#include <cstddef>
#include <filesystem>

#include "async/harness/os.h"
#include "kota/zest/async.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// A pid no process has: above the largest Linux allows (2^22) and macOS
// uses, and not a multiple of four, which every Windows pid is.
constexpr int missing_pid = 999'999'999;

ZEST_SUITE(async_io_system, zest::LoopFixture) {

ZEST_CASE(pid_is_this_process) {
    ZEXPECT(sys::pid() > 0);
    auto self = sys::process();
    ZASSERT(self.has_value());
    ZEXPECT(self->pid == sys::pid());
}

ZEST_CASE(memory_figures_are_consistent) {
    auto info = sys::memory();
    ZEXPECT(info.total > 0U);
    ZEXPECT(info.free <= info.total);
    ZEXPECT(info.available <= info.total);

    auto rss = sys::resident_memory();
    ZASSERT(rss.has_value());
    ZEXPECT(*rss > 0U);
}

ZEST_CASE(process_describes_this_process) {
    auto self = sys::process();
    ZASSERT(self.has_value());
    ZEXPECT(self->rss > 0U);
    ZEXPECT(self->vsize > 0U);
    ZEXPECT(self->max_rss > 0U);

    auto by_pid = sys::process(sys::pid());
    ZASSERT(by_pid.has_value());
    ZEXPECT(by_pid->pid == sys::pid());
    ZEXPECT(by_pid->rss > 0U);
}

ZEST_CASE(process_describes_a_child) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    auto pid = spawned->proc.pid();

    auto child = sys::process(pid);
    // Closing its stdin ends the child.
    spawned->stdin_pipe = pipe{};
    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
    ZASSERT(child.has_value());
    ZEXPECT(child->pid == pid);
    ZEXPECT(child->rss > 0U);
}

ZEST_CASE(process_of_a_missing_pid_fails) {
    auto stat = sys::process(missing_pid);
    ZASSERT(stat.has_error());
    ZEXPECT(stat.error() == error::no_such_process);
}

ZEST_CASE(cpu_cores_are_listed) {
    auto cores = sys::cpu_cores();
    ZASSERT(cores.has_value());
    ZEXPECT(!cores->empty());
    ZEXPECT(sys::parallelism() >= 1U);
    for(std::size_t i = 0; i < cores->size(); ++i) {
        ZEST_CONTEXT("core {}", i);
        ZEXPECT(!(*cores)[i].model.empty());
        // Virtual machines may report no clock speed.
        ZEXPECT((*cores)[i].speed_mhz >= 0);
    }
}

ZEST_CASE(uname_names_the_system) {
    auto name = sys::uname();
    ZASSERT(name.has_value());
    ZEXPECT(!name->sysname.empty());
    ZEXPECT(!name->release.empty());
    ZEXPECT(!name->machine.empty());
}

ZEST_CASE(hostname_and_uptime_are_reported) {
    auto host = sys::hostname();
    ZASSERT(host.has_value());
    ZEXPECT(!host->empty());

    auto up = sys::uptime();
    ZASSERT(up.has_value());
    ZEXPECT(up->count() > 0);
}

ZEST_CASE(directories_are_reported) {
    auto home = sys::home_directory();
    ZASSERT(home.has_value());
    ZEXPECT(!home->empty());

    auto tmp = sys::temp_directory();
    ZASSERT(tmp.has_value());
    ZEXPECT(std::filesystem::is_directory(*tmp));
}

// CMake builds system_tests, Bazel a binary per module: async_system_tests.
ZEST_CASE(executable_path_names_this_program) {
    auto path = sys::executable_path();
    ZASSERT(path.has_value());
    ZEXPECT(std::filesystem::path(*path).stem().string().ends_with("system_tests"));
}

// Setting the priority it already has leaves the process as it was.
ZEST_CASE(priority_is_read_and_set) {
    auto original = sys::priority();
    ZASSERT(original.has_value());
    ZEXPECT(!sys::set_priority(*original));
    auto again = sys::priority();
    ZASSERT(again.has_value());
    ZEXPECT(*again == *original);
}

// Windows answers a pid it cannot open with ERROR_INVALID_PARAMETER.
ZEST_CASE(priority_of_a_missing_pid_fails) {
    // Windows reports the pid OpenProcess rejects as ESRCH too.
    auto read = sys::priority(missing_pid);
    ZASSERT(read.has_error());
    ZEXPECT(read.error() == error::no_such_process);
    ZEXPECT(sys::set_priority(0, missing_pid) == error::no_such_process);
}

// Lowering a child's priority needs no privilege; Windows maps the value to
// a priority class, so only POSIX reads back what it set.
#ifndef _WIN32
ZEST_CASE(priority_of_a_child_is_set) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ZASSERT(spawned.has_value());
    auto pid = spawned->proc.pid();

    auto set = sys::set_priority(10, pid);
    auto read = sys::priority(pid);
    // Closing its stdin ends the child.
    spawned->stdin_pipe = pipe{};
    auto [status] = run(spawned->proc.wait());
    ZEXPECT(test::exit_status_of(status) == 0);
    ZEXPECT(!set);
    ZASSERT(read.has_value());
    ZEXPECT(*read == 10);
}
#endif

};  // ZEST_SUITE(async_io_system)

}  // namespace

}  // namespace kota
