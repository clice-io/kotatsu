#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/directory.h"
#include "kota/fs/path.h"
#include "kota/fs/resolve.h"
#include "kota/fs/status.h"
#include "kota/fs/temp.h"

namespace kota::fs {

namespace {

/// What canonical() names `path`.
std::string canonical_of(std::string_view path) {
    std::string real;
    ZASSERT(!canonical(real, path));
    return real;
}

ZEST_SUITE(fs_temp) {

ZEST_CASE(temp_directory_goes_with_what_it_holds) {
    test::TempDir dir;
    std::string held;
    {
        auto temp = TempDirectory::create(dir.root(), "case-");
        ZASSERT(temp.has_value());
        held = temp->path();
        ZEXPECT(is_directory(held));
        ZEXPECT(path::filename(held).starts_with("case-"));
        std::string file = held;
        path::push(file, "file.txt");
        test::write_text(file, "text");
    }
    ZEXPECT(try_exists(held) == false);
}

ZEST_CASE(temp_directory_is_named_as_canonical_names_it) {
    // macOS's temp directory lies behind /var, a link to /private/var, and
    // Windows hands out 8.3 names: either would spell it otherwise.
    test::TempDir dir;
    auto made = TempDirectory::create(dir.root(), "case-");
    ZASSERT(made.has_value());
    ZEXPECT(made->path() == canonical_of(made->path()));
    ZEXPECT(path::parent(made->path()) == canonical_of(dir.root()));
}

#ifndef _WIN32
ZEST_CASE(temp_directory_is_its_owners_alone) {
    test::TempDir dir;
    auto temp = TempDirectory::create(dir.root(), "case-");
    ZASSERT(temp.has_value());
    auto found = status(temp->path());
    ZASSERT(found.has_value());
    ZEXPECT(found->permissions == 0700);
}
#endif

ZEST_CASE(temp_directory_removes_now_and_once) {
    test::TempDir dir;
    auto temp = TempDirectory::create(dir.root(), "case-");
    ZASSERT(temp.has_value());
    std::string held(temp->path());
    ZEXPECT(!temp->remove());
    ZEXPECT(try_exists(held) == false);
    ZEXPECT(temp->path().empty());
    ZEXPECT(!temp->remove());
}

ZEST_CASE(temp_directory_kept_stays) {
    test::TempDir dir;
    std::string kept;
    {
        auto temp = TempDirectory::create(dir.root(), "case-");
        ZASSERT(temp.has_value());
        std::string held(temp->path());
        kept = std::move(*temp).keep();
        ZEXPECT(kept == held);
        ZEXPECT(temp->path().empty());
    }
    ZEXPECT(is_directory(kept));
}

ZEST_CASE(temp_directory_moves_its_directory) {
    test::TempDir dir;
    auto first = TempDirectory::create(dir.root(), "first-");
    auto second = TempDirectory::create(dir.root(), "second-");
    ZASSERT(first.has_value());
    ZASSERT(second.has_value());
    std::string first_path(first->path());
    std::string second_path(second->path());
    TempDirectory moved(std::move(*first));
    ZEXPECT(first->path().empty());
    ZEXPECT(moved.path() == first_path);
    // Taking another's removes the one it held.
    moved = std::move(*second);
    ZEXPECT(try_exists(first_path) == false);
    ZEXPECT(moved.path() == second_path);
    ZEXPECT(is_directory(second_path));
}

ZEST_CASE(temp_directory_in_nothing_fails) {
    test::TempDir dir;
    auto temp = TempDirectory::create(dir.file("missing"), "case-");
    ZASSERT(!temp.has_value());
    ZEXPECT(temp.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(temp_file_goes_when_this_goes) {
    test::TempDir dir;
    std::string held;
    {
        auto temp = TempFile::create(dir.root(), "case-", ".cpp");
        ZASSERT(temp.has_value());
        held = temp->path();
        ZEXPECT(path::filename(held).starts_with("case-"));
        ZEXPECT(path::extension(held) == ".cpp");
        ZEXPECT(held == canonical_of(held));
        ZEXPECT(!temp->file().write("text"));
        ZEXPECT(test::read_text(held) == "text");
    }
    ZEXPECT(try_exists(held) == false);
}

#ifndef _WIN32
ZEST_CASE(temp_file_is_its_owners_alone) {
    test::TempDir dir;
    auto temp = TempFile::create(dir.root(), "case-");
    ZASSERT(temp.has_value());
    auto found = status(temp->path());
    ZASSERT(found.has_value());
    ZEXPECT(found->permissions == 0600);
}
#endif

ZEST_CASE(temp_file_removes_now_and_once) {
    test::TempDir dir;
    auto temp = TempFile::create(dir.root(), "case-");
    ZASSERT(temp.has_value());
    std::string held(temp->path());
    ZEXPECT(!temp->remove());
    ZEXPECT(try_exists(held) == false);
    ZEXPECT(!temp->file().is_open());
    ZEXPECT(!temp->remove());
}

ZEST_CASE(temp_file_kept_stays) {
    test::TempDir dir;
    std::string kept;
    {
        auto temp = TempFile::create(dir.root(), "case-");
        ZASSERT(temp.has_value());
        kept = std::move(*temp).keep();
        // Open until the TempFile goes.
        ZEXPECT(!temp->file().write("text"));
    }
    ZEXPECT(test::read_text(kept) == "text");
}

ZEST_CASE(temp_file_moves_its_file) {
    test::TempDir dir;
    auto first = TempFile::create(dir.root(), "first-");
    auto second = TempFile::create(dir.root(), "second-");
    ZASSERT(first.has_value());
    ZASSERT(second.has_value());
    std::string first_path(first->path());
    TempFile moved(std::move(*first));
    ZEXPECT(first->path().empty());
    ZEXPECT(moved.path() == first_path);
    ZEXPECT(moved.file().is_open());
    moved = std::move(*second);
    ZEXPECT(try_exists(first_path) == false);
    ZEXPECT(moved.file().is_open());
}

};  // ZEST_SUITE(fs_temp)

}  // namespace

}  // namespace kota::fs
