#pragma once

#include <string>

namespace kota {

/// An IP address, in its numeric text form, and a port.
struct endpoint {
    std::string addr;
    int port = 0;
};

}  // namespace kota
