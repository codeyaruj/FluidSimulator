#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <memory>
#include <iomanip>
#include "FluidSim.h"
#include "Renderer.h"

// Global state
std::unique_ptr<FluidSim> g_fluidSim;
std::unique_ptr<Renderer> g_renderer;
int g_windowWidth = 800;
int g_windowHeight = 800;

// Mouse interaction state
bool g_leftMousePressed = false;
double g_prevMouseX = 0.0;
double g_prevMouseY = 0.0;

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

/**
 * Convert screen coordinates to grid coordinates.
 * Handles coordinate system transformation (screen Y is inverted).
 */
void screenToGrid(double screenX, double screenY, int& gridX, int& gridY) {
    gridX = static_cast<int>((screenX / g_windowWidth) * GRID_SIZE);
    gridY = static_cast<int>(((g_windowHeight - screenY) / g_windowHeight) * GRID_SIZE);
}

void errorCallback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

void framebufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    g_windowWidth = width;
    g_windowHeight = height;
    glViewport(0, 0, width, height);
    if (g_renderer) {
        g_renderer->setWindowSize(width, height);
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_leftMousePressed = true;
            double xpos, ypos;
            glfwGetCursorPos(window, &xpos, &ypos);
            g_prevMouseX = xpos;
            g_prevMouseY = ypos;
        } else if (action == GLFW_RELEASE) {
            g_leftMousePressed = false;
        }
    }
}

void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
    if (g_fluidSim) {
        int gridX, gridY;
        screenToGrid(xpos, ypos, gridX, gridY);
        
        // Left-click: add fluid and velocity
        if (g_leftMousePressed) {
            // Calculate mouse velocity for fluid injection
            float dx = static_cast<float>(xpos - g_prevMouseX);
            float dy = static_cast<float>(ypos - g_prevMouseY);
            
            // Add density (dye) at cursor
            g_fluidSim->addDensity(gridX, gridY, 200.0f);
            
            // Add velocity based on mouse movement
            g_fluidSim->addVelocity(gridX, gridY, dx * 5.0f, -dy * 5.0f);
            
            // Add in a small radius for better visuals
            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    g_fluidSim->addDensity(gridX + i, gridY + j, 100.0f);
                    g_fluidSim->addVelocity(gridX + i, gridY + j, dx * 5.0f, -dy * 5.0f);
                }
            }
            
            g_prevMouseX = xpos;
            g_prevMouseY = ypos;
        }
    }
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    
    switch (key) {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
            
        case GLFW_KEY_R:
            // Reset simulation
            if (g_fluidSim) {
                g_fluidSim = std::make_unique<FluidSim>(DIFFUSION, VISCOSITY);
                std::cout << "Simulation reset!" << std::endl;
            }
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
        return -1;
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
        return -1;
    }
    
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPositionCallback);
    glfwSetKeyCallback(window, keyCallback);
    
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }
    
    // Set viewport
    glViewport(0, 0, g_windowWidth, g_windowHeight);
    
    // Enable vsync for consistent frame rate
    glfwSwapInterval(1);
    
    // Print system info
    std::cout << "========================================" << std::endl;
    std::cout << "  2D Fluid Simulation - Debug Edition" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "\nGrid Size: " << GRID_SIZE << "x" << GRID_SIZE << std::endl;
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
    
    // Initialize simulation and renderer
    g_fluidSim = std::make_unique<FluidSim>(DIFFUSION, VISCOSITY);
    g_renderer = std::make_unique<Renderer>(GRID_SIZE, g_windowWidth, g_windowHeight);
    
    // Timing variables
    g_lastFrameTime = glfwGetTime();
    double lastFPSTime = glfwGetTime();
    int frameCount = 0;
    
    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Calculate delta time for dynamic timestep
        double currentTime = glfwGetTime();
        float dt = static_cast<float>(currentTime - g_lastFrameTime);
        g_lastFrameTime = currentTime;
        
        // FPS counter
        frameCount++;
        if (currentTime - lastFPSTime >= 1.0) {
            std::cout << "FPS: " << frameCount << " | dt: " << std::fixed 
                      << std::setprecision(2) << (dt * 1000.0f) << "ms" << std::endl;
            frameCount = 0;
            lastFPSTime = currentTime;
        }
        
        // Update simulation with dynamic timestep
        g_fluidSim->step(dt);
        
        // Render with current visualization mode
        g_renderer->draw(*g_fluidSim);
        
        // Swap buffers and poll events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    // Cleanup
    g_renderer.reset();
    g_fluidSim.reset();
    
    glfwDestroyWindow(window);
    glfwTerminate();
    
    return 0;
}
