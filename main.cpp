#include <iostream>
#include "Zpusher.h"

int main() {
    std::cout << "[BEGIN] main" << std::endl;
    ZPusher zpusher;
    zpusher.Init();
    zpusher.start_Push();
    std::cin.get();
    return 0;
}
