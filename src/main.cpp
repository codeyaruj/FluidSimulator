#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <iomanip>
#include "FluidSim.h"
#include "Renderer.h"
#include "SimulationUtils.h"

// Global state
std::unique_ptr<FluidSim> g_fluidSim;
std::unique_ptr<Renderer> g_renderer;
bool g_resetRequested = false;
int g_windowWidth = 800;
int g_windowHeight = 800;
int g_framebufferWidth = 800;
int g_framebufferHeight = 800;

// Mouse interaction state
struct MouseInputState {
    bool leftPressed = false;
    bool hasCursorPosition = false;
    double cursorX = 0.0;
    double cursorY = 0.0;
    double accumulatedDeltaX = 0.0;
    double accumulatedDeltaY = 0.0;
};

MouseInputState g_mouseInput;

// Timing for dynamic timestep
double g_lastFrameTime = 0.0;

/**
 * Print visualization mode help text.
 */
void printModeHelp() {
    std::cout << "\n=== Visualization Modes ===" << std::endl;
    std::cout << "1 - Density (dye visualization)" << std::endl;
    std::cout << "2 - Divergence (should be near zero - black)" << std::endl;
    std::cout << "3 - Vorticity (rotation detection)" << std::endl;
}

void errorCallback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    g_framebufferWidth = width;
    g_framebufferHeight = height;
    glViewport(0, 0, width, height);
}

void windowSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    g_windowWidth = width;
    g_windowHeight = height;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_mouseInput.leftPressed = true;
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            if (std::isfinite(xpos) && std::isfinite(ypos)) {
                g_mouseInput.hasCursorPosition = true;
                g_mouseInput.cursorX = xpos;
                g_mouseInput.cursorY = ypos;
            }
            g_mouseInput.accumulatedDeltaX = 0.0;
            g_mouseInput.accumulatedDeltaY = 0.0;
        } else if (action == GLFW_RELEASE) {
            g_mouseInput.leftPressed = false;
        }
    }
}

void cursorPositionCallback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (!std::isfinite(xpos) || !std::isfinite(ypos)) {
        return;
    }

    if (g_mouseInput.leftPressed && g_mouseInput.hasCursorPosition) {
        g_mouseInput.accumulatedDeltaX += xpos - g_mouseInput.cursorX;
        g_mouseInput.accumulatedDeltaY += ypos - g_mouseInput.cursorY;
    }

    g_mouseInput.hasCursorPosition = true;
    g_mouseInput.cursorX = xpos;
    g_mouseInput.cursorY = ypos;
}

void applyMouseInput(float rawDt, float simulationDt) {
    double pendingDeltaX = g_mouseInput.accumulatedDeltaX;
    double pendingDeltaY = g_mouseInput.accumulatedDeltaY;
    g_mouseInput.accumulatedDeltaX = 0.0;
    g_mouseInput.accumulatedDeltaY = 0.0;

    if (!std::isfinite(pendingDeltaX) || !std::isfinite(pendingDeltaY)) {
        pendingDeltaX = 0.0;
        pendingDeltaY = 0.0;
    }

    if (!g_fluidSim || !g_mouseInput.hasCursorPosition ||
        simulationDt <= 0.0f || !std::isfinite(rawDt) || rawDt <= 0.0f) {
        return;
    }

    GridCoordinate gridPosition;
    if (!mapWindowToGrid(g_mouseInput.cursorX, g_mouseInput.cursorY,
                         g_windowWidth, g_windowHeight, GRID_SIZE,
                         gridPosition)) {
        return;
    }

    float velocityRateX = 0.0f;
    float velocityRateY = 0.0f;
    const bool hasVelocityRate = calculatePointerVelocityRate(
        pendingDeltaX, pendingDeltaY, g_windowWidth, g_windowHeight, rawDt,
        MOUSE_VELOCITY_COUPLING, MAX_NORMALIZED_MOUSE_SPEED,
        velocityRateX, velocityRateY);

    if (g_mouseInput.leftPressed) {
        applyDensityBrush(*g_fluidSim, gridPosition, simulationDt);
    }

    if (hasVelocityRate && (pendingDeltaX != 0.0 || pendingDeltaY != 0.0)) {
        applyVelocityBrush(*g_fluidSim, gridPosition, velocityRateX,
                           velocityRateY, simulationDt);
    }
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    
    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
            
        case GLFW_KEY_R:
            // Defer allocation so exceptions remain inside main's try/catch.
            g_resetRequested = true;
            break;
            
        // Visualization mode keys
        case GLFW_KEY_1:
            if (g_renderer) {
                g_renderer->setRenderMode(RenderMode::DENSITY);
                std::cout << "Mode: DENSITY" << std::endl;
            }
            break;
            
        case GLFW_KEY_2:
            if (g_renderer) {
                g_renderer->setRenderMode(RenderMode::DIVERGENCE);
                std::cout << "Mode: DIVERGENCE (should be near black/zero)" << std::endl;
            }
            break;
            
        case GLFW_KEY_3:
            if (g_renderer) {
                g_renderer->setRenderMode(RenderMode::VORTICITY);
                std::cout << "Mode: VORTICITY (rotation detection)" << std::endl;
            }
            break;
            
        case GLFW_KEY_TAB:
            // Cycle to next mode
            if (g_renderer) {
                g_renderer->nextRenderMode();
                std::cout << "Mode changed" << std::endl;
            }
            break;
            
        case GLFW_KEY_H:
            printModeHelp();
            break;
    }
}

int main() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return EXIT_FAILURE;
    }
    
    glfwSetErrorCallback(errorCallback);
    
    // Set OpenGL version (3.3 Core)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    
    // Create window
    GLFWwindow* window = glfwCreateWindow(g_windowWidth, g_windowHeight, 
                                          "2D Fluid Simulation - Debug Mode", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }
    
    glfwMakeContextCurrent(window);
    glfwSetWindowSizeCallback(window, windowSizeCallback);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPositionCallback);
    glfwSetKeyCallback(window, keyCallback);
    
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // GLEW may leave a benign GL_INVALID_ENUM on core-profile contexts.
    while (glGetError() != GL_NO_ERROR) {}

    glfwGetWindowSize(window, &g_windowWidth, &g_windowHeight);
    glfwGetFramebufferSize(window, &g_framebufferWidth, &g_framebufferHeight);
    
    // Set viewport
    glViewport(0, 0, g_framebufferWidth, g_framebufferHeight);
    
    // Enable vsync for consistent frame rate
    glfwSwapInterval(1);
    
    // Print system info
    std::cout << "========================================" << std::endl;
    std::cout << "  2D Fluid Simulation - Debug Edition" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "\nGrid Size: " << GRID_SIZE << "x" << GRID_SIZE << std::endl;
    std::cout << "Window Size: " << g_windowWidth << "x" << g_windowHeight << std::endl;
    std::cout << "Framebuffer Size: " << g_framebufferWidth << "x"
              << g_framebufferHeight << std::endl;
    std::cout << "Viscosity: " << VISCOSITY << std::endl;
    std::cout << "Diffusion: " << DIFFUSION << std::endl;
    
    std::cout << "\n=== CONTROLS ===" << std::endl;
    std::cout << "Mouse Left + Drag  : Add fluid and velocity" << std::endl;
    std::cout << "R                  : Reset simulation" << std::endl;
    std::cout << "1-3                : Switch visualization mode" << std::endl;
    std::cout << "TAB                : Cycle modes" << std::endl;
    std::cout << "H                  : Show mode help" << std::endl;
    std::cout << "ESC                : Exit" << std::endl;
    
    printModeHelp();
    
    int exitStatus = EXIT_SUCCESS;
    try {
        // Initialize simulation and renderer while the OpenGL context is current.
        g_fluidSim = std::make_unique<FluidSim>(DIFFUSION, VISCOSITY);
        g_renderer = std::make_unique<Renderer>(GRID_SIZE);

        g_lastFrameTime = glfwGetTime();
        double lastFPSTime = glfwGetTime();
        int frameCount = 0;

        while (!glfwWindowShouldClose(window)) {
            // Poll first so all cursor events are accumulated once per update.
            glfwPollEvents();
            if (glfwWindowShouldClose(window)) {
                break;
            }

            const double currentTime = glfwGetTime();
            const float rawDt = static_cast<float>(currentTime - g_lastFrameTime);
            const float simulationDt = FluidSim::sanitizeTimestep(rawDt);
            g_lastFrameTime = currentTime;

            frameCount++;
            if (currentTime - lastFPSTime >= 1.0) {
                std::cout << "FPS: " << frameCount << " | dt: " << std::fixed
                          << std::setprecision(2) << (simulationDt * 1000.0f)
                          << "ms" << std::endl;
                frameCount = 0;
                lastFPSTime = currentTime;
            }

            if (g_resetRequested) {
                g_fluidSim = std::make_unique<FluidSim>(DIFFUSION, VISCOSITY);
                g_resetRequested = false;
                std::cout << "Simulation reset!" << std::endl;
            }

            applyMouseInput(rawDt, simulationDt);
            g_fluidSim->step(simulationDt);
            g_renderer->draw(*g_fluidSim);
            glfwSwapBuffers(window);
        }
    } catch (const std::exception& error) {
        std::cerr << "Fatal initialization or rendering error:\n"
                  << error.what() << std::endl;
        exitStatus = EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal unknown initialization or rendering error" << std::endl;
        exitStatus = EXIT_FAILURE;
    }
    
    // Cleanup
    g_renderer.reset();
    g_fluidSim.reset();
    
    glfwDestroyWindow(window);
    glfwTerminate();
    
    return exitStatus;
}
