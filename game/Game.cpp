#include "Game.h"
#include "engine/core/Engine.h"
#include "engine/graphics/Renderer.h"
#include "engine/physics/Physics.h"
#include "engine/input/InputManager.h"
#include "game/player/Player.h"
#include "game/enemies/Enemy.h"
#include <memory> // Add this for smart pointers
#include <iostream>

Game::Game() : player(nullptr), enemies() {}

void Game::initialize() {
    // Initialize engine components
    engine.initialize();
    renderer.initialize();
    physics.initialize();
    inputManager.initialize();

    // Initialize player
    player = std::make_unique<Player>();
    player->initialize();

    // Initialize enemies
    for (int i = 0; i < 5; ++i) {
        auto enemy = std::make_unique<Enemy>();
        enemy->initialize();
        enemies.push_back(std::move(enemy));
    }

    std::cout << "Game initialized\n";
}

void Game::update() {
    // Update input
    inputManager.update();

    // Update player
    if (player) {
        player->update();
    }

    // Update enemies
    for (auto& enemy : enemies) {
        enemy->update();
    }

    // Update physics
    physics.update();

    std::cout << "Game updating\n";
}

void Game::render() {
    // Clear the screen
    renderer.clear();

    // Render player
    if (player) {
        player->render();
    }

    // Render enemies
    for (auto& enemy : enemies) {
        enemy->render();
    }

    // Present the rendered frame
    renderer.present();

    std::cout << "Game rendering\n";
}

void Game::shutdown() {
    // Cleanup player
    if (player) {
        player->shutdown();
        player.reset();
    }

    // Cleanup enemies
    for (auto& enemy : enemies) {
        enemy->shutdown();
    }
    enemies.clear();

    // Cleanup engine components
    renderer.cleanup();
    physics.cleanup();
    inputManager.cleanup();
    engine.shutdown();

    std::cout << "Game shutdown\n";
}