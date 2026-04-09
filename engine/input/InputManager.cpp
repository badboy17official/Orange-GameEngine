#include "InputManager.h"
#include <GLFW/glfw3.h>

InputManager::InputManager()
    : window(nullptr), keys{} {}

void InputManager::initialize() {
    if (!glfwInit()) {
        // Handle error
        return;
    }
    window = glfwCreateWindow(640, 480, "Prototype Window", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        // Handle error
        return;
    }
    glfwMakeContextCurrent(window);
}

void InputManager::update() {
    for (int i = 0; i < keys.size(); ++i) {
        keys[i] = glfwGetKey(window, i) == GLFW_PRESS;
    }
}

bool InputManager::isKeyPressed(int key) {
    return keys[key];
}

void InputManager::shutdown() {
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}