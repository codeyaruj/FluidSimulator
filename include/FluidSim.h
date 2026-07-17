#ifndef FLUIDSIM_H
#define FLUIDSIM_H

#include <cassert>
#include <vector>

constexpr int GRID_SIZE = 128;
constexpr int DIFFUSION_SOLVER_ITERATIONS = 4;
constexpr int PRESSURE_SOLVER_MAX_ITERATIONS = 2048;
constexpr float PRESSURE_RELAXATION = 1.9f;
constexpr float PROJECTION_DIVERGENCE_TOLERANCE = 0.01f;
constexpr float DIFFUSION = 0.0f;
constexpr float VISCOSITY = 0.0000001f;

// Timesteps use seconds and density stays in the range [0, 1].
constexpr float MAX_SIMULATION_TIMESTEP = 0.05f;
constexpr float MIN_DENSITY = 0.0f;
constexpr float MAX_DENSITY = 1.0f;
constexpr float DENSITY_FADE_RATE = 0.12f;
constexpr float DENSITY_INJECTION_RATE = 1.0f;
constexpr float CENTER_DENSITY_RATE_SCALE = 3.0f;

class FluidSim {
private:
    int size;
    float diffusion;
    float viscosity;

    // Previous buffers keep solver input separate from its output.
    std::vector<float> densityPrevious;
    std::vector<float> density;
    std::vector<float> velocityX;
    std::vector<float> velocityY;
    std::vector<float> velocityXPrevious;
    std::vector<float> velocityYPrevious;

    std::vector<float> pressure;
    std::vector<float> divergence;
    std::vector<float> vorticity;

    int index(int x, int y) const {
        assert(x >= 0 && x < size);
        assert(y >= 0 && y < size);
        return x + y * size;
    }

    void diffuse(int boundaryType,
                 std::vector<float>& output,
                 const std::vector<float>& input,
                 float diffusionRate,
                 float dt);

    // Removes divergence from velocity by solving for pressure.
    void project(std::vector<float>& velocityX,
                 std::vector<float>& velocityY,
                 std::vector<float>& pressureField,
                 std::vector<float>& divergenceField);

    // Moves a field backward through the current velocity field.
    void advect(int boundaryType,
                std::vector<float>& output,
                const std::vector<float>& input,
                const std::vector<float>& sourceVelocityX,
                const std::vector<float>& sourceVelocityY,
                float dt);

    // 0 mirrors scalars, 1 stops X flow, and 2 stops Y flow.
    void setBoundary(int boundaryType, std::vector<float>& field);

    bool linearSolve(int boundaryType,
                     std::vector<float>& output,
                     const std::vector<float>& input,
                     float a,
                     float c,
                     int maxIterations,
                     float relaxation,
                     float residualTolerance = 0.0f);

    void computeDivergence(const std::vector<float>& velocityX,
                           const std::vector<float>& velocityY,
                           std::vector<float>& output);

    void computeVorticityField();

public:
    FluidSim(float diffusionRate, float viscosityRate);
    ~FluidSim();

    void step(float dt);

    void addDensity(int x, int y, float amount);
    void addVelocity(int x, int y, float amountX, float amountY);
    void fadeDensity(float dt);

    const std::vector<float>& getDensity() const { return density; }
    const std::vector<float>& getVelocityX() const { return velocityX; }
    const std::vector<float>& getVelocityY() const { return velocityY; }
    const std::vector<float>& getDivergence() const { return divergence; }
    const std::vector<float>& getVorticity() const { return vorticity; }
    int getSize() const { return size; }
};

#endif
