#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace kota::test {

/// What `body` prints to std::cout, which it prints to while it runs; the
/// result of `body` goes to `result`.
template <typename Body, typename Result>
std::string printed_by(Body&& body, Result& result) {
    std::ostringstream out;
    auto* previous = std::cout.rdbuf(out.rdbuf());
    result = body();
    std::cout.rdbuf(previous);
    return out.str();
}

}  // namespace kota::test
