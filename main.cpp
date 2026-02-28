#include <iostream>
#include "Zpusher.h"

int main() {
    std::cout << "[BEGIN] main" << std::endl;
    //HP Wide Vision HD Camera
    //Integrated Camera
    //LRCP 500W
    ZPusher zpusher("video=LRCP 500W");
    std::cin.get();
    return 0;
}
