#ifndef RENDERER_H
#define RENDERER_H

#include <GL/glew.h>
#include "FluidSim.h"

#include <string>
#include <vector>

enum class RenderMode {
    DENSITY,
    DIVERGENCE,
    VORTICITY
};

std::vector<float> prepareTextureData(const std::vector<float>& data,
                                      int gridSize,
                                      bool signedField);

class Renderer {
private:
    GLuint shaderProgram;
    GLuint VAO;
    GLuint VBO;
    GLuint texture;
    int gridSize;
    RenderMode currentMode;
    std::string lastError;

    GLuint compileShader(const char* source, GLenum type);
    GLuint createShaderProgram(const char* vertexSrc, const char* fragmentSrc);
    void initBuffers();
    void initTexture();
    void initShaders();
    void releaseResources() noexcept;
    void updateTexture(const std::vector<float>& data, bool signedField);

public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    bool initialize(int size);
    const std::string& getLastError() const { return lastError; }

    void draw(const FluidSim& fluid);
    void setRenderMode(RenderMode mode) { currentMode = mode; }
    RenderMode getRenderMode() const { return currentMode; }
    void nextRenderMode();
};

#endif
