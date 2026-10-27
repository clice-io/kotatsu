// remove_all() with each allocation it makes failing in turn: every
// descriptor its walk opened is closed by the time std::bad_alloc reaches the
// caller. A program of its own, since it replaces the global operator new,
// which a test binary shares with zest and every other test; built where the
// walk holds descriptors (POSIX) and an allocation can throw.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <format>
#include <new>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>

#include "kota/fs/directory.h"

namespace {

/// The allocations left until the one that fails, zero while none is to.
std::size_t countdown = 0;

int failures = 0;

/// Whether `passed`; prints `what` when not.
bool check(bool passed, std::string_view what) {
    if(!passed) {
        std::fprintf(stderr, "failed: %s\n", std::string(what).c_str());
        ++failures;
    }
    return passed;
}

/// The descriptor the next open() takes, the lowest free.
int lowest_free() {
    int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    ::close(fd);
    return fd;
}

/// Makes at `root` a tree with a directory inside, which the walk holds open
/// while it lists that one's entries, and names longer than a string holds
/// in place, which listing allocates for.
void make_tree(const std::string& root) {
    constexpr std::string_view name = "a-name-longer-than-a-short-string";
    auto inner = std::format("{}/{}", root, name);
    check(::mkdir(root.c_str(), 0700) == 0, std::format("mkdir {}", root));
    check(::mkdir(inner.c_str(), 0700) == 0, std::format("mkdir {}", inner));
    for(const auto& directory: {root, inner}) {
        for(int i = 0; i < 3; ++i) {
            auto path = std::format("{}/{}-{}", directory, name, i);
            int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
            if(check(fd >= 0, std::format("create {}", path))) {
                ::close(fd);
            }
        }
    }
}

}  // namespace

void* operator new(std::size_t size) {
    if(countdown != 0 && --countdown == 0) {
        throw std::bad_alloc();
    }
    if(void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

int main() {
    const char* temp = std::getenv("TMPDIR");
    auto base = std::format("{}/kota-XXXXXX", temp != nullptr && *temp != '\0' ? temp : "/tmp");
    if(::mkdtemp(base.data()) == nullptr) {
        std::perror("mkdtemp");
        return 1;
    }
    // Each round fails one allocation later than the one before, until
    // remove_all() makes all it needs and removes the tree.
    std::size_t failed = 0;
    for(std::size_t round = 1;; ++round) {
        auto root = std::format("{}/{}", base, round);
        make_tree(root);
        int free_before = lowest_free();
        bool finished = false;
        countdown = round;
        try {
            auto removed = kota::fs::remove_all(root);
            countdown = 0;
            check(removed.has_value(), std::format("remove_all {}", root));
            finished = true;
        } catch(const std::bad_alloc&) {
            countdown = 0;
            ++failed;
        }
        check(lowest_free() == free_before,
              std::format("allocation {} failing leaves a descriptor open", round));
        if(finished) {
            break;
        }
    }
    check(failed > 0, "remove_all allocates nothing that could fail");
    check(kota::fs::remove_all(base).has_value(), std::format("remove_all {}", base));
    return failures == 0 ? 0 : 1;
}
