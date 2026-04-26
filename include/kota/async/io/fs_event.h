#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "kota/async/runtime/task.h"
#include "kota/async/vocab/error.h"

namespace kota {

class event_loop;

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

    struct options {
        std::chrono::milliseconds debounce;
        bool recursive;

        constexpr options() : debounce(std::chrono::milliseconds{200}), recursive(true) {}

        explicit constexpr options(std::chrono::milliseconds debounce, bool recursive = true) :
            debounce(debounce), recursive(recursive) {}
    };

    static result<fs_event> create(std::string_view path,
                                   options opts = {},
                                   event_loop& loop = event_loop::current());

    task<std::vector<change>, error> next();

    void close();

private:
    struct Self;

    explicit fs_event(std::shared_ptr<Self> self) noexcept;

    std::shared_ptr<Self> self;
};

}  // namespace kota
