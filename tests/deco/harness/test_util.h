#pragma once

#include <expected>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "kota/deco/option.h"

namespace kota::option::test {

inline std::vector<std::string> split2vec(std::string_view str) {
    auto views = std::views::split(str, ' ') | std::views::transform([](auto&& rng) {
                     return std::string(rng.begin(), rng.end());
                 });
    return std::vector<std::string>(views.begin(), views.end());
}

struct ParseCapture {
    std::vector<std::string> argv;
    std::vector<ParsedArg> args;
    std::vector<ParseError> errors;
};

inline ParseCapture parse_all(const OptTable& table,
                              std::vector<std::string> argv,
                              ParseOptions options = {}) {
    ParseCapture capture;
    capture.argv = std::move(argv);
    for(auto& result: table.parse(capture.argv, options)) {
        if(result.has_value()) {
            capture.args.push_back(*result);
        } else {
            capture.errors.push_back(result.error());
        }
    }
    return capture;
}

struct ParsedArgs {
    std::vector<std::string> argv_storage;
    std::vector<ParsedArg> parsed;

    std::size_t size() const {
        return parsed.size();
    }

    bool empty() const {
        return parsed.empty();
    }

    ParsedArg& operator[](std::size_t index) {
        return parsed[index];
    }

    const ParsedArg& operator[](std::size_t index) const {
        return parsed[index];
    }

    auto begin() {
        return parsed.begin();
    }

    auto end() {
        return parsed.end();
    }

    auto begin() const {
        return parsed.begin();
    }

    auto end() const {
        return parsed.end();
    }
};

template <typename BuiltTy>
std::expected<ParsedArgs, std::string> parse_with(const BuiltTy& built,
                                                  std::vector<std::string> argv) {
    auto table = built.make_opt_table();
    auto opts = built.make_parse_options();
    ParsedArgs args;
    args.argv_storage = std::move(argv);
    for(auto& result: table.parse(args.argv_storage, opts)) {
        if(!result.has_value()) {
            return std::unexpected(std::string(result.error().message));
        }
        args.parsed.push_back(*result);
    }
    return args;
}

}  // namespace kota::option::test
