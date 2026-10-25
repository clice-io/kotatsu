#include <filesystem>
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

ZEST_SUITE(fs_cygwin_files) {

ZEST_CASE(read_symlink_reads_the_installer_spelling) {
    test::TempDir dir;
    test::write_text(dir.file("cc"), "!<symlink>gcc.exe\0"sv);
    std::string target;
    ZEXPECT(read_symlink(target, dir.file("cc")));
    ZEXPECT(target == "gcc.exe");
}

ZEST_CASE(read_symlink_reads_the_runtime_spelling) {
    test::TempDir dir;
    // What `ln -s` writes: UTF-16 behind a byte order mark, `é` included.
    test::write_text(dir.file("link"), "!<symlink>\xFF\xFE/\0u\0s\0r\0/\0\xE9\0\0\0"sv);
    std::string target;
    ZEXPECT(read_symlink(target, dir.file("link")));
    ZEXPECT(target == "/usr/\xC3\xA9");
}

ZEST_CASE(read_symlink_of_anything_else_fails) {
    test::TempDir dir;
    test::write_text(dir.file("plain"), "int main() {}");
    std::string target = "kept";
    ZEXPECT(!read_symlink(target, dir.file("plain")));
    ZEXPECT(!read_symlink(target, dir.file("missing")));
    ZEXPECT(!read_symlink(target, dir.root()));
    ZEXPECT(target == "kept");
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

};  // ZEST_SUITE(fs_cygwin_files)

}  // namespace

}  // namespace kota::fs::cygwin
