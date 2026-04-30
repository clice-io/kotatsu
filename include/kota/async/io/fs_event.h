#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kota/async/io/loop.h"
#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"

namespace kota {

class fs_event {
public:
    fs_event() noexcept;

    fs_event(const fs_event&) = delete;
    fs_event& operator=(const fs_event&) = delete;

    fs_event(fs_event&&) noexcept;
    fs_event& operator=(fs_event&&) noexcept;

    ~fs_event();

    enum class effect : std::uint8_t {
        create,
        modify,
        destroy,
        rename,
        overflow,
        other,
    };

    struct change {
        std::string path;
        effect type = effect::other;
        std::string old_path;
    };

    constexpr static std::chrono::milliseconds default_debounce{200};

    struct options {
        std::chrono::milliseconds debounce = default_debounce;
        bool recursive = true;
    };

    static result<fs_event> create(std::string_view path,
                                   options opts,
                                   event_loop& loop = event_loop::current());

    static result<fs_event> create(std::string_view path, event_loop& loop = event_loop::current());

    task<std::vector<change>, error> next();

    void stop();

private:
    struct Self;

    explicit fs_event(std::shared_ptr<Self> self) noexcept;

    // shared_ptr (not unique_handle): macOS/Windows callbacks run on background
    // threads and must prevent Self destruction via shared_from_this().
    std::shared_ptr<Self> self;
};

}  // namespace kota
