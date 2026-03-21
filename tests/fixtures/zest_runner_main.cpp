#include "eventide/zest/zest.h"

int main(int argc, char** argv) {
    static_cast<void>(
        eventide::zest::run_cli(argc,
                                argv,
                                "zest_runner_fixture [options] Run zest runner fixture"));
    return 0;
}
