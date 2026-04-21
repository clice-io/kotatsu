#include "kota/zest/zest.h"
#include "kota/async/io/system.h"

namespace kota {

TEST_SUITE(system_info) {

TEST_CASE(current_pid_positive) {
    EXPECT_GT(sys::current_pid(), 0);
}

TEST_CASE(memory_sane) {
    auto info = sys::memory();
    EXPECT_TRUE(info.total > 0);
    EXPECT_TRUE(info.free > 0);
    EXPECT_TRUE(info.available > 0);
    EXPECT_TRUE(info.free <= info.total);
    EXPECT_TRUE(info.available <= info.total);
}

TEST_CASE(resident_memory) {
    auto rss = sys::resident_memory();
    ASSERT_TRUE(rss.has_value());
    EXPECT_TRUE(*rss > 0);
}

TEST_CASE(process_self) {
    auto stat = sys::process();
    ASSERT_TRUE(stat.has_value());
    EXPECT_EQ(stat->pid, sys::current_pid());
    EXPECT_GT(stat->rss, std::size_t{0});
    EXPECT_GT(stat->vsize, std::size_t{0});
    EXPECT_GT(stat->max_rss, std::size_t{0});
}

TEST_CASE(process_by_pid) {
    auto pid = sys::current_pid();
    auto stat = sys::process(pid);
    ASSERT_TRUE(stat.has_value());
    EXPECT_EQ(stat->pid, pid);
    EXPECT_GT(stat->rss, std::size_t{0});
}

TEST_CASE(process_invalid_pid) {
    auto stat = sys::process(999999999);
    EXPECT_FALSE(stat.has_value());
}

TEST_CASE(cpu_cores_populated) {
    auto cores = sys::cpu_cores();
    ASSERT_TRUE(cores.has_value());
    EXPECT_TRUE(!cores->empty());
    // speed_mhz may be 0 on some virtualized environments.
    for(auto& core: *cores) {
        EXPECT_TRUE(!core.model.empty());
        EXPECT_TRUE(core.speed_mhz >= 0);
    }
}

TEST_CASE(parallelism_positive) {
    EXPECT_TRUE(sys::parallelism() >= 1);
}

TEST_CASE(uname_populated) {
    auto name = sys::uname();
    ASSERT_TRUE(name.has_value());
    EXPECT_TRUE(!name->sysname.empty());
    EXPECT_TRUE(!name->machine.empty());
}

TEST_CASE(hostname_nonempty) {
    auto host = sys::hostname();
    ASSERT_TRUE(host.has_value());
    EXPECT_TRUE(!host->empty());
}

TEST_CASE(uptime_positive) {
    auto up = sys::uptime();
    ASSERT_TRUE(up.has_value());
    EXPECT_TRUE(up->count() > 0);
}

TEST_CASE(home_directory_nonempty) {
    auto home = sys::home_directory();
    ASSERT_TRUE(home.has_value());
    EXPECT_TRUE(!home->empty());
}

TEST_CASE(temp_directory_nonempty) {
    auto tmp = sys::temp_directory();
    ASSERT_TRUE(tmp.has_value());
    EXPECT_TRUE(!tmp->empty());
}

TEST_CASE(priority_round_trip) {
    auto orig = sys::priority();
    ASSERT_TRUE(orig.has_value());

#ifdef _WIN32
    // Windows maps nice values to discrete priority classes;
    // only class boundary values (0, 10, 19, ...) round-trip exactly.
    auto err = sys::set_priority(0);
    EXPECT_TRUE(!err.has_error());
    auto changed = sys::priority();
    ASSERT_TRUE(changed.has_value());
    EXPECT_EQ(*changed, 0);
    sys::set_priority(*orig);
#else
    auto err = sys::set_priority(*orig + 1);
    EXPECT_TRUE(!err.has_error());

    auto changed = sys::priority();
    ASSERT_TRUE(changed.has_value());
    EXPECT_EQ(*changed, *orig + 1);

    sys::set_priority(*orig);
#endif
}

};  // TEST_SUITE(system_info)

}  // namespace kota
