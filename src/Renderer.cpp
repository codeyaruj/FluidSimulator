#include "Renderer.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
 * - DIVERGENCE/VORTICITY: Blue (negative) → Black → Red (positive)
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
    } else if (renderMode == 1 || renderMode == 2) {
        // SIGNED FIELDS: Blue (negative) → Black (zero) → Red (positive)
        float magnitude = abs(value);
        if (value < 0.0) {
            color = vec3(0.0, 0.0, magnitude);
        } else {
            color = vec3(magnitude, 0.0, 0.0);
        }
    } else {
        color = vec3(value, value, value);
    }
    
    FragColor = vec4(color, 1.0);
}
)";

namespace {

void throwOnOpenGLError(const char* operation) {
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::ostringstream message;
        message << operation << " failed with OpenGL error 0x"
                << std::hex << error;
        throw std::runtime_error(message.str());
    }
}

} // namespace

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

Renderer::Renderer(int size)
    : shaderProgram(0), VAO(0), VBO(0), texture(0), gridSize(size),
      currentMode(RenderMode::DENSITY) {
    checkedGridElementCount(gridSize);
    try {
        initShaders();
        initBuffers();
        initTexture();
    } catch (...) {
        releaseResources();
        throw;
    }
}

Renderer::~Renderer() {
    releaseResources();
}

void Renderer::releaseResources() noexcept {
    if (texture != 0) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
    if (VBO != 0) {
        glDeleteBuffers(1, &VBO);
        VBO = 0;
    }
    if (VAO != 0) {
        glDeleteVertexArrays(1, &VAO);
        VAO = 0;
    }
    if (shaderProgram != 0) {
        glDeleteProgram(shaderProgram);
        shaderProgram = 0;
    }
}

// ============================================================================
// SHADER COMPILATION
// ============================================================================

GLuint Renderer::compileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        throw std::runtime_error("OpenGL failed to create a shader object");
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        std::string detail;
        try {
            GLint logLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> log(
                static_cast<std::size_t>(std::max(1, logLength)), '\0');
            GLsizei written = 0;
            glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()),
                               &written, log.data());
            detail.assign(log.data(),
                          static_cast<std::size_t>(std::max(0, written)));
        } catch (...) {
            glDeleteShader(shader);
            throw;
        }
        glDeleteShader(shader);
        throw std::runtime_error("Shader compilation failed:\n" + detail);
    }
    
    return shader;
}

GLuint Renderer::createShaderProgram(const char* vertexSrc, const char* fragmentSrc) {
    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    GLuint program = 0;

    try {
        vertexShader = compileShader(vertexSrc, GL_VERTEX_SHADER);
        fragmentShader = compileShader(fragmentSrc, GL_FRAGMENT_SHADER);

        program = glCreateProgram();
        if (program == 0) {
            throw std::runtime_error("OpenGL failed to create a shader program");
        }

        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint success = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
            std::vector<char> log(static_cast<std::size_t>(std::max(1, logLength)), '\0');
            GLsizei written = 0;
            glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()),
                                &written, log.data());
            const std::string detail(
                log.data(), static_cast<std::size_t>(std::max(0, written)));
            throw std::runtime_error("Shader program linking failed:\n" + detail);
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return program;
    } catch (...) {
        if (program != 0) {
            glDeleteProgram(program);
        }
        if (fragmentShader != 0) {
            glDeleteShader(fragmentShader);
        }
        if (vertexShader != 0) {
            glDeleteShader(vertexShader);
        }
        throw;
    }
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
    if (VAO == 0 || VBO == 0) {
        throw std::runtime_error("OpenGL failed to allocate renderer buffers");
    }
    
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
    throwOnOpenGLError("Renderer buffer initialization");
}

void Renderer::initTexture() {
    glGenTextures(1, &texture);
    if (texture == 0) {
        throw std::runtime_error("OpenGL failed to allocate the scalar-field texture");
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Create empty texture (GL_R32F = single channel 32-bit float)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, gridSize, gridSize, 0, GL_RED, GL_FLOAT, nullptr);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    throwOnOpenGLError("Renderer texture initialization");
}

// ============================================================================
// TEXTURE UPDATE
// ============================================================================

void Renderer::updateTexture(const std::vector<float>& data, TextureFieldKind kind) {
    // Preparation validates the exact buffer size before any OpenGL call.
    const std::vector<float> prepared = prepareTextureData(data, gridSize, kind);

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gridSize, gridSize,
                    GL_RED, GL_FLOAT, prepared.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    throwOnOpenGLError("Scalar-field texture upload");
}

// ============================================================================
// MAIN DRAW FUNCTION
// ============================================================================

void Renderer::draw(const FluidSim& fluid) {
    // Get appropriate data based on render mode
    const std::vector<float>* data = nullptr;
    TextureFieldKind kind = TextureFieldKind::DENSITY;
    
    switch (currentMode) {
        case RenderMode::DENSITY:
            data = &fluid.getDensity();
            break;
        case RenderMode::DIVERGENCE:
            data = &fluid.getDivergence();
            kind = TextureFieldKind::SIGNED;
            break;
        case RenderMode::VORTICITY:
            data = &fluid.getVorticity();
            kind = TextureFieldKind::SIGNED;
            break;
    }
    
    if (!data) return;
    
    updateTexture(*data, kind);
    
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
