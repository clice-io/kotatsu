#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "fs/harness/dir.h"
#include "fs/harness/files.h"
#include "fs/harness/win32.h"
#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/path.h"
#include "kota/fs/status.h"

namespace kota::fs {

namespace {

using namespace std::literals;

ZEST_SUITE(fs_file) {

ZEST_CASE(write_file_writes_the_whole_file) {
    test::TempDir dir;
    ZEXPECT(!write_file(dir.file("é中😀.txt"), "text\0with a NUL"sv));
    ZEXPECT(test::read_text(dir.file("é中😀.txt")) == "text\0with a NUL"sv);
    // Writing again empties the file first.
    ZEXPECT(!write_file(dir.file("é中😀.txt"), "x"));
    ZEXPECT(test::read_text(dir.file("é中😀.txt")) == "x");
}

ZEST_CASE(read_file_reads_the_whole_file) {
    test::TempDir dir;
    test::write_text(dir.file("é中😀.txt"), "text\0with a NUL"sv);
    std::string content;
    ZEXPECT(!read_file(content, dir.file("é中😀.txt")));
    ZEXPECT(content == "text\0with a NUL"sv);
}

ZEST_CASE(read_file_writes_over_what_the_buffer_held) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "new");
    small_string<8> content("some old text");
    ZEXPECT(!read_file(content, dir.file("file")));
    ZEXPECT(std::string_view(content) == "new");
}

ZEST_CASE(read_file_reads_past_its_buffer) {
    test::TempDir dir;
    auto large = test::text_of(100'000);
    test::write_text(dir.file("large"), large);
    std::string content;
    ZEXPECT(!read_file(content, dir.file("large")));
    ZEXPECT(content == large);
    test::write_text(dir.file("empty"), "");
    ZEXPECT(!read_file(content, dir.file("empty")));
    ZEXPECT(content == "");
}

ZEST_CASE(read_file_sizes_its_buffer_once) {
    test::TempDir dir;
    auto text = test::text_of(1'000'000);
    test::write_text(dir.file("large"), text);
    test::CountedBuffer content;
    ZEXPECT(!read_file(content, dir.file("large")));
    ZEXPECT(content.text == text);
    ZEXPECT(content.resizes == 2);
}

#ifdef __linux__
ZEST_CASE(read_file_reads_a_file_that_reports_no_size) {
    std::string content;
    ZEXPECT(!read_file(content, "/proc/self/status"));
    ZEXPECT(zest::contains(content, "Pid:"));
}
#endif

ZEST_CASE(read_file_of_nothing_fails) {
    test::TempDir dir;
    std::string content;
    ZEXPECT(read_file(content, dir.file("missing")) == std::errc::no_such_file_or_directory);
}

ZEST_CASE(read_file_of_a_directory_fails) {
    test::TempDir dir;
    std::string content;
    ZEXPECT(read_file(content, dir.root()) == std::errc::is_a_directory);
}

ZEST_CASE(write_file_atomic_replaces_the_whole_file) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "old content");
    ZEXPECT(!write_file_atomic(dir.file("file"), "new"));
    ZEXPECT(test::read_text(dir.file("file")) == "new");
    // The sibling it wrote through is gone.
    ZEXPECT(test::entries_of(dir.root()).size() == 1u);
}

ZEST_CASE(write_file_atomic_replaces_a_link) {
    test::TempDir dir;
    test::write_text(dir.file("target"), "old");
    if(!test::linked(create_symlink("target", dir.file("link")))) {
        return;
    }
    ZEXPECT(!write_file_atomic(dir.file("link"), "new"));
    ZEXPECT(test::read_text(dir.file("link")) == "new");
    ZEXPECT(test::read_text(dir.file("target")) == "old");
    ZEXPECT(!is_symlink(dir.file("link")));
}

#ifdef _WIN32
ZEST_CASE(write_file_atomic_keeps_the_attributes) {
    // Read-only, hidden and system, which a file there refuses no rename for.
    test::TempDir dir;
    test::write_text(dir.file("file"), "old");
    constexpr DWORD kept = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
    ZASSERT(::SetFileAttributesW(test::std_path(dir.file("file")).wstring().c_str(), kept));
    auto error = write_file_atomic(dir.file("file"), "new");
    auto attributes = ::GetFileAttributesW(test::std_path(dir.file("file")).wstring().c_str());
    auto content = test::read_text(dir.file("file"));
    // The read-only file goes before a check could end the case: TempDir,
    // through std::filesystem, would leave it.
    ZEXPECT(remove(dir.file("file")) == true);
    ZEXPECT(error == std::error_code());
    ZEXPECT(attributes != INVALID_FILE_ATTRIBUTES);
    ZEXPECT((attributes & kept) == kept);
    ZEXPECT(content == "new");
}
#else
ZEST_CASE(write_file_atomic_keeps_the_permissions) {
    // A read-only file's too, which is no file the sibling cannot replace.
    test::TempDir dir;
    for(unsigned permissions: {0600u, 0755u, 0444u}) {
        ZEST_CONTEXT("{:o}", permissions);
        test::write_text(dir.file("file"), "old");
        ZASSERT(!set_permissions(dir.file("file"), permissions));
        ZEXPECT(!write_file_atomic(dir.file("file"), "new"));
        ZEXPECT(test::read_text(dir.file("file")) == "new");
        auto found = status(dir.file("file"));
        ZASSERT(found.has_value());
        ZEXPECT(found->permissions == permissions);
        ZASSERT(remove(dir.file("file")) == true);
    }
}

ZEST_CASE(write_file_atomic_makes_a_new_file_as_write_file_does) {
    // Its permissions what the umask leaves.
    test::TempDir dir;
    ZEXPECT(!write_file(dir.file("written"), "text"));
    ZEXPECT(!write_file_atomic(dir.file("replaced"), "text"));
    auto written = status(dir.file("written"));
    auto replaced = status(dir.file("replaced"));
    ZASSERT(written.has_value());
    ZASSERT(replaced.has_value());
    ZEXPECT(replaced->permissions == written->permissions);
}

ZEST_CASE(write_file_atomic_keeps_the_owner) {
    if(::geteuid() != 0) {
        // Only root gives a file to another user.
        zest::skip();
        return;
    }
    test::TempDir dir;
    test::write_text(dir.file("file"), "old");
    ZASSERT(::chown(dir.file("file").c_str(), 65534, 65534) == 0);
    ZEXPECT(!write_file_atomic(dir.file("file"), "new"));
    struct stat info;
    ZASSERT(::stat(dir.file("file").c_str(), &info) == 0);
    ZEXPECT(info.st_uid == 65534u);
    ZEXPECT(info.st_gid == 65534u);
}
#endif

ZEST_CASE(write_file_atomic_takes_a_name_near_the_limit) {
    test::TempDir dir;
    // 240 bytes, of 255 a component may hold: the sibling's name is cut
    // short, here inside the eleventh character.
    std::string name;
    for(int i = 0; i < 11; ++i) {
        name += "中";
    }
    name += std::string(207, 'a');
    auto file = dir.root();
    path::push(file, name);
    ZEXPECT(!write_file_atomic(file, "text"));
    std::string content;
    ZEXPECT(!read_file(content, file));
    ZEXPECT(content == "text");
}

ZEST_CASE(write_file_atomic_never_shows_a_part) {
    test::TempDir dir;
    auto file = dir.file("file");
    // Each writer's text is one letter, repeated: a mix of two shows.
    auto text_of = [](int writer) {
        return std::string(64 * 1024, static_cast<char>('a' + writer));
    };
    ZEXPECT(!write_file_atomic(file, text_of(0)));
    // The first error each side met, which a failure reports.
    std::mutex mutex;
    std::error_code read_error;
    std::error_code write_error;
    auto keep = [&](std::error_code& first, std::error_code error) {
        std::lock_guard lock(mutex);
        if(!first) {
            first = error;
        }
    };
    std::atomic<int> parts = 0;
    std::atomic<bool> writing = true;
    std::thread reader([&] {
        std::string content;
        while(writing) {
            if(auto error = read_file(content, file)) {
#ifdef _WIN32
                // An open that races a rename may fail on Windows.
                if(error == std::errc::permission_denied ||
                   error == std::errc::no_such_file_or_directory) {
                    continue;
                }
#endif
                keep(read_error, error);
            } else if(content != text_of(content.empty() ? 0 : content.front() - 'a')) {
                ++parts;
            }
        }
    });
    constexpr int writers = 4;
    std::vector<std::thread> threads;
    for(int writer = 0; writer < writers; ++writer) {
        threads.emplace_back([&, writer] {
            for(int i = 0; i < 20; ++i) {
                if(auto error = write_file_atomic(file, text_of(writer))) {
                    keep(write_error, error);
                }
            }
        });
    }
    for(auto& thread: threads) {
        thread.join();
    }
    writing = false;
    reader.join();
    ZEXPECT(read_error == std::error_code());
    ZEXPECT(write_error == std::error_code());
    ZEXPECT(parts.load() == 0);
    ZEXPECT(test::entries_of(dir.root()).size() == 1u);
}

ZEST_CASE(write_file_atomic_over_a_directory_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "taken");
    test::write_text(dir.file("taken/file"), "text");
    auto error = write_file_atomic(dir.file("taken"), "new");
#ifdef _WIN32
    ZEXPECT(error == std::errc::permission_denied);
#else
    ZEXPECT(error == std::errc::is_a_directory);
#endif
    // The sibling it wrote through is gone.
    ZEXPECT(test::entries_of(dir.root()).size() == 1u);
}

ZEST_CASE(create_temp_file_opens_a_new_empty_file) {
    test::TempDir dir;
    std::set<std::string> made;
    std::string temp;
    for(int i = 0; i < 8; ++i) {
        auto created = create_temp_file(temp, dir.root(), "pre-", ".tmp");
        ZASSERT(created.has_value());
        ZEXPECT(path::parent(temp) == dir.root());
        ZEXPECT(zest::starts_with(path::filename(temp), "pre-"));
        ZEXPECT(path::extension(temp) == ".tmp");
        ZEXPECT(is_file(temp));
        ZEXPECT(test::read_text(temp) == "");
        made.insert(temp);
    }
    ZEXPECT(made.size() == 8u);
}

ZEST_CASE(create_temp_file_reads_and_writes) {
    test::TempDir dir;
    std::string temp;
    auto created = create_temp_file(temp, dir.root(), "", "");
    ZASSERT(created.has_value());
    ZEXPECT(!created->write("text"));
    ZEXPECT(created->seek(0) == 0u);
    std::array<char, 16> buffer{};
    ZEXPECT(created->read(buffer) == 4u);
    ZEXPECT(std::string_view(buffer.data(), 4) == "text");
}

ZEST_CASE(create_temp_file_in_nothing_fails) {
    test::TempDir dir;
    std::string temp;
    auto created = create_temp_file(temp, dir.file("missing"), "pre-", "");
    ZASSERT(!created.has_value());
    ZEXPECT(created.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(create_temp_file_named_elsewhere_fails) {
    test::TempDir dir;
    std::filesystem::create_directory(dir.path / "inside");
    std::string temp;
    std::vector<std::string_view> elsewhere = {
        "../escaped-",
        "a/b-",
#ifdef _WIN32
        R"(..\escaped-)",
        "C:",
        "file:stream-",
#endif
    };
    for(auto name: elsewhere) {
        ZEST_CONTEXT("{}", name);
        auto prefixed = create_temp_file(temp, dir.file("inside"), name, "");
        ZASSERT(!prefixed.has_value());
        ZEXPECT(prefixed.error() == std::errc::invalid_argument);
        auto suffixed = create_temp_file(temp, dir.file("inside"), "pre-", name);
        ZASSERT(!suffixed.has_value());
        ZEXPECT(suffixed.error() == std::errc::invalid_argument);
        ZEXPECT(create_temp_directory(temp, dir.file("inside"), name) ==
                std::errc::invalid_argument);
    }
    // Nothing was made, in the directory or beside it.
    ZEXPECT(test::entries_of(dir.file("inside")).empty());
    ZEXPECT(test::entries_of(dir.root()).size() == 1u);
}

ZEST_CASE(create_temp_directory_makes_a_new_empty_directory) {
    test::TempDir dir;
    std::set<std::string> made;
    std::string temp;
    for(int i = 0; i < 8; ++i) {
        ZEXPECT(!create_temp_directory(temp, dir.root(), "pre-"));
        ZEXPECT(path::parent(temp) == dir.root());
        ZEXPECT(zest::starts_with(path::filename(temp), "pre-"));
        ZEXPECT(is_directory(temp));
        ZEXPECT(test::entries_of(temp).empty());
        made.insert(temp);
    }
    ZEXPECT(made.size() == 8u);
}

#ifndef _WIN32
ZEST_CASE(create_temp_file_lets_only_its_owner_in) {
    test::TempDir dir;
    std::string temp;
    ZASSERT(create_temp_file(temp, dir.root(), "", "").has_value());
    auto found = status(temp);
    ZASSERT(found.has_value());
    ZEXPECT(found->permissions == 0600u);
}

ZEST_CASE(create_temp_directory_lets_only_its_owner_in) {
    test::TempDir dir;
    std::string temp;
    ZASSERT(!create_temp_directory(temp, dir.root(), ""));
    auto found = status(temp);
    ZASSERT(found.has_value());
    ZEXPECT(found->permissions == 0700u);
}
#endif

ZEST_CASE(create_temp_directory_in_nothing_fails) {
    test::TempDir dir;
    std::string temp;
    ZEXPECT(create_temp_directory(temp, dir.file("missing"), "pre-") ==
            std::errc::no_such_file_or_directory);
}

};  // ZEST_SUITE(fs_file)

}  // namespace

}  // namespace kota::fs
