// This file declares the Engine class, which is responsible for initializing and managing the game engine's core functionalities.

#pragma once

#include "engine/graphics/Renderer.h"
#include "engine/input/InputManager.h"
#include "engine/physics/Physics.h"
#include "engine/core/Profiler.h"

class Engine {
public:
    Engine();

    void start();
    void update();
    void render();
    void shutdown();

private:
    bool isRunning;
    Renderer renderer;
    InputManager inputManager;
    Physics physics;
    Profiler profiler;

    void initializeSystems();
    void gameLoop();
    void updatePhysics();
};