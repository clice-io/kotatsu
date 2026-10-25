#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "kota/zest/zest.h"
#include "kota/support/small_string.h"
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

/// What to_windows() writes for `posix`.
std::string windows_of(const MountTable& table, std::string_view posix) {
    std::string out;
    table.to_windows(out, posix);
    return out;
}

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
    ZEXPECT(windows_of(*table, "/usr/bin") == "C:/cygwin/usr/bin");
}

ZEST_CASE(parse_takes_names_with_spaces) {
    // A toolchain's Cygwin under Program Files (Andes), and a user's mount
    // point with a space.
    auto table = MountTable::parse(
        "C:/Program Files/Andes Tech/cygwin/bin on /usr/bin type ntfs (binary,auto)\n"
        "C:/Program Files/Andes Tech/cygwin on / type ntfs (binary,auto)\n"
        "D:/My Sources on /home/me/my sources type ntfs (binary,user)\n"
        "C: on /cygdrive/c type ntfs (binary,posix=0,user,noumount,auto)\n");
    ZASSERT(table.has_value());
    ZASSERT(table->mounts.size() == 4);
    ZEXPECT(windows_of(*table, "/usr/include/stdio.h") ==
            "C:/Program Files/Andes Tech/cygwin/usr/include/stdio.h");
    ZEXPECT(windows_of(*table, "/usr/bin/gcc") == "C:/Program Files/Andes Tech/cygwin/bin/gcc");
    ZEXPECT(windows_of(*table, "/home/me/my sources/a.c") == "D:/My Sources/a.c");
    ZEXPECT(windows_of(*table, "/cygdrive/c/Program Files/x") == "C:/Program Files/x");
}

ZEST_CASE(parse_without_a_root_mount_fails) {
    ZEXPECT(MountTable::parse("C:/cygwin64/bin on /usr/bin type ntfs (binary)\n") == std::nullopt);
    ZEXPECT(MountTable::parse("") == std::nullopt);
}

ZEST_CASE(to_windows_maps_through_the_longest_mount) {
    auto table = MountTable::parse(cygwin_listing);
    ZASSERT(table.has_value());
    ZEXPECT(windows_of(*table, "/usr/include/stdio.h") == "C:/cygwin64/usr/include/stdio.h");
    // Cygwin mounts /usr/lib at the root's lib, not under usr.
    ZEXPECT(windows_of(*table, "/usr/lib/gcc") == "C:/cygwin64/lib/gcc");
    ZEXPECT(windows_of(*table, "/usr/bin") == "C:/cygwin64/bin");
    ZEXPECT(windows_of(*table, "/home/me/src/a.c") == "D:/src/a.c");
    // A mount point matches whole names only.
    ZEXPECT(windows_of(*table, "/usr/binary") == "C:/cygwin64/usr/binary");
    ZEXPECT(windows_of(*table, "/cygdrive/c/proj") == "C:/proj");
    ZEXPECT(windows_of(*table, "/cygdrive/C/proj") == "C:/proj");
    ZEXPECT(windows_of(*table, "/cygdrive/c") == "C:/");
}

ZEST_CASE(to_windows_folds_dots_before_it_maps) {
    auto table = MountTable::parse(cygwin_listing);
    ZASSERT(table.has_value());
    // `..` out of a mount point leaves the mount, as the runtime folds it.
    ZEXPECT(windows_of(*table, "/usr/lib/../include") == "C:/cygwin64/usr/include");
    ZEXPECT(windows_of(*table, "/usr/./bin//gcc") == "C:/cygwin64/bin/gcc");
}

ZEST_CASE(to_windows_keeps_a_share) {
    auto table = MountTable::parse(cygwin_listing);
    ZASSERT(table.has_value());
    ZEXPECT(windows_of(*table, "//server/share/a/../b.c") == "//server/share/b.c");
    // A share's name stays even where `..` would climb past it.
    ZEXPECT(windows_of(*table, "//server/share/..") == "//server/share/");
    // Three slashes or more are the root's.
    ZEXPECT(windows_of(*table, "///usr/bin") == "C:/cygwin64/bin");
}

ZEST_CASE(to_windows_writes_over_what_the_buffer_held) {
    auto table = MountTable::parse(cygwin_listing);
    ZASSERT(table.has_value());
    small_string<16> out("text the path replaces");
    table->to_windows(out, "/usr/bin");
    ZEXPECT(std::string_view(out) == "C:/cygwin64/bin");
    table->to_windows(out, "//server/share/a/../b.c");
    ZEXPECT(std::string_view(out) == "//server/share/b.c");
}

ZEST_CASE(to_windows_reads_msys_drives_at_the_root) {
    auto table = MountTable::parse(msys_listing);
    ZASSERT(table.has_value());
    ZEXPECT(windows_of(*table, "/c/Users/me") == "C:/Users/me");
    ZEXPECT(windows_of(*table, "/bin/gcc") == "C:/msys64/usr/bin/gcc");
    ZEXPECT(windows_of(*table, "/mingw64/include") == "C:/msys64/mingw64/include");
}

};  // ZEST_SUITE(fs_cygwin)

}  // namespace

}  // namespace kota::fs::cygwin
