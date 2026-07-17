#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

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

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D densityTexture;
uniform int renderMode;

void main() {
    float value = texture(densityTexture, TexCoord).r;
    vec3 color;

    if (renderMode == 0) {
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

std::size_t getTextureElementCount(int gridSize) {
    if (gridSize <= 0) {
        throw std::invalid_argument("Texture grid size must be positive");
    }

    std::size_t size = static_cast<std::size_t>(gridSize);

    if (size > std::numeric_limits<std::size_t>::max() / size) {
        throw std::overflow_error("Texture grid element count overflow");
    }

    return size * size;
}

}

std::vector<float> prepareTextureData(const std::vector<float>& data,
                                      int gridSize,
                                      bool signedField) {
    std::size_t expectedSize = getTextureElementCount(gridSize);

    if (data.size() != expectedSize) {
        std::ostringstream message;
        message << "Texture data size mismatch: expected " << expectedSize
                << " values, received " << data.size();
        throw std::invalid_argument(message.str());
    }

    if (!signedField) {
        return data;
    }

    float maximumMagnitude = 0.0f;

    for (std::size_t i = 0; i < data.size(); ++i) {
        float value = data[i];

        if (!std::isfinite(value)) {
            std::ostringstream message;
            message << "Texture data contains a non-finite value at index " << i;
            throw std::invalid_argument(message.str());
        }

        maximumMagnitude = std::max(maximumMagnitude, std::abs(value));
    }

    std::vector<float> normalized(data.size(), 0.0f);

    if (maximumMagnitude <= std::numeric_limits<float>::epsilon()) {
        return normalized;
    }

    for (std::size_t i = 0; i < data.size(); ++i) {
        normalized[i] = data[i] / maximumMagnitude;
    }

    return normalized;
}

Renderer::Renderer()
    : shaderProgram(0),
      VAO(0),
      VBO(0),
      texture(0),
      gridSize(0),
      currentMode(RenderMode::DENSITY) {
}

Renderer::~Renderer() {
    releaseResources();
}

bool Renderer::initialize(int size) {
    releaseResources();
    gridSize = 0;
    currentMode = RenderMode::DENSITY;
    lastError.clear();

    try {
        getTextureElementCount(size);
        gridSize = size;
        initShaders();
        initBuffers();
        initTexture();
        return true;
    } catch (const std::exception& error) {
        lastError = error.what();
    } catch (...) {
        lastError = "Unknown renderer initialization error";
    }

    releaseResources();
    gridSize = 0;
    return false;
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

GLuint Renderer::compileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);

    if (shader == 0) {
        throw std::runtime_error("OpenGL failed to create a shader object");
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    if (success == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

    std::string detail;

    try {
        std::vector<char> log(
            static_cast<std::size_t>(std::max(1, logLength)), '\0');
        GLsizei written = 0;
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()),
                           &written, log.data());
        detail.assign(
            log.data(), static_cast<std::size_t>(std::max(0, written)));
    } catch (...) {
        glDeleteShader(shader);
        throw;
    }

    glDeleteShader(shader);

    throw std::runtime_error("Shader compilation failed:\n" + detail);
}

GLuint Renderer::createShaderProgram(const char* vertexSrc,
                                     const char* fragmentSrc) {
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

        if (success != GL_TRUE) {
            GLint logLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

            std::vector<char> log(
                static_cast<std::size_t>(std::max(1, logLength)), '\0');
            GLsizei written = 0;
            glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()),
                                &written, log.data());

            std::string detail(
                log.data(), static_cast<std::size_t>(std::max(0, written)));
            throw std::runtime_error(
                "Shader program linking failed:\n" + detail);
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
    shaderProgram = createShaderProgram(
        vertexShaderSource, fragmentShaderSource);
}

void Renderer::initBuffers() {
    float vertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    if (VAO == 0 || VBO == 0) {
        throw std::runtime_error("OpenGL failed to allocate renderer buffers");
    }

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer::initTexture() {
    glGenTextures(1, &texture);

    if (texture == 0) {
        throw std::runtime_error(
            "OpenGL failed to allocate the scalar-field texture");
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, gridSize, gridSize, 0,
                 GL_RED, GL_FLOAT, nullptr);

    GLenum error = glGetError();
    glBindTexture(GL_TEXTURE_2D, 0);

    if (error != GL_NO_ERROR) {
        std::ostringstream message;
        message << "OpenGL failed to create the scalar-field texture: error 0x"
                << std::hex << error;
        throw std::runtime_error(message.str());
    }
}

void Renderer::updateTexture(const std::vector<float>& data,
                             bool signedField) {
    std::vector<float> prepared =
        prepareTextureData(data, gridSize, signedField);

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gridSize, gridSize,
                    GL_RED, GL_FLOAT, prepared.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::draw(const FluidSim& fluid) {
    const std::vector<float>* data = nullptr;
    bool signedField = false;

    switch (currentMode) {
        case RenderMode::DENSITY:
            data = &fluid.getDensity();
            break;

        case RenderMode::DIVERGENCE:
            data = &fluid.getDivergence();
            signedField = true;
            break;

        case RenderMode::VORTICITY:
            data = &fluid.getVorticity();
            signedField = true;
            break;
    }

    if (data == nullptr) {
        return;
    }

    updateTexture(*data, signedField);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(shaderProgram, "densityTexture"), 0);
    glUniform1i(
        glGetUniformLocation(shaderProgram, "renderMode"),
        static_cast<int>(currentMode));

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void Renderer::nextRenderMode() {
    int mode = static_cast<int>(currentMode);
    mode = (mode + 1) % 3;
    currentMode = static_cast<RenderMode>(mode);
}
