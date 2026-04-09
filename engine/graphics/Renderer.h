// This file declares the Renderer class, which handles rendering graphics to the screen.

#pragma once

class Renderer {
public:
    void initialize();
    void render();
    void clear();
    void present();
    void cleanup();
};