#include "kota/zest/assert/check.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <print>
#include <string_view>
#include <thread>
#include <vector>

#include "execution.h"
#include "kota/zest/assert/trace.h"
#include "kota/zest/runner/registry.h"

namespace kota::zest {

namespace {

struct ContextEntry {
    std::uint64_t id;
    std::string message;
};

/// Contexts entered on this thread and not yet ended, outermost first. It
/// holds their messages rather than the contexts: one that ends on another
/// thread, as a coroutine's may, is left here instead of dangling.
std::vector<ContextEntry>& contexts() {
    thread_local std::vector<ContextEntry> stack;
    return stack;
}

/// Prints `text` under `label`, continuing its later lines at the same indent.
void print_line(std::string_view label, std::string_view text) {
    constexpr std::string_view indent = "           ";
    bool first = true;
    while(true) {
        auto newline = text.find('\n');
        auto line = text.substr(0, newline);
        if(first && !label.empty()) {
            std::println("{}{}: {}", indent, label, line);
        } else {
            std::println("{}{}", indent, line);
        }
        first = false;
        if(newline == std::string_view::npos) {
            return;
        }
        text.remove_prefix(newline + 1);
    }
}

void print_contexts() {
    for(const auto& context: contexts()) {
        print_line("context", context.message);
    }
}

struct Hook {
    std::uint64_t id;
    function<void()> run;
};

/// The fatal hooks alive, oldest first. The thread ending the process holds
/// the mutex to the end, and its hooks may create or destroy hooks of their
/// own, hence recursive.
struct Hooks {
    std::recursive_mutex mutex;
    std::vector<Hook> alive;
    std::uint64_t next_id = 0;
};

/// Built on first use: a FatalHook of static duration may come first.
Hooks& hooks() {
    static Hooks instance;
    return instance;
}

}  // namespace

void flush_output() {
    // std::cout buffers on its own once sync_with_stdio(false) is set.
    std::cout.flush();
    std::clog.flush();
    std::fflush(nullptr);
}

std::uint64_t Context::enter(std::string message) {
    static std::atomic<std::uint64_t> next_id = 0;
    auto id = next_id.fetch_add(1, std::memory_order_relaxed);
    contexts().push_back({id, std::move(message)});
    return id;
}

// Not necessarily the innermost: coroutines interleave their contexts.
Context::~Context() {
    std::erase_if(contexts(), [this](const ContextEntry& entry) { return entry.id == id; });
}

FatalHook::FatalHook(function<void()> hook) {
    auto& registry = hooks();
    std::lock_guard lock(registry.mutex);
    id = registry.next_id++;
    registry.alive.push_back({.id = id, .run = std::move(hook)});
}

// Gone already if it ran, or if the process is ending on another thread,
// which then never lets go of the mutex.
FatalHook::~FatalHook() {
    auto& registry = hooks();
    std::lock_guard lock(registry.mutex);
    std::erase_if(registry.alive, [this](const Hook& hook) { return hook.id == id; });
}

namespace detail {

void report_failure(std::string_view expression,
                    std::initializer_list<ReportLine> lines,
                    std::source_location location) {
    std::println("[ expect ] {}", expression);
    for(const auto& line: lines) {
        print_line(line.label, line.text);
    }
    print_contexts();
    print_line("at", std::format("{}:{}", location.file_name(), location.line()));
    print_trace(location);
    failure();
}

void end_fatally() {
    thread_local bool ending_here = false;
    static std::atomic<bool> ending = false;
    // A hook's own ZASSERT: the runner knows already.
    if(ending_here) {
        flush_output();
        std::_Exit(fatal_exit_code);
    }
    if(ending.exchange(true)) {
        // Another thread ends the process, and this one with it.
        while(true) {
            std::this_thread::sleep_for(std::chrono::hours(1));
        }
    }
    ending_here = true;
    if(fatal_notice != nullptr) {
        fatal_notice();
    }

    auto& registry = hooks();
    registry.mutex.lock();
    while(!registry.alive.empty()) {
        auto hook = std::move(registry.alive.back());
        registry.alive.pop_back();
#ifdef __cpp_exceptions
        // What a hook throws is printed, and the next one runs.
        trace_exception(std::move(hook.run));
#else
        hook.run();
#endif
    }
    flush_output();
    std::_Exit(fatal_exit_code);
}

}  // namespace detail

}  // namespace kota::zest
