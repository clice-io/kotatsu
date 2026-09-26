#include "kota/zest/assert/check.h"

#include <atomic>
#include <print>
#include <string_view>
#include <vector>

#include "kota/zest/assert/trace.h"
#include "kota/zest/runner/registry.h"

namespace kota::zest {

namespace {

struct Entry {
    std::uint64_t id;
    std::string message;
};

/// Contexts entered on this thread and not yet ended, outermost first. It
/// holds their messages rather than the contexts: one that ends on another
/// thread, as a coroutine's may, is left here instead of dangling.
std::vector<Entry>& contexts() {
    thread_local std::vector<Entry> stack;
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

}  // namespace

std::uint64_t Context::enter(std::string message) {
    static std::atomic<std::uint64_t> next_id = 0;
    auto id = next_id.fetch_add(1, std::memory_order_relaxed);
    contexts().push_back({id, std::move(message)});
    return id;
}

// Not necessarily the innermost: coroutines interleave their contexts.
Context::~Context() {
    std::erase_if(contexts(), [this](const Entry& entry) { return entry.id == id; });
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

void fail_reported(std::source_location location) {
    print_contexts();
    print_trace(location);
    failure();
}

#ifdef __cpp_exceptions

void check_throws(function<void()> body,
                  std::string_view expression,
                  bool expect_throw,
                  std::source_location location) {
    // An unexpected exception is printed where it is caught.
    bool threw = trace_exception(std::move(body), !expect_throw);
    if(threw != expect_throw) {
        report_failure(expression,
                       {
                           {"", expect_throw ? "expected to throw" : "expected not to throw"}
        },
                       location);
    }
}

#endif

}  // namespace detail

}  // namespace kota::zest
