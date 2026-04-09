// This file declares the Game class, which serves as the main game logic handler.

#pragma once

#include <memory>
#include <vector>
#include "engine/graphics/Renderer.h"

class Game {
public:
    Game();
    ~Game();

    void initialize();
    void update();
    void render();
    void shutdown();

private:
    class Player;
    class Enemy;

    std::unique_ptr<Player> player;
    std::vector<std::unique_ptr<Enemy>> enemies;
};