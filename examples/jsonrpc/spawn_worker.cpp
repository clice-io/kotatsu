#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "eventide/jsonrpc/peer.h"
#include "eventide/async/loop.h"
#include "eventide/async/process.h"

namespace et = eventide;
namespace jsonrpc = et::jsonrpc;

namespace {

struct BuildParams {
    std::string worker_name;
    std::string source;
    std::string header;
    std::string include_path;
};

struct BuildResult {
    std::string worker_name;
    std::string command_line;
    std::string resolved_header;
};

struct WorkerLog {
    std::string worker_name;
    std::string text;
};

et::task<jsonrpc::Result<BuildResult>> handle_build_request(jsonrpc::RequestContext& context,
                                                            const BuildParams& params) {
    auto log_status =
        context->send_notification("worker/log",
                                   WorkerLog{
                                       .worker_name = params.worker_name,
                                       .text = "preparing compile command for " + params.source,
                                   });
    if(!log_status) {
        co_return std::unexpected(log_status.error());
    }

    co_return BuildResult{
        .worker_name = params.worker_name,
        .command_line = "clang++ -c " + params.source + " -I" + params.include_path,
        .resolved_header = params.include_path + "/" + params.header,
    };
}

struct WorkerPlan {
    std::string worker_name;
    std::string source;
    std::string header;
    std::string include_path;
};

struct WorkerOutcome {
    std::string worker_name;
    bool ok = false;
    std::string error;
};

et::task<void> run_parent_session(jsonrpc::Peer& peer,
                                  et::process child,
                                  WorkerPlan plan,
                                  WorkerOutcome& outcome) {
    outcome.worker_name = plan.worker_name;

    auto build_result =
        co_await peer.send_request<BuildResult>("worker/build",
                                                BuildParams{
                                                    .worker_name = plan.worker_name,
                                                    .source = plan.source,
                                                    .header = plan.header,
                                                    .include_path = plan.include_path,
                                                });
    if(!build_result) {
        outcome.error = "request failed: " + build_result.error().message;
    } else {
        std::println("[{}] worker command: {}",
                     build_result->worker_name,
                     build_result->command_line);
        std::println("[{}] resolved header: {}",
                     build_result->worker_name,
                     build_result->resolved_header);
    }

    auto close_status = peer.close_output();
    if(!close_status && outcome.error.empty()) {
        outcome.error = "closing worker output failed: " + close_status.error().message;
    }

    auto child_status = co_await child.wait();
    if(!child_status) {
        if(outcome.error.empty()) {
            outcome.error =
                "waiting for worker failed: " + std::string(child_status.error().message());
        }
        co_return;
    }

    if(child_status->status != 0 || child_status->term_signal != 0) {
        if(outcome.error.empty()) {
            outcome.error =
                "worker exited unexpectedly: status=" + std::to_string(child_status->status) +
                " signal=" + std::to_string(child_status->term_signal);
        }
        co_return;
    }

    outcome.ok = outcome.error.empty();
}

int run_worker() {
    et::event_loop loop;
    auto transport = jsonrpc::StreamTransport::open_stdio(loop);
    if(!transport) {
        std::println(stderr, "failed to open stdio transport: {}", transport.error());
        return 1;
    }

    jsonrpc::Peer peer(loop, std::move(*transport));

    peer.on_request("worker/build", handle_build_request);

    loop.schedule(peer.run());
    return loop.run();
}

int run_parent(std::string self_path) {
    et::event_loop loop;

    const std::vector<WorkerPlan> plans = {
        {
         .worker_name = "worker-1",
         .source = "src/main.cpp",
         .header = "vector",
         .include_path = "/opt/eventide/example/include",
         },
        {
         .worker_name = "worker-2",
         .source = "src/lib.cpp",
         .header = "string",
         .include_path = "/opt/eventide/example/include",
         },
        {
         .worker_name = "worker-3",
         .source = "src/tool.cpp",
         .header = "memory",
         .include_path = "/opt/eventide/example/include",
         },
    };

    std::vector<WorkerOutcome> outcomes(plans.size());
    std::vector<std::unique_ptr<jsonrpc::Peer>> peers;
    peers.reserve(plans.size());

    for(std::size_t index = 0; index < plans.size(); ++index) {
        et::process::options opts;
        opts.file = self_path;
        opts.args = {self_path, "--worker"};
        opts.streams = {
            et::process::stdio::pipe(true, false),
            et::process::stdio::pipe(false, true),
            et::process::stdio::inherit(),
        };

        auto spawned = et::process::spawn(opts, loop);
        if(!spawned) {
            std::println(stderr,
                         "failed to spawn {}: {}",
                         plans[index].worker_name,
                         spawned.error().message());
            return 1;
        }

        auto transport = std::make_unique<jsonrpc::StreamTransport>(std::move(spawned->stdout_pipe),
                                                                    std::move(spawned->stdin_pipe));
        auto peer = std::make_unique<jsonrpc::Peer>(loop, std::move(transport));

        peer->on_notification("worker/log", [](const WorkerLog& params) {
            std::println(stderr, "[{}] {}", params.worker_name, params.text);
        });

        loop.schedule(peer->run());
        loop.schedule(
            run_parent_session(*peer, std::move(spawned->proc), plans[index], outcomes[index]));
        peers.push_back(std::move(peer));
    }

    auto loop_status = loop.run();
    if(loop_status != 0) {
        std::println(stderr, "parent loop exited with status {}", loop_status);
        return 1;
    }

    bool all_ok = true;
    for(const auto& outcome: outcomes) {
        if(outcome.ok) {
            continue;
        }
        all_ok = false;
        std::println(stderr, "[{}] {}", outcome.worker_name, outcome.error);
    }

    return all_ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if(argc > 1 && std::string_view(argv[1]) == "--worker") {
        return run_worker();
    }

    auto self_path = std::filesystem::absolute(argv[0]).string();
    return run_parent(std::move(self_path));
}
