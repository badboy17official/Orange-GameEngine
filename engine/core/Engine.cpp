#include "engine/graphics/Renderer.h"
#include "Engine.h"
#include "InputManager.h"
#include "Physics.h"
#include "Profiler.h"

Engine::Engine() : isRunning(false) {}

void Engine::start() {
    isRunning = true;
    initializeSystems();
    gameLoop();
}

void Engine::initializeSystems() {
    renderer.initialize();
    inputManager.initialize();
    physics.initialize();
    profiler.startProfile("Engine Initialization");
}

void Engine::gameLoop() {
    while (isRunning) {
        profiler.startProfile("Frame Update");
        update();
        render();
        profiler.stopProfile("Frame Update");
    }
}

void Engine::update() {
    inputManager.update();
    physics.update(0.016f); // Pass a dummy deltaTime for now
    // Update game logic here
}

void Engine::render() {
    renderer.render();
}

void Engine::shutdown() {
    isRunning = false;
    renderer.cleanup();
    // Cleanup other systems
}