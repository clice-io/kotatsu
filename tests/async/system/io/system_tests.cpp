#include <cstddef>
#include <filesystem>

#include "async/harness/loop_fixture.h"
#include "async/harness/os.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

// A pid no process has: above the largest Linux allows (2^22) and macOS
// uses, and not a multiple of four, which every Windows pid is.
constexpr int missing_pid = 999'999'999;

ZEST_SUITE(async_io_system, test::LoopFixture) {

ZEST_CASE(pid_is_this_process) {
    EXPECT(sys::pid() > 0);
    auto self = sys::process();
    ASSERT(self.has_value());
    EXPECT(self->pid == sys::pid());
}

ZEST_CASE(memory_figures_are_consistent) {
    auto info = sys::memory();
    EXPECT(info.total > 0U);
    EXPECT(info.free <= info.total);
    EXPECT(info.available <= info.total);

    auto rss = sys::resident_memory();
    ASSERT(rss.has_value());
    EXPECT(*rss > 0U);
}

ZEST_CASE(process_describes_this_process) {
    auto self = sys::process();
    ASSERT(self.has_value());
    EXPECT(self->rss > 0U);
    EXPECT(self->vsize > 0U);
    EXPECT(self->max_rss > 0U);

    auto by_pid = sys::process(sys::pid());
    ASSERT(by_pid.has_value());
    EXPECT(by_pid->pid == sys::pid());
    EXPECT(by_pid->rss > 0U);
}

ZEST_CASE(process_describes_a_child) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    auto pid = spawned->proc.pid();

    auto child = sys::process(pid);
    // Closing its stdin ends the child.
    spawned->stdin_pipe = pipe{};
    auto [status] = run(spawned->proc.wait());
    EXPECT(test::exit_status_of(status) == 0);
    ASSERT(child.has_value());
    EXPECT(child->pid == pid);
    EXPECT(child->rss > 0U);
}

ZEST_CASE(process_of_a_missing_pid_fails) {
    auto stat = sys::process(missing_pid);
    ASSERT(stat.has_error());
    EXPECT(stat.error() == error::no_such_process);
}

ZEST_CASE(cpu_cores_are_listed) {
    auto cores = sys::cpu_cores();
    ASSERT(cores.has_value());
    EXPECT(!cores->empty());
    EXPECT(sys::parallelism() >= 1U);
    for(std::size_t i = 0; i < cores->size(); ++i) {
        ZEST_CONTEXT("core {}", i);
        EXPECT(!(*cores)[i].model.empty());
        // Virtual machines may report no clock speed.
        EXPECT((*cores)[i].speed_mhz >= 0);
    }
}

ZEST_CASE(uname_names_the_system) {
    auto name = sys::uname();
    ASSERT(name.has_value());
    EXPECT(!name->sysname.empty());
    EXPECT(!name->release.empty());
    EXPECT(!name->machine.empty());
}

ZEST_CASE(hostname_and_uptime_are_reported) {
    auto host = sys::hostname();
    ASSERT(host.has_value());
    EXPECT(!host->empty());

    auto up = sys::uptime();
    ASSERT(up.has_value());
    EXPECT(up->count() > 0);
}

ZEST_CASE(directories_are_reported) {
    auto home = sys::home_directory();
    ASSERT(home.has_value());
    EXPECT(!home->empty());

    auto tmp = sys::temp_directory();
    ASSERT(tmp.has_value());
    EXPECT(std::filesystem::is_directory(*tmp));
}

ZEST_CASE(executable_path_names_this_program) {
    auto path = sys::executable_path();
    ASSERT(path.has_value());
    EXPECT(std::filesystem::path(*path).stem().string() == "system_tests");
}

// Setting the priority it already has leaves the process as it was.
ZEST_CASE(priority_is_read_and_set) {
    auto original = sys::priority();
    ASSERT(original.has_value());
    EXPECT(!sys::set_priority(*original));
    auto again = sys::priority();
    ASSERT(again.has_value());
    EXPECT(*again == *original);
}

// Windows answers a pid it cannot open with ERROR_INVALID_PARAMETER.
ZEST_CASE(priority_of_a_missing_pid_fails) {
#ifdef _WIN32
    const auto missing = error::invalid_argument;
#else
    const auto missing = error::no_such_process;
#endif
    auto read = sys::priority(missing_pid);
    ASSERT(read.has_error());
    EXPECT(read.error() == missing);
    EXPECT(sys::set_priority(0, missing_pid) == missing);
}

// Lowering a child's priority needs no privilege; Windows maps the value to
// a priority class, so only POSIX reads back what it set.
#ifndef _WIN32
ZEST_CASE(priority_of_a_child_is_set) {
    auto spawned = process::spawn(test::stdin_reader(), loop);
    ASSERT(spawned.has_value());
    auto pid = spawned->proc.pid();

    auto set = sys::set_priority(10, pid);
    auto read = sys::priority(pid);
    // Closing its stdin ends the child.
    spawned->stdin_pipe = pipe{};
    auto [status] = run(spawned->proc.wait());
    EXPECT(test::exit_status_of(status) == 0);
    EXPECT(!set);
    ASSERT(read.has_value());
    EXPECT(*read == 10);
}
#endif

};  // ZEST_SUITE(async_io_system)

}  // namespace

}  // namespace kota
