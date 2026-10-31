// copy_file() and rename_noreplace() where the system calls they start with
// fail, as on an old kernel or a file system without them: a filter of system
// calls fails copy_file_range with ENOSYS, as Linux before 4.5 does, sendfile
// with EINVAL, as a file it cannot splice does, and renameat2 with EINVAL, as
// NFS does, then with ENOSYS, as Linux before 3.15 does. A program of its
// own, since a filter holds for the whole process and is never lifted; Linux's
// alone.

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <format>
#include <latch>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <string>
#include <string_view>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

#include "kotatsu/fs/directory.h"
#include "kotatsu/fs/file.h"
#include "kotatsu/fs/status.h"

// The numbers the library takes where the C library's headers lack them.
#ifndef SYS_copy_file_range
#if defined(__x86_64__)
#define SYS_copy_file_range 326
#elif defined(__aarch64__) || defined(__riscv)
#define SYS_copy_file_range 285
#endif
#endif
#ifndef SYS_renameat2
#if defined(__x86_64__)
#define SYS_renameat2 316
#elif defined(__aarch64__) || defined(__riscv)
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

/// Installs `filter`, which holds from now on.
template <std::size_t N>
bool install(std::array<sock_filter, N> filter) {
    sock_fprog program{.len = static_cast<unsigned short>(N), .filter = filter.data()};
    return ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
           ::prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}

/// Fails copy_file_range, sendfile and renameat2 from now on.
bool deny_calls() {
    return install(std::to_array<sock_filter>({
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_copy_file_range, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_sendfile, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EINVAL),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_renameat2, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EINVAL),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    }));
}

/// Fails renameat2 with ENOSYS from now on: of two filters that fail a call,
/// the newer says how.
bool deny_renameat2() {
    return install(std::to_array<sock_filter>({
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_renameat2, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | ENOSYS),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    }));
}

/// Whether a system call's `result` says it failed with `expected`.
bool denied(long result, int expected) {
    return result == -1 && errno == expected;
}

std::string read(const std::string& path) {
    std::string text;
    check(!kotatsu::fs::read_file(text, path), std::format("read {}", path));
    return text;
}

void write(const std::string& path, std::string_view text) {
    check(!kotatsu::fs::write_file(path, text), std::format("write {}", path));
}

/// Text of `size` bytes that is no repetition of a short run.
std::string text_of(std::size_t size) {
    std::string text(size, '\0');
    for(std::size_t i = 0; i < size; ++i) {
        text[i] = static_cast<char>('a' + (i * 7 + i / 26) % 26);
    }
    return text;
}

/// The copy goes through the loop of reads and writes.
void copy_reads_and_writes(const std::string& root) {
    using Time = std::chrono::sys_time<std::chrono::nanoseconds>;
    constexpr auto past = Time(std::chrono::nanoseconds(1'000'000'000'123'456'789));
    auto text = text_of(1024 * 1024 + 7);
    auto from = root + "/copied";
    auto to = root + "/copy";
    write(from, text);
    check(!kotatsu::fs::set_permissions(from, 0640), "set_permissions");
    check(!kotatsu::fs::set_times(from, {.mtime = past}), "set_times");
    check(!kotatsu::fs::copy_file(from, to), "copy_file");
    check(read(to) == text, "copy_file copies the bytes");
    auto copied = kotatsu::fs::status(to);
    if(check(copied.has_value(), "status of the copy")) {
        check(copied->permissions == 0640, "copy_file takes the permissions");
        check(copied->mtime == past, "copy_file takes the mtime");
    }
}

/// A write that fails, past the limit of a file's size, fails the copy, which
/// leaves what it wrote before.
void copy_fails_with_a_write(const std::string& root) {
    constexpr std::size_t limit = 64 * 1024;
    auto from = root + "/large";
    auto to = root + "/cut";
    write(from, text_of(4 * limit));
    // The signal would end the process; without it the write fails.
    std::signal(SIGXFSZ, SIG_IGN);
    rlimit original;
    check(::getrlimit(RLIMIT_FSIZE, &original) == 0, "getrlimit");
    rlimit lowered = original;
    lowered.rlim_cur = limit;
    check(::setrlimit(RLIMIT_FSIZE, &lowered) == 0, "setrlimit");
    auto error = kotatsu::fs::copy_file(from, to);
    check(::setrlimit(RLIMIT_FSIZE, &original) == 0, "setrlimit back");
    check(error == std::errc::file_too_large, "copy_file past the limit fails");
    auto cut = kotatsu::fs::status(to);
    if(check(cut.has_value(), "status of the cut copy")) {
        check(cut->size == limit, "copy_file leaves what it wrote");
    }
}

/// Renames go through a hard link.
void rename_links(const std::string& root) {
    auto from = root + "/from";
    auto to = root + "/to";
    auto taken = root + "/taken";
    write(from, "new");
    write(taken, "old");
    check(!kotatsu::fs::rename_noreplace(from, to), "rename_noreplace of a file");
    check(kotatsu::fs::try_exists(from) == false, "rename_noreplace takes the old name");
    check(read(to) == "new", "rename_noreplace gives the new name");

    check(kotatsu::fs::rename_noreplace(to, taken) == std::errc::file_exists,
          "rename_noreplace over a file fails");
    check(read(to) == "new", "rename_noreplace over a file keeps the renamed");
    check(read(taken) == "old", "rename_noreplace over a file keeps what was there");

    check(!kotatsu::fs::create_symlink("to", root + "/link"), "create_symlink");
    check(!kotatsu::fs::rename_noreplace(root + "/link", root + "/moved"),
          "rename_noreplace of a symlink");
    check(kotatsu::fs::is_symlink(root + "/moved"), "rename_noreplace renames a symlink itself");

    auto directory = root + "/directory";
    check(kotatsu::fs::create_directory(directory) == true, "create_directory");
    check(kotatsu::fs::rename_noreplace(directory, root + "/elsewhere") ==
              std::errc::invalid_argument,
          "rename_noreplace of a directory fails as the system refused it");
    check(kotatsu::fs::is_directory(directory), "rename_noreplace of a directory keeps it");
}

/// Runs `rename(i)` on `renamers` threads at once: what each returned.
template <std::size_t renamers, typename Rename>
std::array<std::error_code, renamers> race(Rename rename) {
    std::array<std::error_code, renamers> errors;
    std::latch start(renamers);
    std::vector<std::thread> threads;
    for(std::size_t i = 0; i < renamers; ++i) {
        threads.emplace_back([&, i] {
            start.arrive_and_wait();
            errors[i] = rename(i);
        });
    }
    for(auto& thread: threads) {
        thread.join();
    }
    return errors;
}

/// Of renames that race of files of their own to one name, one renames and
/// the others fail; of one file to names of their own, at least one.
void rename_races(const std::string& root) {
    constexpr std::size_t renamers = 4;
    for(int round = 0; round < 50; ++round) {
        auto one = std::format("{}/one-{}", root, round);
        auto own = [&](std::size_t i) {
            return std::format("{}/own-{}-{}", root, round, i);
        };
        for(std::size_t i = 0; i < renamers; ++i) {
            write(own(i), std::to_string(i));
        }
        auto to_one = race<renamers>(
            [&](std::size_t i) { return kotatsu::fs::rename_noreplace(own(i), one); });
        check(std::ranges::count(to_one, std::error_code()) == 1,
              std::format("round {}: one rename to one name succeeds", round));
        check(std::ranges::count(to_one, std::make_error_code(std::errc::file_exists)) ==
                  renamers - 1,
              std::format("round {}: the other renames to one name find it taken", round));

        // The one file goes to names of its own: those that link it before
        // its old name goes all succeed.
        auto text = read(one);
        auto to_own = race<renamers>(
            [&](std::size_t i) { return kotatsu::fs::rename_noreplace(one, own(i) + "-moved"); });
        check(kotatsu::fs::try_exists(one) == false,
              std::format("round {}: the one file loses its old name", round));
        for(std::size_t i = 0; i < renamers; ++i) {
            auto what = std::format("round {}, renamer {}", round, i);
            if(to_own[i]) {
                check(to_own[i] == std::errc::no_such_file_or_directory,
                      std::format("{}: a rename that lost finds the file gone", what));
                check(kotatsu::fs::try_exists(own(i) + "-moved") == false,
                      std::format("{}: a rename that lost names nothing", what));
            } else {
                check(read(own(i) + "-moved") == text,
                      std::format("{}: a rename that won names the one file", what));
            }
        }
        check(std::ranges::count(to_own, std::error_code()) >= 1,
              std::format("round {}: a rename of one file succeeds", round));
    }
}

/// With renameat2 failing as before Linux 3.15, a directory fails with what
/// the kernel said.
void rename_without_renameat2(const std::string& root) {
    auto directory = root + "/directory";
    check(deny_renameat2(), "install the second filter");
    check(kotatsu::fs::rename_noreplace(directory, root + "/elsewhere") ==
              std::errc::function_not_supported,
          "rename_noreplace of a directory fails as the old kernel refused it");
    check(kotatsu::fs::is_directory(directory), "rename_noreplace of a directory keeps it");
}

}  // namespace

int main() {
    const char* temp = std::getenv("TMPDIR");
    auto root = std::format("{}/kotatsu-XXXXXX", temp != nullptr && *temp != '\0' ? temp : "/tmp");
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
        copy_fails_with_a_write(root);
        rename_links(root);
        rename_races(root);
        rename_without_renameat2(root);
    }
    check(kotatsu::fs::remove_all(root).has_value(), std::format("remove_all {}", root));
    return failures == 0 ? 0 : 1;
}
