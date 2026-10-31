#pragma once

#include <iostream>
#include <sstream>
#include <string>

#include "kotatsu/support/scope_exit.h"

namespace kotatsu::test {

/// What `body` prints to std::cout while it runs. std::cout is given back its
/// own buffer however `body` ends.
template <typename Body>
std::string printed_by(Body&& body) {
    std::ostringstream out;
    std::streambuf* previous = std::cout.rdbuf(out.rdbuf());
    ScopeExit restore([previous] { std::cout.rdbuf(previous); });
    body();
    return out.str();
}

}  // namespace kotatsu::test
