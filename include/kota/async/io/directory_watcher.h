#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"

namespace kota {

class event_loop;

class directory_watcher {
public:
    directory_watcher() noexcept;

    directory_watcher(const directory_watcher&) = delete;
    directory_watcher& operator=(const directory_watcher&) = delete;

    directory_watcher(directory_watcher&&) noexcept;
    directory_watcher& operator=(directory_watcher&&) noexcept;

    ~directory_watcher();

    enum class effect : std::uint8_t {
        create,
        modify,
        destroy,
        rename,
        other,
    };

    struct change {
        std::string path;
        effect type;
        std::string associated;
        bool is_directory;
    };

    struct options {
        std::chrono::milliseconds debounce;

        constexpr options(std::chrono::milliseconds debounce = std::chrono::milliseconds{200}) :
            debounce(debounce) {}
    };

    static result<directory_watcher> create(const char* path,
                                            options opts = {},
                                            event_loop& loop = event_loop::current());

    task<std::vector<change>, error> next();

    void close();

private:
    struct Self;

    explicit directory_watcher(std::shared_ptr<Self> self) noexcept;

    std::shared_ptr<Self> self;
};

}  // namespace kota
