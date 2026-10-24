#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/fs/cygwin.h"

namespace kota::fs::cygwin {

namespace {

/// What Cygwin's `mount` lists on a default install, plus a user's fstab
/// mount and a drive.
constexpr std::string_view cygwin_listing =
    "C:/cygwin64/bin on /usr/bin type ntfs (binary,auto)\n"
    "C:/cygwin64/lib on /usr/lib type ntfs (binary,auto)\n"
    "D:/src on /home/me/src type ntfs (binary,user)\n"
    "C:/cygwin64 on / type ntfs (binary,auto)\n"
    "C: on /cygdrive/c type ntfs (binary,posix=0,user,noumount,auto)\n";

/// MSYS2's, which mounts drives at the root, with Windows line ends.
constexpr std::string_view msys_listing =
    "C:/msys64 on / type ntfs (binary,noacl,auto)\r\n"
    "C:/msys64/usr/bin on /bin type ntfs (binary,noacl,auto)\r\n"
    "C: on /c type ntfs (binary,noacl,posix=0,user,noumount,auto)\r\n";

ZEST_SUITE(fs_cygwin) {

ZEST_CASE(posix_paths_are_rooted_but_not_absolute_on_windows) {
    ZEXPECT(is_posix_path("/usr/include", path::Style::Windows));
    ZEXPECT(is_posix_path("/cygdrive/c/x", path::Style::Windows));
    ZEXPECT(!is_posix_path("C:/x", path::Style::Windows));
    ZEXPECT(!is_posix_path("//server/share/x", path::Style::Windows));
    ZEXPECT(!is_posix_path("usr/include", path::Style::Windows));
    ZEXPECT(!is_posix_path("/usr/include", path::Style::Posix));
}

ZEST_CASE(parse_puts_the_longest_mount_point_first) {
    auto table = MountTable::parse(cygwin_listing);
    ZASSERT(table.has_value());
    ZASSERT(table->mounts.size() == 5);
    ZEXPECT(table->mounts.front() == std::pair<std::string, std::string>("/home/me/src", "D:/src"));
    ZEXPECT(table->mounts.back() == std::pair<std::string, std::string>("/", "C:/cygwin64"));
}

ZEST_CASE(parse_takes_directories_with_backslashes) {
    auto table = MountTable::parse("C:\\cygwin on / type ntfs (binary,auto)\n");
    ZASSERT(table.has_value());
    ZEXPECT(table->to_windows("/usr/bin") == "C:/cygwin/usr/bin");
}

ZEST_CASE(parse_without_a_root_mount_fails) {
    ZEXPECT(MountTable::parse("C:/cygwin64/bin on /usr/bin type ntfs (binary)\n") == std::nullopt);
    ZEXPECT(MountTable::parse("") == std::nullopt);
}

ZEST_CASE(to_windows_maps_through_the_longest_mount) {
    auto table = MountTable::parse(cygwin_listing);
    ZASSERT(table.has_value());
    ZEXPECT(table->to_windows("/usr/include/stdio.h") == "C:/cygwin64/usr/include/stdio.h");
    // Cygwin mounts /usr/lib at the root's lib, not under usr.
    ZEXPECT(table->to_windows("/usr/lib/gcc") == "C:/cygwin64/lib/gcc");
    ZEXPECT(table->to_windows("/usr/bin") == "C:/cygwin64/bin");
    ZEXPECT(table->to_windows("/home/me/src/a.c") == "D:/src/a.c");
    // A mount point matches whole names only.
    ZEXPECT(table->to_windows("/usr/binary") == "C:/cygwin64/usr/binary");
    ZEXPECT(table->to_windows("/cygdrive/c/proj") == "C:/proj");
    ZEXPECT(table->to_windows("/cygdrive/C/proj") == "C:/proj");
    ZEXPECT(table->to_windows("/cygdrive/c") == "C:/");
}

ZEST_CASE(to_windows_folds_dots_before_it_maps) {
    auto table = MountTable::parse(cygwin_listing);
    ZASSERT(table.has_value());
    // `..` out of a mount point leaves the mount, as the runtime folds it.
    ZEXPECT(table->to_windows("/usr/lib/../include") == "C:/cygwin64/usr/include");
    ZEXPECT(table->to_windows("/usr/./bin//gcc") == "C:/cygwin64/bin/gcc");
}

ZEST_CASE(to_windows_keeps_a_share) {
    auto table = MountTable::parse(cygwin_listing);
    ZASSERT(table.has_value());
    ZEXPECT(table->to_windows("//server/share/a/../b.c") == "//server/share/b.c");
    // A share's name stays even where `..` would climb past it.
    ZEXPECT(table->to_windows("//server/share/..") == "//server/share/");
    // Three slashes or more are the root's.
    ZEXPECT(table->to_windows("///usr/bin") == "C:/cygwin64/bin");
}

ZEST_CASE(to_windows_reads_msys_drives_at_the_root) {
    auto table = MountTable::parse(msys_listing);
    ZASSERT(table.has_value());
    ZEXPECT(table->to_windows("/c/Users/me") == "C:/Users/me");
    ZEXPECT(table->to_windows("/bin/gcc") == "C:/msys64/usr/bin/gcc");
    ZEXPECT(table->to_windows("/mingw64/include") == "C:/msys64/mingw64/include");
}

};  // ZEST_SUITE(fs_cygwin)

}  // namespace

}  // namespace kota::fs::cygwin
