#include <iostream>
#include <string>
#include "Zpusher.h"

int main(int argc, char* argv[]) {
    std::string config_path = "config.json";
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[i + 1];
            i++;
        }
    }
    std::cout << "[Config] Using config: " << config_path << std::endl;
    ZPusher zpusher;
    zpusher.Init(config_path);
    zpusher.start_Push();
    std::cin.get();
    return 0;
}
