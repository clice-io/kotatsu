#pragma once

#include "eventide/async/async.h"

namespace eventide {

struct loop_fixture {
    event_loop loop;

    template <typename... Tasks>
    void schedule_all(Tasks&... tasks) {
        (loop.schedule(tasks), ...);
        loop.run();
    }
};

}  // namespace eventide
