#include <filesystem>

#include "kota/zest/zest.h"
#include "kota/async/io/system.h"

namespace kota {

ZEST_SUITE(async_io_system) {

ZEST_CASE(pid_positive) {
    EXPECT(sys::pid() > 0);
}

ZEST_CASE(memory_sane) {
    auto info = sys::memory();
    // These may return 0 on platforms where the value is unknown.
    if(info.total != 0) {
        EXPECT(info.free <= info.total);
        EXPECT(info.available <= info.total);
    }
}

ZEST_CASE(resident_memory) {
    auto rss = sys::resident_memory();
    ASSERT(rss);
    EXPECT(*rss > 0);
}

ZEST_CASE(process_self) {
    auto stat = sys::process();
    ASSERT(stat);
    EXPECT(stat->pid == sys::pid());
    EXPECT(stat->rss > std::size_t{0});
    EXPECT(stat->vsize > std::size_t{0});
    EXPECT(stat->max_rss > std::size_t{0});
}

ZEST_CASE(process_by_pid) {
    auto pid = sys::pid();
    auto stat = sys::process(pid);
    ASSERT(stat);
    EXPECT(stat->pid == pid);
    EXPECT(stat->rss > std::size_t{0});
}

ZEST_CASE(process_invalid_pid) {
    auto stat = sys::process(999999999);
    EXPECT(!stat);
}

ZEST_CASE(cpu_cores_populated) {
    auto cores = sys::cpu_cores();
    ASSERT(cores);
    EXPECT(!cores->empty());
    // speed_mhz may be 0 on some virtualized environments.
    for(auto& core: *cores) {
        EXPECT(!core.model.empty());
        EXPECT(core.speed_mhz >= 0);
    }
}

ZEST_CASE(parallelism_positive) {
    EXPECT(sys::parallelism() >= 1);
}

ZEST_CASE(uname_populated) {
    auto name = sys::uname();
    ASSERT(name);
    EXPECT(!name->sysname.empty());
    EXPECT(!name->machine.empty());
}

ZEST_CASE(hostname_nonempty) {
    auto host = sys::hostname();
    ASSERT(host);
    EXPECT(!host->empty());
}

ZEST_CASE(uptime_positive) {
    auto up = sys::uptime();
    ASSERT(up);
    EXPECT(up->count() > 0);
}

ZEST_CASE(home_directory_nonempty) {
    auto home = sys::home_directory();
    ASSERT(home);
    EXPECT(!home->empty());
}

ZEST_CASE(executable_path_names_this_program) {
    auto path = sys::executable_path();
    ASSERT(path);
    EXPECT(std::filesystem::path(*path).stem().string() == "system_tests");
}

ZEST_CASE(temp_directory_nonempty) {
    auto tmp = sys::temp_directory();
    ASSERT(tmp);
    EXPECT(!tmp->empty());
}

ZEST_CASE(priority_round_trip) {
    auto orig = sys::priority();
    ASSERT(orig);

    // Set to same value (no-op) — verifies the setter without altering state.
    auto err = sys::set_priority(*orig);
    EXPECT(!err.has_error());

    auto changed = sys::priority();
    ASSERT(changed);
    EXPECT(*changed == *orig);
}

};  // ZEST_SUITE(async_io_system)

}  // namespace kota
