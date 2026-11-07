#pragma once

// DiskImage on macOS, a volume of its own for the tests that need another
// file system than the system temp directory's, or one that goes away.

#ifdef __APPLE__
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <thread>

#include "fs/harness/files.h"
#include "kotatsu/zest/zest.h"

namespace kotatsu::test {

/// A disk image of 1 MB of `file_system`, as `hdiutil create -fs` names it,
/// in a test's directory, attached beside it, which hdiutil lets any user do;
/// detached, as though the disk went, when this goes or a failed ZASSERT ends
/// the process.
struct DiskImage {
    DiskImage(const TempDir& dir, std::string_view file_system) :
        image(dir.file("image.dmg")), mount(dir.file("volume")) {
        std::filesystem::create_directory(std_path(mount));
        ZASSERT(hdiutil(
            std::format("create -size 1m -fs '{}' -volname kotatsu '{}'", file_system, image)));
        attach(false);
    }

    DiskImage(const DiskImage&) = delete;
    DiskImage& operator=(const DiskImage&) = delete;

    ~DiskImage() {
        detach();
    }

    /// `name` on the volume.
    std::string file(std::string_view name) const {
        return std::format("{}/{}", mount, name);
    }

    void attach(bool read_only) {
        // The disk's device, which hdiutil names first, before its volume:
        // through a file, as the helper hdiutil leaves running would keep a
        // pipe open.
        auto listed = image + ".attached";
        attached = std::system(std::format("hdiutil attach {} -nobrowse -noverify -noautofsck "
                                           "-mountpoint '{}' '{}' > '{}'",
                                           read_only ? "-readonly" : "",
                                           mount,
                                           image,
                                           listed)
                                   .c_str()) == 0;
        ZASSERT(attached);
        auto text = read_text(listed);
        device = text.substr(0, text.find_first_of(" \t\n"));
        ZASSERT(device.starts_with("/dev/"));
    }

    /// Detaches it, files open on it or not: false when it stays attached.
    /// A daemon may hold the disk for a moment once its volume is unmounted,
    /// which -force does not override ("Resource busy"): the disk is tried
    /// again by its device, a few times.
    bool detach() {
        for(int tries = 0; attached && tries < 10; ++tries) {
            if(tries != 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            attached = !hdiutil(std::format("detach -force '{}'", device));
        }
        return !attached;
    }

private:
    static bool hdiutil(std::string_view arguments) {
        return std::system(std::format("hdiutil {} > /dev/null", arguments).c_str()) == 0;
    }

    const std::string image;
    const std::string mount;
    std::string device;
    bool attached = false;
    zest::FatalHook on_fatal{[this] { detach(); }};
};

}  // namespace kotatsu::test
#endif
