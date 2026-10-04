#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace kota::test {

/// What `body` prints to std::cout while it runs. std::cout is given back its
/// own buffer however `body` ends.
template <typename Body>
std::string printed_by(Body&& body) {
    struct Capture {
        std::ostringstream out;
        std::streambuf* previous = std::cout.rdbuf(out.rdbuf());

        ~Capture() {
            std::cout.rdbuf(previous);
        }
    } capture;

    body();
    return capture.out.str();
}

}  // namespace kota::test
