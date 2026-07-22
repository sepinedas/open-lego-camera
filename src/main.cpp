#include <iostream>

#include "app.hpp"
#include "config.hpp"

int main(int argc, char** argv) {
    olc::Config cfg;
    int exitCode = 0;
    if (!olc::parseArgs(argc, argv, cfg, &exitCode))
        return exitCode;

    olc::App app;
    if (!app.init(cfg))
        return 1;

    return app.run();
}
