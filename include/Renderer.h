#ifndef RENDERER_H
#define RENDERER_H

#include <GL/glew.h>
#include <vector>
#include "FluidSim.h"
#include "SimulationUtils.h"

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
    
    // Shader compilation helper
    GLuint compileShader(const char* source, GLenum type);
    GLuint createShaderProgram(const char* vertexSrc, const char* fragmentSrc);
    
    // Initialize OpenGL resources
    void initBuffers();
    void initTexture();
    void initShaders();
    void releaseResources() noexcept;
    void updateTexture(const std::vector<float>& data, TextureFieldKind kind);

public:
    explicit Renderer(int size);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /**
     * Main draw function - renders fluid based on current mode.
     * @param fluid Reference to the fluid simulation
     */
    void draw(const FluidSim& fluid);
    
    /**
     * Set the visualization mode.
     */
    void setRenderMode(RenderMode mode) { currentMode = mode; }
    RenderMode getRenderMode() const { return currentMode; }
    
    /**
     * Cycle to next render mode.
     */
    void nextRenderMode();
    
};

#endif // RENDERER_H
