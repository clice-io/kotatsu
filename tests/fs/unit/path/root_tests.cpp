#include <string_view>

#include "kota/zest/zest.h"
#include "kota/fs/path.h"

namespace kota::fs::path {

namespace {

constexpr auto posix = Style::Posix;
constexpr auto windows = Style::Windows;

ZEST_SUITE(fs_path_root) {

ZEST_CASE(separators_depend_on_the_style) {
    ZEXPECT(is_separator('/', posix));
    ZEXPECT(!is_separator('\\', posix));
    ZEXPECT(is_separator('/', windows));
    ZEXPECT(is_separator('\\', windows));
}

ZEST_CASE(posix_roots_are_directories_alone) {
    auto r = root("/usr/lib", posix);
    ZEXPECT(r.prefix == Prefix::None);
    ZEXPECT(r.name == "");
    ZEXPECT(r.directory == "/");
    // `//` is no network root: Linux reads it as `/`.
    ZEXPECT(root("//net/x", posix).directory == "//");
    ZEXPECT(root("//net/x", posix).name == "");
    // A drive and a backslash are file name characters.
    ZEXPECT(root("C:\\x", posix).name == "");
    ZEXPECT(root("C:\\x", posix).directory == "");
}

ZEST_CASE(windows_drives_are_prefixes) {
    auto r = root("C:\\x", windows);
    ZEXPECT(r.prefix == Prefix::Drive);
    ZEXPECT(r.name == "C:");
    ZEXPECT(r.directory == "\\");
    ZEXPECT(root("c:/x", windows).directory == "/");
    ZEXPECT(root("C:x", windows).prefix == Prefix::Drive);
    ZEXPECT(root("C:x", windows).directory == "");
    ZEXPECT(root("\\x", windows).prefix == Prefix::None);
    ZEXPECT(root("\\x", windows).directory == "\\");
    // Only a letter makes a drive.
    ZEXPECT(root("1:x", windows).prefix == Prefix::None);
}

ZEST_CASE(windows_unc_roots_hold_the_share) {
    auto r = root(R"(\\server\share\x)", windows);
    ZEXPECT(r.prefix == Prefix::UNC);
    ZEXPECT(r.name == R"(\\server\share)");
    ZEXPECT(r.directory == "\\");
    ZEXPECT(root("//server/share", windows).name == "//server/share");
    ZEXPECT(root("//server/share", windows).directory == "");
    ZEXPECT(root(R"(\\server)", windows).name == R"(\\server)");
    ZEXPECT(root(R"(\\server)", windows).prefix == Prefix::UNC);
}

ZEST_CASE(windows_device_and_verbatim_prefixes) {
    auto device = root(R"(\\.\pipe\name)", windows);
    ZEXPECT(device.prefix == Prefix::Device);
    ZEXPECT(device.name == R"(\\.\pipe)");
    ZEXPECT(root("//./COM1", windows).prefix == Prefix::Device);

    auto drive = root(R"(\\?\C:\x)", windows);
    ZEXPECT(drive.prefix == Prefix::VerbatimDrive);
    ZEXPECT(drive.name == R"(\\?\C:)");
    ZEXPECT(drive.directory == "\\");

    auto unc = root(R"(\\?\UNC\server\share\x)", windows);
    ZEXPECT(unc.prefix == Prefix::VerbatimUNC);
    ZEXPECT(unc.name == R"(\\?\UNC\server\share)");

    auto volume = root(R"(\\?\Volume{1234}\x)", windows);
    ZEXPECT(volume.prefix == Prefix::Verbatim);
    ZEXPECT(volume.name == R"(\\?\Volume{1234})");
    // `C:x` after `\\?\` is no drive, and `/` separates nothing there.
    ZEXPECT(root(R"(\\?\C:x)", windows).prefix == Prefix::Verbatim);
    ZEXPECT(root(R"(\\?\C:/x)", windows).name == R"(\\?\C:/x)");
    // Spelled with `/`, `\\?\` is a device path, which Win32 normalizes.
    ZEXPECT(root("//?/C:/x", windows).prefix == Prefix::Device);
}

ZEST_CASE(windows_prefixes_end_before_a_separator_with_no_name_after_it) {
    auto share = root(R"(\\?\UNC\server\)", windows);
    ZEXPECT(share.prefix == Prefix::VerbatimUNC);
    ZEXPECT(share.name == R"(\\?\UNC\server)");
    ZEXPECT(share.directory == "\\");
    ZEXPECT(root(R"(\\?\UNC\server\\x)", windows).name == R"(\\?\UNC\server)");
    ZEXPECT(root(R"(\\?\UNC\server\\x)", windows).directory == R"(\\)");
    ZEXPECT(root(R"(\\?\UNC\)", windows).name == R"(\\?\UNC)");

    auto device = root(R"(\\.\)", windows);
    ZEXPECT(device.prefix == Prefix::Device);
    ZEXPECT(device.name == R"(\\.)");
    ZEXPECT(device.directory == "\\");
    ZEXPECT(root("//./", windows).name == "//.");

    auto verbatim = root(R"(\\?\)", windows);
    ZEXPECT(verbatim.prefix == Prefix::Verbatim);
    ZEXPECT(verbatim.name == R"(\\?)");
    ZEXPECT(verbatim.directory == "\\");
}

ZEST_CASE(absolute_means_independent_of_the_current_directory) {
    ZEXPECT(is_absolute("/x", posix));
    ZEXPECT(!is_absolute("x", posix));
    ZEXPECT(is_absolute("C:\\x", windows));
    ZEXPECT(is_absolute("C:/", windows));
    ZEXPECT(is_absolute(R"(\\server\share)", windows));
    ZEXPECT(is_absolute(R"(\\.\pipe\x)", windows));
    ZEXPECT(is_absolute(R"(\\?\C:)", windows));
    // The current drive's root, and a drive's current directory.
    ZEXPECT(!is_absolute("/x", windows));
    ZEXPECT(!is_absolute("\\x", windows));
    ZEXPECT(!is_absolute("C:x", windows));
    ZEXPECT(!is_absolute("C:", windows));
}

ZEST_CASE(rooted_paths_take_no_directory_in_front) {
    ZEXPECT(has_root("/x", posix));
    ZEXPECT(has_root("\\x", windows));
    ZEXPECT(has_root("C:\\x", windows));
    ZEXPECT(has_root(R"(\\server\share)", windows));
    ZEXPECT(!has_root("C:x", windows));
    ZEXPECT(!has_root("x", windows));
}

};  // ZEST_SUITE(fs_path_root)

}  // namespace

}  // namespace kota::fs::path
