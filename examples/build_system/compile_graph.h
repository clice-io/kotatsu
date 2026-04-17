#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "kota/async/async.h"

namespace kota {

struct CompileUnit {
    std::string name;
    std::vector<std::string> dependencies;
    std::vector<std::string> dependents;
    bool dirty = true;
    bool compiling = false;
    std::unique_ptr<cancellation_source> source = std::make_unique<cancellation_source>();
    std::unique_ptr<event> completion;
};

class CompileGraph {
public:
    using delay_fn = std::function<std::chrono::milliseconds()>;

    explicit CompileGraph(delay_fn delay = [] { return std::chrono::milliseconds{10}; }) :
        compile_delay_(std::move(delay)) {}

    void add_unit(const std::string& name, std::vector<std::string> deps = {}) {
        auto& unit = units_[name];
        unit.name = name;
        unit.dependencies = std::move(deps);

        for(auto& dep: unit.dependencies) {
            if(units_.find(dep) == units_.end()) {
                units_[dep].name = dep;
            }
            units_[dep].dependents.push_back(name);
        }
    }

    task<bool> compile(const std::string& name, event_loop& loop) {
        co_return co_await compile_impl(name, loop);
    }

    void update(const std::string& name) {
        auto it = units_.find(name);
        if(it == units_.end()) {
            return;
        }

        auto& unit = it->second;
        unit.source->cancel();
        unit.source = std::make_unique<cancellation_source>();
        unit.dirty = true;

        for(auto& dep: unit.dependents) {
            update(dep);
        }
    }

private:
    task<bool> compile_impl(const std::string& name, event_loop& loop) {
        auto it = units_.find(name);
        if(it == units_.end()) {
            co_return false;
        }

        auto& unit = it->second;

        // Already compiled and not dirty
        if(!unit.dirty) {
            co_return true;
        }

        // Another task is already compiling this unit — wait for it
        // instead of starting a redundant compilation.
        if(unit.compiling) {
            co_await unit.completion->wait();
            co_return !unit.dirty;
        }

        unit.compiling = true;
        unit.completion = std::make_unique<event>();

        // Compile dependencies first, each cancellable via this unit's token
        for(auto& dep_name: unit.dependencies) {
            auto dep_result =
                co_await with_token(compile_impl(dep_name, loop), unit.source->token());
            if(!dep_result.has_value() || !*dep_result) {
                unit.compiling = false;
                unit.completion->set();
                co_await cancel();
            }
        }

        // Simulate compilation work, cancellable via the unit's token
        auto work = [&]() -> task<bool> {
            co_await sleep(compile_delay_(), loop);
            co_return true;
        };

        auto result = co_await with_token(work(), unit.source->token());
        if(!result.has_value()) {
            unit.compiling = false;
            unit.completion->set();
            co_await cancel();
        }

        unit.dirty = false;
        unit.compiling = false;
        unit.completion->set();
        co_return true;
    }

    std::map<std::string, CompileUnit> units_;
    delay_fn compile_delay_;
};

}  // namespace kota
