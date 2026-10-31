#pragma once

#include <string>

namespace kotatsu::async {

/// An IP address, in its numeric text form, and a port.
struct endpoint {
    std::string addr;
    int port = 0;
};

}  // namespace kotatsu::async
