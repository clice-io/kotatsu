#include <cstddef>
#include <ranges>
#include <utility>
#include <vector>

#include "async/harness/loop_fixture.h"
#include "kota/zest/macro.h"
#include "kota/zest/zest.h"
#include "kota/async/async.h"

namespace kota {

namespace {

/// Records what a walk visits.
struct Collector : async_visitor<Collector> {
    std::vector<async_node::NodeKind> nodes;
    std::vector<sync_primitive::Kind> resources;
    std::vector<std::pair<const void*, const void*>> edges;
    bool enter_aggregates = true;

    bool visit_task(const task_frame& node) {
        nodes.push_back(node.kind);
        return true;
    }

    bool visit_wait_node(const wait_node& node) {
        nodes.push_back(node.kind);
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
    auto* node = target.operator->();

    // Checked while the waiters are alive: a waiter and its resource are
    // linked both ways, the resource listing its queue.
    struct Links {
        bool event_waiter_to_event = false;
        bool event_to_event_waiter = false;
        bool mutex_waiter_to_mutex = false;
        bool mutex_to_mutex_waiter = false;
    };

    auto inspect = [&]() -> task<std::pair<Collector, Links>> {
        Collector collector;
        collector.walk_node(*node);
        const void* event_waiter = gate.get_head();
        const void* mutex_waiter = lock.get_head();
        Links links{
            .event_waiter_to_event = collector.has_edge(event_waiter, &gate),
            .event_to_event_waiter = collector.has_edge(&gate, event_waiter),
            .mutex_waiter_to_mutex = collector.has_edge(mutex_waiter, &lock),
            .mutex_to_mutex_waiter = collector.has_edge(&lock, mutex_waiter),
        };
        gate.set();
        lock.unlock();
        co_return std::pair{std::move(collector), links};
    };

    auto [combined_result, walked] = run(std::move(target), inspect());
    EXPECT(combined_result.has_value());
    ASSERT(walked.has_value());
    auto& [collector, links] = *walked;
    EXPECT(collector.count(Kind::WhenAll) == 1U);
    EXPECT(collector.count(Kind::EventWaiter) == 1U);
    EXPECT(collector.count(Kind::MutexWaiter) == 1U);
    EXPECT(collector.count(Kind::Task) >= 3U);
    EXPECT(zest::contains(collector.resources, sync_primitive::Kind::Event));
    EXPECT(zest::contains(collector.resources, sync_primitive::Kind::Mutex));
    EXPECT(links.event_waiter_to_event);
    EXPECT(links.event_to_event_waiter);
    EXPECT(links.mutex_waiter_to_mutex);
    EXPECT(links.mutex_to_mutex_waiter);
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
    auto* node = target.operator->();
    auto inspect = [&]() -> task<Collector> {
        Collector collector;
        collector.enter_aggregates = false;
        collector.walk_node(*node);
        gate.set();
        co_return collector;
    };

    auto [combined_result, walked] = run(std::move(target), inspect());
    EXPECT(combined_result.has_value());
    ASSERT(walked.has_value());
    EXPECT(walked->count(Kind::WhenAll) == 1U);
    EXPECT(walked->count(Kind::EventWaiter) == 0U);
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
    auto* node = target.operator->();
    auto inspect = [&]() -> task<std::vector<std::size_t>> {
        Collector collector;
        collector.walk_node(*node);
        std::vector<std::size_t> events{collector.resources.size()};
        collector.walk_node(*node);
        events.push_back(collector.resources.size());
        collector.reset();
        collector.walk_node(*node);
        events.push_back(collector.resources.size());
        gate.set();
        co_return events;
    };

    auto [combined_result, sizes] = run(std::move(target), inspect());
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
    auto* node = target.operator->();

    // Checked while the waiter is queued: it names its resource and its task,
    // and a task names no resource.
    struct Links {
        bool queued = false;
        bool waiter_to_mutex = false;
        bool waiter_to_task = false;
        bool task_to_nothing = false;
    };

    auto inspect = [&]() -> task<Links> {
        const auto* queued = lock.get_head();
        Links links{.queued = queued != nullptr, .task_to_nothing = get_resource(*node) == nullptr};
        if(queued) {
            links.waiter_to_mutex = get_resource(*queued) == &lock;
            links.waiter_to_task = get_parent(*queued) == node;
        }
        lock.unlock();
        co_return links;
    };

    auto [waited, links] = run(std::move(target), inspect());
    EXPECT(waited.has_value());
    ASSERT(links.has_value());
    ASSERT(links->queued);
    EXPECT(links->waiter_to_mutex);
    EXPECT(links->waiter_to_task);
    EXPECT(links->task_to_nothing);
}

};  // ZEST_SUITE(async_runtime_walk)

}  // namespace

}  // namespace kota
