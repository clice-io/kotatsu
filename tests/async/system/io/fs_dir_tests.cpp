#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "async/harness/os.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

using Entries = std::vector<std::pair<std::string, fs::dirent::type>>;

Entries sorted(std::vector<fs::dirent> entries) {
    Entries out;
    for(auto& entry: entries) {
        out.emplace_back(std::move(entry.name), entry.kind);
    }
    std::ranges::sort(out);
    return out;
}

ZEST_SUITE(async_io_fs_dir, test::LoopFixture) {

ZEST_CASE(mkdir_makes_and_rmdir_removes_a_directory) {
    test::TempDir dir;
    auto path = dir.file("made");
    auto make_and_remove = [&]() -> task<bool, error> {
        co_await fs::mkdir(path, 0755).or_fail();
        bool made = std::filesystem::is_directory(path);
        co_await fs::rmdir(path).or_fail();
        co_return made;
    };

    auto [result] = run(make_and_remove());
    ASSERT(result.has_value());
    EXPECT(*result);
    EXPECT(!std::filesystem::exists(path));
}

ZEST_CASE(mkdir_of_an_existing_directory_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "taken");

    auto [result] = run(fs::mkdir(dir.file("taken"), 0755, loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::file_already_exists);
}

ZEST_CASE(rmdir_of_a_directory_with_entries_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "full");
    test::write_file(dir.path / "full" / "file.txt", "x");

    auto [result] = run(fs::rmdir(dir.file("full"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::directory_not_empty);
}

ZEST_CASE(rmdir_of_a_missing_directory_fails) {
    test::TempDir dir;
    auto [result] = run(fs::rmdir(dir.file("missing"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(mkdtemp_makes_a_new_directory_each_time) {
    test::TempDir dir;
    auto tpl = dir.file("tmp-XXXXXX");

    auto [first, second] = run(fs::mkdtemp(tpl, loop), fs::mkdtemp(tpl, loop));
    ASSERT(first.has_value());
    ASSERT(second.has_value());
    EXPECT(*first != *second);
    EXPECT(std::filesystem::is_directory(*first));
    EXPECT(std::filesystem::is_directory(*second));
    EXPECT(zest::starts_with(*first, dir.file("tmp-")));
}

ZEST_CASE(mkdtemp_without_a_template_fails) {
    test::TempDir dir;
    auto [result] = run(fs::mkdtemp(dir.file("tmp"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::invalid_argument);
}

ZEST_CASE(scandir_lists_the_entries_with_their_kind) {
    test::TempDir dir;
    test::write_file(dir.path / "file.txt", "x");
    std::filesystem::create_directory(dir.path / "sub");

    auto [result] = run(fs::scandir(dir.path.string(), loop));
    ASSERT(result.has_value());
    EXPECT(sorted(std::move(*result)) == Entries{
                                             {"file.txt", fs::dirent::type::file},
                                             {"sub",      fs::dirent::type::dir },
    });
}

ZEST_CASE(scandir_of_a_missing_directory_fails) {
    test::TempDir dir;
    auto [result] = run(fs::scandir(dir.file("missing"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

ZEST_CASE(opendir_readdir_and_closedir_walk_a_directory) {
    test::TempDir dir;
    test::write_file(dir.path / "a.txt", "x");
    test::write_file(dir.path / "b.txt", "x");
    auto walk = [&]() -> task<std::vector<fs::dirent>, error> {
        std::vector<fs::dirent> entries;
        auto handle = co_await fs::opendir(dir.path.string()).or_fail();
        while(true) {
            auto batch = co_await fs::readdir(handle).or_fail();
            if(batch.empty()) {
                break;
            }
            entries.insert(entries.end(), batch.begin(), batch.end());
        }
        co_await fs::closedir(handle).or_fail();
        co_return entries;
    };

    auto [result] = run(walk());
    ASSERT(result.has_value());
    EXPECT(sorted(std::move(*result)) == Entries{
                                             {"a.txt", fs::dirent::type::file},
                                             {"b.txt", fs::dirent::type::file},
    });
}

ZEST_CASE(opendir_of_a_missing_directory_fails) {
    test::TempDir dir;
    auto [result] = run(fs::opendir(dir.file("missing"), loop));
    ASSERT(result.has_error());
    EXPECT(result.error() == error::no_such_file_or_directory);
}

// closedir() leaves the handle empty; reading or closing it again fails.
ZEST_CASE(closed_directory_handle_fails) {
    test::TempDir dir;

    struct Seen {
        bool valid_after_close = true;
        error read_again;
        error close_again;
    };

    auto close_twice = [&]() -> task<Seen, error> {
        Seen seen;
        auto handle = co_await fs::opendir(dir.path.string()).or_fail();
        co_await fs::closedir(handle).or_fail();
        seen.valid_after_close = handle.valid();
        auto again = co_await fs::readdir(handle);
        seen.read_again = again.has_error() ? again.error() : error();
        auto closed = co_await fs::closedir(handle);
        seen.close_again = closed.has_error() ? closed.error() : error();
        co_return seen;
    };

    auto [result] = run(close_twice());
    ASSERT(result.has_value());
    EXPECT(!result->valid_after_close);
    EXPECT(result->read_again == error::invalid_argument);
    EXPECT(result->close_again == error::invalid_argument);
}

};  // ZEST_SUITE(async_io_fs_dir)

}  // namespace

}  // namespace kota
