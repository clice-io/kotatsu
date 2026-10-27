// copy_file() and rename_noreplace() where the system calls they start with
// fail, as on an old kernel or a file system without them: a filter of system
// calls fails copy_file_range with ENOSYS, as Linux before 4.5 does, sendfile
// with EINVAL, as a file it cannot splice does, and renameat2 with EINVAL, as
// NFS does. A program of its own, since the filter holds for the whole
// process and is never lifted; Linux's alone.

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <format>
#include <iterator>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <string>
#include <string_view>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <system_error>
#include <unistd.h>

#include "kota/fs/directory.h"
#include "kota/fs/file.h"
#include "kota/fs/status.h"

// The numbers the library takes where the C library's headers lack them.
#ifndef SYS_copy_file_range
#ifdef __x86_64__
#define SYS_copy_file_range 326
#else
#define SYS_copy_file_range 285
#endif
#endif
#ifndef SYS_renameat2
#ifdef __x86_64__
#define SYS_renameat2 316
#else
#define SYS_renameat2 276
#endif
#endif

namespace {

int failures = 0;

/// Whether `passed`; prints `what` when not.
bool check(bool passed, std::string_view what) {
    if(!passed) {
        std::fprintf(stderr, "failed: %s\n", std::string(what).c_str());
        ++failures;
    }
    return passed;
}

/// Fails copy_file_range, sendfile and renameat2 from now on.
bool deny_calls() {
    sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_copy_file_range, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_sendfile, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EINVAL),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_renameat2, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EINVAL),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    sock_fprog program{.len = static_cast<unsigned short>(std::size(filter)), .filter = filter};
    return ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
           ::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}

/// Whether a system call's `result` says it failed with `expected`.
bool denied(long result, int expected) {
    return result == -1 && errno == expected;
}

std::string read(const std::string& path) {
    std::string text;
    check(!kota::fs::read_file(text, path), std::format("read {}", path));
    return text;
}

void write(const std::string& path, std::string_view text) {
    check(!kota::fs::write_file(path, text), std::format("write {}", path));
}

/// The copy goes through the loop of reads and writes.
void copy_reads_and_writes(const std::string& root) {
    using Time = std::chrono::sys_time<std::chrono::nanoseconds>;
    constexpr auto past = Time(std::chrono::nanoseconds(1'000'000'000'123'456'789));
    std::string text(1024 * 1024 + 7, '\0');
    for(std::size_t i = 0; i < text.size(); ++i) {
        text[i] = static_cast<char>('a' + (i * 7 + i / 26) % 26);
    }
    auto from = root + "/copied";
    auto to = root + "/copy";
    write(from, text);
    check(!kota::fs::set_permissions(from, 0640), "set_permissions");
    check(!kota::fs::set_times(from, {.mtime = past}), "set_times");
    check(!kota::fs::copy_file(from, to), "copy_file");
    check(read(to) == text, "copy_file copies the bytes");
    auto copied = kota::fs::status(to);
    if(check(copied.has_value(), "status of the copy")) {
        check(copied->permissions == 0640, "copy_file takes the permissions");
        check(copied->mtime == past, "copy_file takes the mtime");
    }
}

/// Renames go through a hard link.
void rename_links(const std::string& root) {
    auto from = root + "/from";
    auto to = root + "/to";
    auto taken = root + "/taken";
    write(from, "new");
    write(taken, "old");
    check(!kota::fs::rename_noreplace(from, to), "rename_noreplace of a file");
    check(kota::fs::exists(from) == false, "rename_noreplace takes the old name");
    check(read(to) == "new", "rename_noreplace gives the new name");

    check(kota::fs::rename_noreplace(to, taken) == std::errc::file_exists,
          "rename_noreplace over a file fails");
    check(read(to) == "new", "rename_noreplace over a file keeps the renamed");
    check(read(taken) == "old", "rename_noreplace over a file keeps what was there");

    check(!kota::fs::create_symlink("to", root + "/link"), "create_symlink");
    check(!kota::fs::rename_noreplace(root + "/link", root + "/moved"),
          "rename_noreplace of a symlink");
    check(kota::fs::is_symlink(root + "/moved"), "rename_noreplace renames a symlink itself");

    auto directory = root + "/directory";
    check(kota::fs::create_directory(directory) == true, "create_directory");
    check(kota::fs::rename_noreplace(directory, root + "/elsewhere") == std::errc::invalid_argument,
          "rename_noreplace of a directory fails as the system refused it");
    check(kota::fs::is_directory(directory), "rename_noreplace of a directory keeps it");
}

}  // namespace

int main() {
    const char* temp = std::getenv("TMPDIR");
    auto root = std::format("{}/kota-XXXXXX", temp != nullptr && *temp != '\0' ? temp : "/tmp");
    if(::mkdtemp(root.data()) == nullptr) {
        std::perror("mkdtemp");
        return 1;
    }
    if(check(deny_calls(), "install the filter of system calls")) {
        // What the filter does, lest the checks below pass without it.
        auto from = root + "/nothing";
        auto to = root + "/nothing-either";
        check(denied(::syscall(SYS_copy_file_range, -1, nullptr, -1, nullptr, 1, 0u), ENOSYS),
              "the filter fails copy_file_range");
        check(denied(::syscall(SYS_sendfile, -1, -1, nullptr, 1), EINVAL),
              "the filter fails sendfile");
        check(denied(::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 1),
                     EINVAL),
              "the filter fails renameat2");
        copy_reads_and_writes(root);
        rename_links(root);
    }
    check(kota::fs::remove_all(root).has_value(), std::format("remove_all {}", root));
    return failures == 0 ? 0 : 1;
}
