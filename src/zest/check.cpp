#include "kota/zest/assert/check.h"

#include <cassert>
#include <print>
#include <string_view>
#include <vector>

#include "kota/zest/assert/trace.h"
#include "kota/zest/runner/registry.h"

namespace kota::zest {

namespace {

/// Contexts alive on this thread, outermost first.
std::vector<const Context*>& contexts() {
    thread_local std::vector<const Context*> stack;
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

}  // namespace

Context::Context(std::string message) : text(std::move(message)) {
    contexts().push_back(this);
}

Context::~Context() {
    assert(!contexts().empty() && contexts().back() == this);
    contexts().pop_back();
}

void report_failure(std::string_view expression,
                    std::initializer_list<ReportLine> lines,
                    std::source_location location) {
    std::println("[ expect ] {}", expression);
    for(const auto& line: lines) {
        print_line(line.label, line.text);
    }
    for(const auto* context: contexts()) {
        print_line("context", context->message());
    }
    print_line("at", std::format("{}:{}", location.file_name(), location.line()));
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

}  // namespace kota::zest
