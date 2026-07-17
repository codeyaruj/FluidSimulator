#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "FluidSim.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

constexpr float MOUSE_VELOCITY_COUPLING = 5.0f;
constexpr float MAX_NORMALIZED_MOUSE_SPEED = 10.0f;

std::unique_ptr<FluidSim> fluid;
std::unique_ptr<Renderer> renderer;

int windowWidth = 800;
int windowHeight = 800;
int framebufferWidth = 800;
int framebufferHeight = 800;
bool resetRequested = false;
double lastFrameTime = 0.0;

struct MouseState {
    bool leftPressed = false;
    bool hasPosition = false;
    bool dragPending = false;
    double x = 0.0;
    double y = 0.0;
    double movementX = 0.0;
    double movementY = 0.0;
};

MouseState mouse;

bool mapWindowToGrid(double x, double y, int width, int height, int gridSize,
                     int& gridX, int& gridY) {
    gridX = -1;
    gridY = -1;

    if (width <= 0 || height <= 0 || gridSize <= 0) {
        return false;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    if (x < 0.0 || x > width || y < 0.0 || y > height) {
        return false;
    }

    double normalizedX = x / static_cast<double>(width);
    double normalizedY =
        (static_cast<double>(height) - y) / static_cast<double>(height);

    gridX = static_cast<int>(normalizedX * gridSize);
    gridY = static_cast<int>(normalizedY * gridSize);
    gridX = std::max(0, std::min(gridSize - 1, gridX));
    gridY = std::max(0, std::min(gridSize - 1, gridY));
    return true;
}

bool calculatePointerVelocityRate(double movementX, double movementY,
                                  int width, int height, double elapsedSeconds,
                                  float& velocityX, float& velocityY) {
    velocityX = 0.0f;
    velocityY = 0.0f;

    if (width <= 0 || height <= 0 || elapsedSeconds <= 0.0) {
        return false;
    }
    if (!std::isfinite(movementX) || !std::isfinite(movementY) ||
        !std::isfinite(elapsedSeconds)) {
        return false;
    }

    double normalizedX =
        movementX / static_cast<double>(width) / elapsedSeconds;
    double normalizedY =
        -movementY / static_cast<double>(height) / elapsedSeconds;
    double speed = std::hypot(normalizedX, normalizedY);

    if (!std::isfinite(speed)) {
        return false;
    }
    if (speed > MAX_NORMALIZED_MOUSE_SPEED) {
        double scale = MAX_NORMALIZED_MOUSE_SPEED / speed;
        normalizedX *= scale;
        normalizedY *= scale;
    }

    velocityX = static_cast<float>(normalizedX * MOUSE_VELOCITY_COUPLING);
    velocityY = static_cast<float>(normalizedY * MOUSE_VELOCITY_COUPLING);
    return true;
}

float clampTimestep(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return 0.0f;
    }
    return std::min(dt, MAX_SIMULATION_TIMESTEP);
}

void addDensityBrush(FluidSim& simulation, int centerX, int centerY, float dt) {
    for (int y = centerY - 1; y <= centerY + 1; ++y) {
        for (int x = centerX - 1; x <= centerX + 1; ++x) {
            float rate = DENSITY_INJECTION_RATE;
            if (x == centerX && y == centerY) {
                rate *= CENTER_DENSITY_RATE_SCALE;
            }
            simulation.addDensity(x, y, rate * dt);
        }
    }
}

void addVelocityBrush(FluidSim& simulation, int centerX, int centerY,
                      float velocityX, float velocityY, float dt) {
    for (int y = centerY - 1; y <= centerY + 1; ++y) {
        for (int x = centerX - 1; x <= centerX + 1; ++x) {
            simulation.addVelocity(x, y, velocityX * dt, velocityY * dt);
        }
    }
}

void handleMouseInput(float rawDt, float dt) {
    bool dragPending = mouse.dragPending;
    double movementX = mouse.movementX;
    double movementY = mouse.movementY;
    mouse.dragPending = false;
    mouse.movementX = 0.0;
    mouse.movementY = 0.0;

    if (!fluid || !mouse.hasPosition || dt <= 0.0f ||
        !std::isfinite(rawDt) || rawDt <= 0.0f) {
        return;
    }

    int gridX;
    int gridY;
    if (!mapWindowToGrid(mouse.x, mouse.y, windowWidth, windowHeight,
                         GRID_SIZE, gridX, gridY)) {
        return;
    }

    float velocityX;
    float velocityY;
    bool hasVelocity = calculatePointerVelocityRate(
        movementX, movementY, windowWidth, windowHeight, rawDt,
        velocityX, velocityY);

    if (mouse.leftPressed || dragPending) {
        addDensityBrush(*fluid, gridX, gridY, dt);
    }
    if (dragPending && hasVelocity &&
        (movementX != 0.0 || movementY != 0.0)) {
        addVelocityBrush(*fluid, gridX, gridY, velocityX, velocityY, dt);
    }
}

void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << '\n';
}

void framebufferSizeCallback(GLFWwindow*, int width, int height) {
    framebufferWidth = width;
    framebufferHeight = height;
    glViewport(0, 0, width, height);
}

void windowSizeCallback(GLFWwindow*, int width, int height) {
    windowWidth = width;
    windowHeight = height;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }

    if (action == GLFW_PRESS) {
        mouse.leftPressed = true;
        mouse.dragPending = true;
        glfwGetCursorPos(window, &mouse.x, &mouse.y);
        mouse.hasPosition =
            std::isfinite(mouse.x) && std::isfinite(mouse.y);
        mouse.movementX = 0.0;
        mouse.movementY = 0.0;
    } else if (action == GLFW_RELEASE) {
        mouse.leftPressed = false;
    }
}

void cursorPositionCallback(GLFWwindow*, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return;
    }

    if (mouse.leftPressed && mouse.hasPosition) {
        mouse.movementX += x - mouse.x;
        mouse.movementY += y - mouse.y;
        mouse.dragPending = true;
    }

    mouse.x = x;
    mouse.y = y;
    mouse.hasPosition = true;
}

void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) {
        return;
    }

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if (key == GLFW_KEY_R) {
        // Allocation is deferred so exceptions do not cross the GLFW callback.
        resetRequested = true;
    } else if (key == GLFW_KEY_1 && renderer) {
        renderer->setRenderMode(RenderMode::DENSITY);
    } else if (key == GLFW_KEY_2 && renderer) {
        renderer->setRenderMode(RenderMode::DIVERGENCE);
    } else if (key == GLFW_KEY_3 && renderer) {
        renderer->setRenderMode(RenderMode::VORTICITY);
    } else if (key == GLFW_KEY_TAB && renderer) {
        renderer->nextRenderMode();
    }
}

#ifndef FLUIDSIM_TEST_BUILD
int main() {
    if (!glfwInit()) {
        std::cerr << "Could not initialize GLFW\n";
        return EXIT_FAILURE;
    }

    glfwSetErrorCallback(glfwErrorCallback);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(
        windowWidth, windowHeight, "2D Fluid Simulation", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Could not create the GLFW window\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSetWindowSizeCallback(window, windowSizeCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPositionCallback);
    glfwSetKeyCallback(window, keyCallback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Could not initialize GLEW\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // GLEW can leave this harmless error on core-profile contexts.
    while (glGetError() != GL_NO_ERROR) {
    }

    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glfwSwapInterval(1);

    std::cout << "2D Fluid Simulation\n"
              << "Left mouse + drag: add dye and velocity\n"
              << "1, 2, 3: density, divergence, vorticity\n"
              << "Tab: cycle display mode\n"
              << "R: reset\n"
              << "Esc: quit\n";

    int exitStatus = EXIT_SUCCESS;

    try {
        fluid = std::make_unique<FluidSim>(DIFFUSION, VISCOSITY);
        renderer = std::make_unique<Renderer>();

        if (!renderer->initialize(GRID_SIZE)) {
            std::cerr << "Could not initialize renderer:\n"
                      << renderer->getLastError() << '\n';
            exitStatus = EXIT_FAILURE;
        } else {
            lastFrameTime = glfwGetTime();

            while (!glfwWindowShouldClose(window)) {
                glfwPollEvents();

                double currentTime = glfwGetTime();
                float rawDt =
                    static_cast<float>(currentTime - lastFrameTime);
                float dt = clampTimestep(rawDt);
                lastFrameTime = currentTime;

                if (resetRequested) {
                    fluid = std::make_unique<FluidSim>(DIFFUSION, VISCOSITY);
                    resetRequested = false;
                }

                handleMouseInput(rawDt, dt);
                fluid->step(dt);
                renderer->draw(*fluid);
                glfwSwapBuffers(window);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Simulation stopped: " << error.what() << '\n';
        exitStatus = EXIT_FAILURE;
    }

    renderer.reset();
    fluid.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitStatus;
}
#endif
