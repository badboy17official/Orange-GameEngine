#include "game/Game.h"
#include <iostream>

int main() {
    Game game;
    game.initialize();

    for (int i = 0; i < 100; ++i) {
        game.update();
        game.render();
    }

    game.shutdown();
    std::cout << "Prototype ran successfully.\n";
    return 0;
}