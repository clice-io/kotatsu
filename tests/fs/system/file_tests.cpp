#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
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
    ZEXPECT(read_file(dir.file("é中😀.txt")) == std::string("text\0with a NUL"sv));
}

ZEST_CASE(read_file_reads_past_its_buffer) {
    test::TempDir dir;
    std::string large(100'000, '\0');
    for(std::size_t i = 0; i < large.size(); ++i) {
        large[i] = static_cast<char>('a' + i % 26);
    }
    test::write_text(dir.file("large"), large);
    ZEXPECT(read_file(dir.file("large")) == large);
    test::write_text(dir.file("empty"), "");
    ZEXPECT(read_file(dir.file("empty")) == std::string());
}

ZEST_CASE(read_file_of_nothing_fails) {
    test::TempDir dir;
    auto content = read_file(dir.file("missing"));
    ZASSERT(!content.has_value());
    ZEXPECT(content.error() == std::errc::no_such_file_or_directory);
}

ZEST_CASE(read_file_of_a_directory_fails) {
    test::TempDir dir;
    auto content = read_file(dir.root());
    ZASSERT(!content.has_value());
#ifdef _WIN32
    // Windows opens a directory only for its metadata.
    ZEXPECT(content.error() == std::errc::permission_denied);
#else
    ZEXPECT(content.error() == std::errc::is_a_directory);
#endif
}

ZEST_CASE(open_reads_and_writes_through_the_handle) {
    test::TempDir dir;
    auto file = File::open(dir.file("file"), OpenMode::Write);
    ZASSERT(file.has_value());
    ZEXPECT(file->is_open());
    ZEXPECT(!file->write("hello"));
    ZEXPECT(!file->close());
    ZEXPECT(!file->is_open());
    ZEXPECT(test::read_text(dir.file("file")) == "hello");

    auto reader = File::open(dir.file("file"), OpenMode::Read);
    ZASSERT(reader.has_value());
    std::array<char, 16> buffer{};
    auto count = reader->read(buffer);
    ZASSERT(count.has_value());
    ZEXPECT(std::string(buffer.data(), *count) == "hello");
    ZEXPECT(reader->read(buffer) == 0u);
}

ZEST_CASE(a_moved_file_keeps_its_handle) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto opened = File::open(dir.file("file"), OpenMode::Read);
    ZASSERT(opened.has_value());
    auto handle = opened->native_handle();
    File moved(std::move(*opened));
    ZEXPECT(!opened->is_open());
    ZEXPECT(opened->native_handle() == File::closed);
    ZEXPECT(moved.native_handle() == handle);
    File assigned;
    assigned = std::move(moved);
    ZEXPECT(!moved.is_open());
    ZEXPECT(assigned.native_handle() == handle);
    // Closing what is closed does nothing.
    ZEXPECT(!moved.close());
}

ZEST_CASE(read_after_close_fails) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "text");
    auto file = File::open(dir.file("file"), OpenMode::Read);
    ZASSERT(file.has_value());
    ZEXPECT(!file->close());
    std::array<char, 16> buffer{};
    auto count = file->read(buffer);
    ZASSERT(!count.has_value());
    ZEXPECT(count.error() == std::errc::bad_file_descriptor);
    ZEXPECT(file->write("more") == std::errc::bad_file_descriptor);
}

ZEST_CASE(open_to_create_new_over_anything_fails) {
    test::TempDir dir;
    ZEXPECT(File::open(dir.file("new"), OpenMode::CreateNew).has_value());
    auto again = File::open(dir.file("new"), OpenMode::CreateNew);
    ZASSERT(!again.has_value());
    ZEXPECT(again.error() == std::errc::file_exists);
}

ZEST_CASE(open_to_create_new_over_a_dangling_link_fails) {
    test::TempDir dir;
    if(!test::linked(create_symlink("missing", dir.file("link")))) {
        return;
    }
    auto file = File::open(dir.file("link"), OpenMode::CreateNew);
    ZASSERT(!file.has_value());
    ZEXPECT(file.error() == std::errc::file_exists);
    ZEXPECT(!is_file(dir.file("missing")));
}

ZEST_CASE(write_file_atomic_replaces_the_whole_file) {
    test::TempDir dir;
    test::write_text(dir.file("file"), "old content");
    ZEXPECT(!write_file_atomic(dir.file("file"), "new"));
    ZEXPECT(test::read_text(dir.file("file")) == "new");
    // The sibling it wrote through is gone.
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(entries->size() == 1u);
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

ZEST_CASE(write_file_atomic_takes_a_name_near_the_limit) {
    test::TempDir dir;
    // 240 bytes, of 255 a component may hold: the sibling's name is cut
    // short, here inside the eleventh character.
    std::string name;
    for(int i = 0; i < 11; ++i) {
        name += "中";
    }
    name += std::string(207, 'a');
    ZEXPECT(!write_file_atomic(path::join(dir.root(), name), "text"));
    ZEXPECT(read_file(path::join(dir.root(), name)) == std::string("text"));
}

ZEST_CASE(write_file_atomic_never_shows_a_part) {
    test::TempDir dir;
    auto file = dir.file("file");
    // Each writer's text is one letter, repeated: a mix of two shows.
    auto text_of = [](int writer) {
        return std::string(64 * 1024, static_cast<char>('a' + writer));
    };
    ZEXPECT(!write_file_atomic(file, text_of(0)));
    constexpr int writers = 4;
    std::atomic<int> failures = 0;
    std::atomic<int> parts = 0;
    std::atomic<bool> writing = true;
    std::thread reader([&] {
        while(writing) {
            auto content = read_file(file);
            if(!content) {
                ++failures;
            } else if(*content != text_of(content->empty() ? 0 : content->front() - 'a')) {
                ++parts;
            }
        }
    });
    std::vector<std::thread> threads;
    for(int writer = 0; writer < writers; ++writer) {
        threads.emplace_back([&, writer] {
            for(int i = 0; i < 20; ++i) {
                if(write_file_atomic(file, text_of(writer))) {
                    ++failures;
                }
            }
        });
    }
    for(auto& thread: threads) {
        thread.join();
    }
    writing = false;
    reader.join();
    ZEXPECT(failures == 0);
    ZEXPECT(parts == 0);
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(entries->size() == 1u);
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
    auto entries = read_dir(dir.root());
    ZASSERT(entries.has_value());
    ZEXPECT(entries->size() == 1u);
}

ZEST_CASE(create_temp_file_names_a_new_empty_file) {
    test::TempDir dir;
    std::set<std::string> made;
    for(int i = 0; i < 8; ++i) {
        auto created = create_temp_file(dir.root(), "pre-", ".tmp");
        ZASSERT(created.has_value());
        ZEXPECT(zest::starts_with(path::filename(*created), "pre-"));
        ZEXPECT(path::extension(*created) == ".tmp");
        ZEXPECT(test::read_text(*created) == "");
        ZEXPECT(is_file(*created));
        made.insert(*created);
    }
    ZEXPECT(made.size() == 8u);
}

ZEST_CASE(create_temp_file_in_nothing_fails) {
    test::TempDir dir;
    auto created = create_temp_file(dir.file("missing"), "pre-", "");
    ZASSERT(!created.has_value());
    ZEXPECT(created.error() == std::errc::no_such_file_or_directory);
}

};  // ZEST_SUITE(fs_file)

}  // namespace

}  // namespace kota::fs
