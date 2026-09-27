#include <algorithm>
#include <cstddef>
#include <ranges>
#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"
#include "kota/async/runtime/walk.h"

namespace kota {

namespace {

/// Records what a walk visits.
struct Collector : async_visitor<Collector> {
    std::vector<async_node::NodeKind> nodes;
    std::vector<const void*> tasks;
    std::vector<const void*> waiters;
    std::vector<sync_primitive::Kind> resources;
    std::vector<std::pair<const void*, const void*>> edges;
    bool enter_aggregates = true;

    bool visit_task(const task_frame& node) {
        nodes.push_back(node.kind);
        tasks.push_back(&node);
        return true;
    }

    bool visit_wait_node(const wait_node& node) {
        nodes.push_back(node.kind);
        waiters.push_back(&node);
        return true;
    }

    bool visit_aggregate(const aggregate_op& node) {
        nodes.push_back(node.kind);
        return enter_aggregates;
    }

    bool visit_io(const io_op& node) {
        nodes.push_back(node.kind);
        return true;
    }

    bool visit_sync(const sync_primitive& resource) {
        resources.push_back(resource.kind);
        return true;
    }

    void visit_edge(const void* from, const void* to) {
        edges.emplace_back(from, to);
    }

    bool has_edge(const void* from, const void* to) const {
        return std::ranges::contains(edges, std::pair{from, to});
    }

    /// Some waiter and `resource` are linked both ways: the waiter names the
    /// resource, and the resource lists the waiter in its queue.
    bool linked_both_ways(const void* resource) const {
        return std::ranges::any_of(waiters, [&](const void* waiter) {
            return has_edge(waiter, resource) && has_edge(resource, waiter);
        });
    }

    std::size_t count(async_node::NodeKind kind) const {
        return static_cast<std::size_t>(std::ranges::count(nodes, kind));
    }
};

using Kind = async_node::NodeKind;

ZEST_SUITE(async_runtime_walk, test::LoopFixture) {

ZEST_CASE(visitor_walks_down_to_the_resources) {
    event gate;
    mutex lock;
    ASSERT(lock.try_lock());
    auto on_event = [&]() -> task<> {
        co_await gate.wait();
    };
    auto on_mutex = [&]() -> task<> {
        co_await lock.lock();
        lock.unlock();
    };
    auto combined = [&]() -> task<> {
        co_await when_all(on_event(), on_mutex());
    };
    auto target = combined();

    // Walked while the waiters are alive.
    auto inspect = [&]() -> task<Collector> {
        Collector collector;
        collector.walk(target);
        gate.set();
        lock.unlock();
        co_return collector;
    };

    auto [combined_result, walked] = run(target, inspect());
    EXPECT(combined_result.has_value());
    ASSERT(walked.has_value());
    EXPECT(walked->count(Kind::WhenAll) == 1U);
    EXPECT(walked->count(Kind::Waiter) == 2U);
    EXPECT(walked->count(Kind::Task) >= 3U);
    EXPECT(zest::contains(walked->resources, sync_primitive::Kind::Event));
    EXPECT(zest::contains(walked->resources, sync_primitive::Kind::Mutex));
    EXPECT(walked->linked_both_ways(&gate));
    EXPECT(walked->linked_both_ways(&lock));
}

ZEST_CASE(visitor_skips_the_children_of_a_node_it_rejects) {
    event gate;
    auto waiter = [&]() -> task<> {
        co_await gate.wait();
    };
    auto combined = [&]() -> task<> {
        co_await when_all(waiter(), waiter());
    };
    auto target = combined();
    auto inspect = [&]() -> task<Collector> {
        Collector collector;
        collector.enter_aggregates = false;
        collector.walk(target);
        gate.set();
        co_return collector;
    };

    auto [combined_result, walked] = run(target, inspect());
    EXPECT(combined_result.has_value());
    ASSERT(walked.has_value());
    EXPECT(walked->count(Kind::WhenAll) == 1U);
    EXPECT(walked->count(Kind::Waiter) == 0U);
    EXPECT(walked->resources.empty());
}

ZEST_CASE(walk_visits_a_shared_resource_once) {
    event gate;
    auto waiter = [&]() -> task<> {
        co_await gate.wait();
    };
    auto combined = [&]() -> task<> {
        co_await when_all(waiter(), waiter());
    };
    auto target = combined();
    auto inspect = [&]() -> task<std::vector<std::size_t>> {
        Collector collector;
        collector.walk(target);
        std::vector<std::size_t> events{collector.resources.size()};
        collector.walk(target);
        events.push_back(collector.resources.size());
        collector.reset();
        collector.walk(target);
        events.push_back(collector.resources.size());
        gate.set();
        co_return events;
    };

    auto [combined_result, sizes] = run(target, inspect());
    EXPECT(combined_result.has_value());
    ASSERT(sizes.has_value());
    // Two waiters share the event: once per walk, again only after reset().
    EXPECT(*sizes == std::vector<std::size_t>{1, 1, 2});
}

ZEST_CASE(waiter_links_its_task_and_its_resource) {
    mutex lock;
    ASSERT(lock.try_lock());
    auto waiter = [&]() -> task<> {
        co_await lock.lock();
        lock.unlock();
    };
    auto target = waiter();

    // Walked while the waiter is queued: the task points at its waiter, the
    // waiter at its resource, and the task at no resource.
    auto inspect = [&]() -> task<std::pair<Collector, bool>> {
        Collector collector;
        collector.walk(target);
        bool queued = lock.has_waiters();
        lock.unlock();
        co_return std::pair{std::move(collector), queued};
    };

    auto [waited, walked] = run(target, inspect());
    EXPECT(waited.has_value());
    ASSERT(walked.has_value());
    auto& [collector, queued] = *walked;
    ASSERT(queued);
    ASSERT(collector.tasks.size() == 1U);
    ASSERT(collector.waiters.size() == 1U);
    const void* task = collector.tasks.front();
    const void* waiting = collector.waiters.front();
    EXPECT(collector.has_edge(task, waiting));
    EXPECT(collector.has_edge(waiting, &lock));
    EXPECT(!collector.has_edge(task, &lock));
}

};  // ZEST_SUITE(async_runtime_walk)

}  // namespace

}  // namespace kota
