#include "Player.h"
#include "InputManager.h"
#include "Game.h"

Player::Player() : health(100), position(0.0f, 0.0f, 0.0f) {}

void Player::move(float deltaX, float deltaY) {
    position.x += deltaX;
    position.y += deltaY;
}

void Player::shoot() {
    // Implement shooting logic
}

void Player::takeDamage(int damage) {
    health -= damage;
    if (health < 0) {
        health = 0;
        // Handle player death
    }
}

void Player::update(InputManager& inputManager) {
    if (inputManager.isKeyPressed(KEY_W)) {
        move(0.0f, 1.0f); // Move up
    }
    if (inputManager.isKeyPressed(KEY_S)) {
        move(0.0f, -1.0f); // Move down
    }
    if (inputManager.isKeyPressed(KEY_A)) {
        move(-1.0f, 0.0f); // Move left
    }
    if (inputManager.isKeyPressed(KEY_D)) {
        move(1.0f, 0.0f); // Move right
    }
    if (inputManager.isKeyPressed(KEY_SPACE)) {
        shoot(); // Shoot
    }
}