// InputManager.h

#pragma once

#include <GLFW/glfw3.h>
#include <array>

class InputManager {
public:
    InputManager();

    void initialize();
    void update();
    bool isKeyPressed(int key);
    void shutdown();

private:
    GLFWwindow* window;
    std::array<bool, 350> keys; // 350 is enough for most key codes
};