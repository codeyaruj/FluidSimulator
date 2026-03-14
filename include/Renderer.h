#ifndef RENDERER_H
#define RENDERER_H

#include <GL/glew.h>
#include <vector>
#include "FluidSim.h"

/**
 * Visualization modes for inspecting simulation internals.
 * Each mode renders a different physical quantity.
 */
enum class RenderMode {
    DENSITY,      // 1: Standard density field (dye visualization)
    DIVERGENCE,   // 2: Divergence field (should be near zero)
    VORTICITY     // 3: Vorticity/curl (rotation detection)
};

/**
 * Renderer handles all visualization of the fluid simulation.
 * 
 * Features:
 * - Multiple visualization modes for physics debugging
 * - Heatmap-style color mapping for scalar fields
 */
class Renderer {
private:
    GLuint shaderProgram;
    GLuint VAO, VBO;
    GLuint texture;
    int gridSize;
    
    // Current render mode
    RenderMode currentMode;
    
    // Window dimensions for coordinate mapping
    int windowWidth;
    int windowHeight;
    
    // Shader compilation helper
    GLuint compileShader(const char* source, GLenum type);
    GLuint createShaderProgram(const char* vertexSrc, const char* fragmentSrc);
    
    // Initialize OpenGL resources
    void initBuffers();
    void initTexture();
    void initShaders();

public:
    Renderer(int size, int windowW, int windowH);
    ~Renderer();
    
    /**
     * Main draw function - renders fluid based on current mode.
     * @param fluid Reference to the fluid simulation
     */
    void draw(const FluidSim& fluid);
    
    /**
     * Update texture with scalar field data.
     * Handles different data ranges via min/max normalization.
     */
    void updateTexture(const std::vector<float>& data, float minVal = 0.0f, float maxVal = 1.0f);
    
    /**
     * Set the visualization mode.
     */
    void setRenderMode(RenderMode mode) { currentMode = mode; }
    RenderMode getRenderMode() const { return currentMode; }
    
    /**
     * Cycle to next render mode.
     */
    void nextRenderMode();
    
    /**
     * Update window dimensions (e.g., on resize).
     */
    void setWindowSize(int w, int h) { windowWidth = w; windowHeight = h; }
};

#endif // RENDERER_H
