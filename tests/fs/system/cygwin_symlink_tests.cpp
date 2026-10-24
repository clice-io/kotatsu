#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fs/harness/files.h"
#include "kota/zest/zest.h"
#include "kota/fs/cygwin.h"
#include "kota/fs/path.h"

namespace kota::fs::cygwin {

namespace {

using namespace std::literals;

ZEST_SUITE(fs_cygwin_symlink) {

ZEST_CASE(read_symlink_reads_the_installer_spelling) {
    test::TempDir dir;
    test::write_text(dir.file("cc"), "!<symlink>gcc.exe\0"sv);
    ZEXPECT(read_symlink(dir.file("cc")) == std::optional<std::string>("gcc.exe"));
}

ZEST_CASE(read_symlink_reads_the_runtime_spelling) {
    test::TempDir dir;
    // What `ln -s` writes: UTF-16 behind a byte order mark, `é` included.
    test::write_text(dir.file("link"), "!<symlink>\xFF\xFE/\0u\0s\0r\0/\0\xE9\0\0\0"sv);
    ZEXPECT(read_symlink(dir.file("link")) == std::optional<std::string>("/usr/\xC3\xA9"));
}

ZEST_CASE(read_symlink_of_anything_else_is_none) {
    test::TempDir dir;
    test::write_text(dir.file("plain"), "int main() {}");
    ZEXPECT(read_symlink(dir.file("plain")) == std::nullopt);
    ZEXPECT(read_symlink(dir.file("missing")) == std::nullopt);
    ZEXPECT(read_symlink(dir.root()) == std::nullopt);
}

ZEST_CASE(runtime_dirs_keep_the_dll_search_order) {
    test::TempDir dir;
    for(auto name: {"msys/usr/bin", "cygwin/bin", "plain", "tool"}) {
        std::filesystem::create_directories(test::std_path(dir.file(name)));
    }
    test::write_text(dir.file("msys/usr/bin/msys-2.0.dll"), "");
    test::write_text(dir.file("cygwin/bin/cygwin1.dll"), "");
    test::write_text(dir.file("tool/cygwin1.dll"), "");
    std::vector<std::string> search{dir.file("plain"),
                                    dir.file("msys/usr/bin"),
                                    dir.file("cygwin/bin")};
    auto dirs = runtime_dirs(dir.file("tool/gcc.exe"), search);
    ZEXPECT(dirs == std::vector<std::string>{dir.file("tool"),
                                             dir.file("msys/usr/bin"),
                                             dir.file("cygwin/bin")});
}

};  // ZEST_SUITE(fs_cygwin_symlink)

}  // namespace

}  // namespace kota::fs::cygwin
