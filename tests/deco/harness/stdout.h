#pragma once

#include <iostream>
#include <sstream>
#include <string>

#include "kota/support/scope_exit.h"

namespace kota::test {

/// What `body` prints to std::cout while it runs. std::cout is given back its
/// own buffer however `body` ends.
template <typename Body>
std::string printed_by(Body&& body) {
    std::ostringstream out;
    ScopeExit restore([previous = std::cout.rdbuf(out.rdbuf())] { std::cout.rdbuf(previous); });
    body();
    return out.str();
}

}  // namespace kota::test
