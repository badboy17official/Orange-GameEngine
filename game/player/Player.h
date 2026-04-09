#pragma once
#include "engine/input/InputManager.h"

struct Vec3 {
    float x, y, z;
    Vec3(float x_=0, float y_=0, float z_=0) : x(x_), y(y_), z(z_) {}
};

class Player {
public:
    Player();
    void move(float deltaX, float deltaY);
    void update(InputManager& inputManager);
    int getHealth() const;

private:
    int health;
    Vec3 position;
};