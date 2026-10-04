#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "support/harness/throws.h"
#include "kota/zest/zest.h"
#include "kota/support/config.h"
#include "kota/support/small_vector.h"

namespace kota {

namespace {

/// An element whose allocations the instrumented std::allocator below counts and places.
struct Probe {
    int value = 0;
};

/// What std::allocator<Probe> did.
struct Ledger {
    int allocations = 0;
    int deallocations = 0;
    /// Where the next allocation goes, instead of where operator new puts it: memory of the
    /// test's own, which deallocation leaves alone.
    Probe* next = nullptr;
    std::byte* placed_first = nullptr;
    std::byte* placed_last = nullptr;
    /// Whether the next allocation not placed fails, with std::bad_alloc.
    bool fail_next = false;
};

Ledger ledger;

/// Counts std::allocator<Probe>'s calls for its lifetime.
struct Recording {
    Recording() {
        ledger = {};
    }

    Recording(const Recording&) = delete;
    Recording& operator=(const Recording&) = delete;

    ~Recording() {
        ledger = {};
    }

    /// Makes the next allocation `at`, which lies in `arena`.
    template <std::size_t Size>
    void place_next(Probe* at, std::byte (&arena)[Size]) {
        ledger.next = at;
        ledger.placed_first = arena;
        ledger.placed_last = arena + Size;
    }
};

}  // namespace

}  // namespace kota

/// Counts Probe's allocations, and can place the next one; what it places is the test's own
/// memory, and deallocating it only counts.
template <>
struct std::allocator<kota::Probe> {
    using value_type = kota::Probe;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;

    constexpr allocator() noexcept = default;

    template <typename U>
    constexpr allocator(const allocator<U>&) noexcept {}

    kota::Probe* allocate(std::size_t count) {
        kota::ledger.allocations += 1;
        if(kota::ledger.next != nullptr) {
            return std::exchange(kota::ledger.next, nullptr);
        }
        if(std::exchange(kota::ledger.fail_next, false)) {
            KOTA_THROW(std::bad_alloc{});
        }
        return static_cast<kota::Probe*>(::operator new(count * sizeof(kota::Probe)));
    }

// The arena's memory reaches deallocate(), which returns before deleting it; GCC sees the
// arena reach the delete below all the same.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
#endif
    void deallocate(kota::Probe* data, std::size_t) {
        kota::ledger.deallocations += 1;
        const auto* bytes = reinterpret_cast<const std::byte*>(data);
        std::less<const std::byte*> less;
        if(!less(bytes, kota::ledger.placed_first) && less(bytes, kota::ledger.placed_last)) {
            return;
        }
        ::operator delete(data);
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
};

namespace kota {

namespace {

using Probes = vector<Probe>;

/// Room for a Probes and, after it, the one element its first allocation holds, with room to
/// spare: the empty inline buffer of an N = 0 vector starts where the object ends.
struct Arena {
    alignas(Probes) std::byte bytes[2 * sizeof(Probes) + sizeof(Probe)];

    Probes* vector() {
        return reinterpret_cast<Probes*>(bytes);
    }

    /// Where the inline buffer of the vector placed at vector() begins.
    Probe* inline_buffer() {
        auto* v = std::construct_at(vector());
        auto* at = v->data();
        std::destroy_at(v);
        return at;
    }
};

ZEST_SUITE(support_small_vector_allocation) {

ZEST_CASE(allocation_where_the_inline_buffer_begins_is_traded) {
    Recording recording;
    Arena arena;
    auto* spot = arena.inline_buffer();
    auto* v = std::construct_at(arena.vector());
    recording.place_next(spot, arena.bytes);
    v->push_back(Probe{1});
    ZEXPECT(!v->inlined());
    ZEXPECT(v->data() != spot);
    ZASSERT(v->size() == 1U);
    ZEXPECT(v->front().value == 1);
    ZEXPECT(ledger.allocations == 2);
    ZEXPECT(ledger.deallocations == 1);
    std::destroy_at(v);
    ZEXPECT(ledger.deallocations == 2);
}

ZEST_CASE(move_leaves_an_allocation_where_its_inline_buffer_begins) {
    Recording recording;
    Arena arena;
    auto* spot = arena.inline_buffer();
    {
        Probes source;
        recording.place_next(spot, arena.bytes);
        source.push_back(Probe{7});
        ZASSERT(source.data() == spot);
        auto* moved = std::construct_at(arena.vector(), std::move(source));
        ZEXPECT(moved->data() != spot);
        ZEXPECT(!moved->inlined());
        ZASSERT(moved->size() == 1U);
        ZEXPECT(moved->front().value == 7);
        ZEXPECT(source.empty());
        std::destroy_at(moved);
    }
    ZEXPECT(ledger.allocations == ledger.deallocations);
}

ZEST_CASE(move_assignment_leaves_an_allocation_where_its_inline_buffer_begins) {
    Recording recording;
    Arena arena;
    auto* spot = arena.inline_buffer();
    {
        auto* target = std::construct_at(arena.vector());
        Probes source;
        recording.place_next(spot, arena.bytes);
        source.push_back(Probe{7});
        *target = std::move(source);
        ZEXPECT(target->data() != spot);
        ZEXPECT(!target->inlined());
        ZASSERT(target->size() == 1U);
        ZEXPECT(target->front().value == 7);
        std::destroy_at(target);
    }
    ZEXPECT(ledger.allocations == ledger.deallocations);
}

ZEST_CASE(swap_leaves_an_allocation_where_an_inline_buffer_begins) {
    Recording recording;
    Arena arena;
    auto* spot = arena.inline_buffer();
    {
        auto* v = std::construct_at(arena.vector());
        v->push_back(Probe{1});
        Probes other;
        recording.place_next(spot, arena.bytes);
        other.push_back(Probe{2});
        v->swap(other);
        ZEXPECT(v->data() != spot);
        ZASSERT(v->size() == 1U);
        ZEXPECT(v->front().value == 2);
        ZASSERT(other.size() == 1U);
        ZEXPECT(other.front().value == 1);
        std::destroy_at(v);
    }
    ZEXPECT(ledger.allocations == ledger.deallocations);
}

#if KOTA_ENABLE_EXCEPTIONS

ZEST_CASE(allocation_traded_is_freed_when_its_replacement_fails) {
    Recording recording;
    Arena arena;
    auto* spot = arena.inline_buffer();
    auto* v = std::construct_at(arena.vector());
    recording.place_next(spot, arena.bytes);
    ledger.fail_next = true;
    ZEXPECT(test::throws<std::bad_alloc>([&] { v->push_back(Probe{1}); }));
    ZEXPECT(v->empty());
    ZEXPECT(ledger.allocations == 2);
    ZEXPECT(ledger.deallocations == 1);
    std::destroy_at(v);
}

#endif

ZEST_CASE(from_raw_parts_leaves_a_buffer_where_the_inline_buffer_begins) {
    Recording recording;
    Arena arena;
    auto* spot = arena.inline_buffer();
    recording.place_next(spot, arena.bytes);
    auto* buffer = mem::allocate<Probe>(1);
    ZASSERT(buffer == spot);
    std::construct_at(buffer, Probe{5});
    // Placement new builds the result in the arena itself, rather than moving it there.
    auto* v =
        ::new (static_cast<void*>(arena.vector())) Probes(Probes::from_raw_parts(buffer, 1, 1));
    ZEXPECT(v->data() != spot);
    ZASSERT(v->size() == 1U);
    ZEXPECT(v->front().value == 5);
    std::destroy_at(v);
    ZEXPECT(ledger.allocations == ledger.deallocations);
}

ZEST_CASE(move_of_a_heap_vector_allocates_nothing) {
    Recording recording;
    small_vector<Probe, 1> source = {Probe{1}, Probe{2}};
    const auto allocations = ledger.allocations;
    small_vector<Probe, 1> moved(std::move(source));
    small_vector<Probe, 1> assigned;
    assigned = std::move(moved);
    ZEXPECT(ledger.allocations == allocations);
}

ZEST_CASE(copy_assignment_within_the_capacity_allocates_nothing) {
    Recording recording;
    small_vector<Probe, 1> target = {Probe{1}, Probe{2}, Probe{3}};
    small_vector<Probe, 1> source = {Probe{4}, Probe{5}};
    const auto allocations = ledger.allocations;
    target = source;
    ZEXPECT(ledger.allocations == allocations);
}

ZEST_CASE(every_allocation_is_freed) {
    Recording recording;
    {
        small_vector<Probe, 2> v;
        for(int i = 0; i < 100; ++i) {
            v.push_back(Probe{i});
        }
        v.resize(1);
        v.shrink_to_fit();
        ZEXPECT(v.inlined());
        ZEXPECT(ledger.allocations == ledger.deallocations);
        v.reserve(10);
    }
    ZEXPECT(ledger.allocations > 0);
    ZEXPECT(ledger.allocations == ledger.deallocations);
}

};  // ZEST_SUITE(support_small_vector_allocation)

}  // namespace

}  // namespace kota
