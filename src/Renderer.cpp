#include "Renderer.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>

// ============================================================================
// SHADER SOURCES
// ============================================================================

/**
 * Vertex shader for full-screen quad rendering.
 * Passes texture coordinates to fragment shader.
 */
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

/**
 * Fragment shader - visualizes scalar field as color.
 * 
 * Different color mappings for different visualization modes:
 * - DENSITY: Black → Blue → Cyan → Yellow → White
 * - DIVERGENCE: Black (zero) → White (divergence)
 * - VORTICITY: Blue (clockwise) → Black → Red (counter-clockwise)
 */
const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D densityTexture;
uniform int renderMode;  // 0=density, 1=divergence, 2=vorticity

void main() {
    float value = texture(densityTexture, TexCoord).r;
    vec3 color;
    
    if (renderMode == 0) {
        // DENSITY: Black → Blue → Cyan → Yellow → White
        if (value < 0.2) {
            color = vec3(0.0, 0.0, value * 5.0);
        } else if (value < 0.5) {
            float t = (value - 0.2) / 0.3;
            color = vec3(0.0, t, 1.0);
        } else if (value < 0.8) {
            float t = (value - 0.5) / 0.3;
            color = vec3(t, 1.0, 1.0 - t);
        } else {
            float t = (value - 0.8) / 0.2;
            color = vec3(1.0, 1.0, t);
        }
    } else if (renderMode == 1) {
        // DIVERGENCE: Black → White (grayscale)
        float absDiv = abs(value);
        color = vec3(absDiv, absDiv, absDiv);
    } else if (renderMode == 2) {
        // VORTICITY: Blue (clockwise) → Black → Red (counter-clockwise)
        if (value < 0.0) {
            color = vec3(0.0, 0.0, -value);
        } else {
            color = vec3(value, 0.0, 0.0);
        }
    } else {
        color = vec3(value, value, value);
    }
    
    FragColor = vec4(color, 1.0);
}
)";

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

Renderer::Renderer(int size, int windowW, int windowH)
    : gridSize(size), currentMode(RenderMode::DENSITY), 
      windowWidth(windowW), windowHeight(windowH) {
    initShaders();
    initBuffers();
    initTexture();
}

Renderer::~Renderer() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteTextures(1, &texture);
    glDeleteProgram(shaderProgram);
}

// ============================================================================
// SHADER COMPILATION
// ============================================================================

GLuint Renderer::compileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation failed:\n" << infoLog << std::endl;
    }
    
    return shader;
}

GLuint Renderer::createShaderProgram(const char* vertexSrc, const char* fragmentSrc) {
    GLuint vertexShader = compileShader(vertexSrc, GL_VERTEX_SHADER);
    GLuint fragmentShader = compileShader(fragmentSrc, GL_FRAGMENT_SHADER);
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        std::cerr << "Shader program linking failed:\n" << infoLog << std::endl;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    return program;
}

void Renderer::initShaders() {
    shaderProgram = createShaderProgram(vertexShaderSource, fragmentShaderSource);
}

// ============================================================================
// BUFFER INITIALIZATION
// ============================================================================

void Renderer::initBuffers() {
    // Full-screen quad vertices (position + texture coord)
    float vertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,  // top-left
        -1.0f, -1.0f,  0.0f, 0.0f,  // bottom-left
         1.0f, -1.0f,  1.0f, 0.0f,  // bottom-right
        
        -1.0f,  1.0f,  0.0f, 1.0f,  // top-left
         1.0f, -1.0f,  1.0f, 0.0f,  // bottom-right
         1.0f,  1.0f,  1.0f, 1.0f   // top-right
    };
    
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    
    glBindVertexArray(VAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // Position attribute (location = 0)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Texture coord attribute (location = 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer::initTexture() {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Create empty texture (GL_R32F = single channel 32-bit float)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, gridSize, gridSize, 0, GL_RED, GL_FLOAT, nullptr);
    
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ============================================================================
// TEXTURE UPDATE
// ============================================================================

void Renderer::updateTexture(const std::vector<float>& data, float minVal, float maxVal) {
    // Normalize data to [0, 1] range for visualization
    std::vector<float> normalized(data.size());
    float range = maxVal - minVal;
    if (range < 0.0001f) range = 1.0f;  // Avoid division by zero
    
    for (size_t i = 0; i < data.size(); i++) {
        normalized[i] = (data[i] - minVal) / range;
        // Clamp to [0, 1]
        normalized[i] = std::max(0.0f, std::min(1.0f, normalized[i]));
    }
    
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gridSize, gridSize, GL_RED, GL_FLOAT, normalized.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ============================================================================
// MAIN DRAW FUNCTION
// ============================================================================

void Renderer::draw(const FluidSim& fluid) {
    // Get appropriate data based on render mode
    const std::vector<float>* data = nullptr;
    float minVal = 0.0f, maxVal = 1.0f;
    
    switch (currentMode) {
        case RenderMode::DENSITY:
            data = &fluid.getDensity();
            minVal = 0.0f;
            maxVal = 1.0f;
            break;
        case RenderMode::DIVERGENCE:
            data = &fluid.getDivergence();
            maxVal = *std::max_element(data->begin(), data->end());
            maxVal = std::max(0.001f, std::abs(maxVal));
            minVal = -maxVal;
            break;
        case RenderMode::VORTICITY:
            data = &fluid.getVorticity();
            maxVal = *std::max_element(data->begin(), data->end());
            maxVal = std::max(0.001f, std::abs(maxVal));
            minVal = -maxVal;
            break;
    }
    
    if (!data) return;
    
    // Update texture with normalized data
    updateTexture(*data, minVal, maxVal);
    
    // Clear screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Use shader and set uniforms
    glUseProgram(shaderProgram);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    glUniform1i(glGetUniformLocation(shaderProgram, "densityTexture"), 0);
    glUniform1i(glGetUniformLocation(shaderProgram, "renderMode"), static_cast<int>(currentMode));
    
    // Draw full-screen quad
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void Renderer::nextRenderMode() {
    int mode = static_cast<int>(currentMode);
    mode = (mode + 1) % 3;  // Cycle through 3 modes
    currentMode = static_cast<RenderMode>(mode);
}
