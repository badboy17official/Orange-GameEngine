#include "Enemy.h"

Enemy::Enemy() {
    // Initialize enemy attributes
    health = 100;
    damage = 10;
    speed = 2.0f;
}

void Enemy::move(float deltaTime) {
    // Implement enemy movement logic
}

void Enemy::attack(Player& player) {
    // Implement attack logic on the player
    player.takeDamage(damage);
}

void Enemy::takeDamage(int amount) {
    health -= amount;
    if (health <= 0) {
        // Handle enemy death
    }
}

int Enemy::getHealth() const {
    return health;
}