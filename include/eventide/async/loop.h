#pragma once

#include <memory>
#include <source_location>
#include <tuple>

struct uv_loop_s;
using uv_loop_t = uv_loop_s;

namespace eventide {

class async_node;

template <typename T>
class task;

template <typename T>
class shared_task;

class event_loop {
public:
    event_loop();

    ~event_loop();

    static event_loop& current();

    struct self;

    self* operator->() {
        return self.get();
    }

    friend class async_node;

public:
    operator uv_loop_t&() noexcept;

    operator const uv_loop_t&() const noexcept;

    int run();

    void stop();

    template <typename Task>
    void schedule(Task&& task, std::source_location location = std::source_location::current()) {
        auto& promise = task.h.promise();
        if constexpr(std::is_rvalue_reference_v<Task&&>) {
            promise.root = true;
            task.release();
        }

        schedule(static_cast<async_node&>(promise), location);
    }

private:
    void schedule(async_node& frame, std::source_location location);

    std::unique_ptr<self> self;
};

template <typename... Tasks>
auto run(Tasks&&... tasks) {
    event_loop loop;
    (loop.schedule(tasks), ...);
    loop.run();
    return std::tuple(std::move(tasks.value())...);
}

}  // namespace eventide
