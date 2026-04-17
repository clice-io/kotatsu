#pragma once

#include "kota/async/async.h"

namespace kota {

struct loop_fixture {
    event_loop loop;

    template <typename... Tasks>
    void schedule_all(Tasks&... tasks) {
        (loop.schedule(tasks), ...);
        loop.run();
    }
};

}  // namespace kota
