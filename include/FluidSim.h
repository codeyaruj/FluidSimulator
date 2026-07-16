#ifndef FLUIDSIM_H
#define FLUIDSIM_H

#include <vector>
#include <cassert>

// Grid configuration constants
constexpr int GRID_SIZE = 128;
constexpr int DIFFUSION_SOLVER_ITERATIONS = 4;
constexpr int PRESSURE_SOLVER_MAX_ITERATIONS = 2048;
constexpr float PRESSURE_RELAXATION = 1.9f;
constexpr float PROJECTION_DIVERGENCE_TOLERANCE = 0.01f;
constexpr float DIFFUSION = 0.0f;
constexpr float VISCOSITY = 0.0000001f;

// Simulation values use seconds and a normalized density range of [0, 1].
constexpr float MAX_SIMULATION_TIMESTEP = 0.05f;
constexpr float MIN_DENSITY = 0.0f;
constexpr float MAX_DENSITY = 1.0f;
constexpr float DENSITY_FADE_RATE = 0.12f;       // Density units per second.
constexpr float DENSITY_INJECTION_RATE = 1.0f;  // Density units per second.
constexpr float CENTER_DENSITY_RATE_SCALE = 3.0f;

/**
 * FluidSim implements Jos Stam's Stable Fluids algorithm for 2D incompressible flow.
 * 
 * Key concepts:
 * - Eulerian grid-based simulation
 * - Semi-Lagrangian advection (stable for large timesteps)
 * - Helmholtz-Hodge decomposition for velocity projection
 * - Gauss-Seidel/SOR relaxation for implicit diffusion and pressure solves
 */
class FluidSim {
private:
    int size;
    float diff;   // Diffusion rate
    float visc;   // Viscosity
    
    // Density fields (current and previous for double buffering)
    std::vector<float> s;       // Previous density (source for diffusion)
    std::vector<float> density; // Current density
    
    // Velocity fields (current and previous)
    std::vector<float> Vx;   // X velocity component
    std::vector<float> Vy;   // Y velocity component
    std::vector<float> Vx0;  // Previous X velocity
    std::vector<float> Vy0;  // Previous Y velocity
    
    // Auxiliary fields for projection step
    std::vector<float> pressure;    // Pressure field (solved each step)
    std::vector<float> divergence;  // Divergence of velocity field
    std::vector<float> vorticity;   // Vorticity field (curl of velocity)

    /**
     * Convert 2D grid coordinates to 1D array index.
     * ASSUMES valid indices (0 <= x < size, 0 <= y < size).
     * Use debug assertions to catch out-of-bounds access.
     */
    int IX(int x, int y) const {
        assert(x >= 0 && x < size);
        assert(y >= 0 && y < size);
        return x + y * size;
    }
    
    /**
     * Diffusion step - spreads density/velocity via implicit method.
     * Uses iterative relaxation to solve the linear system.
     * 
     * Why implicit? Explicit diffusion is unstable for large dt.
     * Implicit method: (I - dt*diff*Laplacian) * x = x0
     */
    void diffuse(int b, std::vector<float>& x, const std::vector<float>& x0, 
                 float diff, float dt);
    
    /**
     * Projection step - makes velocity field mass-conserving (divergence-free).
     * 
     * Theory: Any vector field can be decomposed into divergence-free 
     * and curl-free components (Helmholtz-Hodge decomposition).
     * We solve Poisson equation: Laplacian(p) = div(V)
     * Then subtract gradient: V' = V - grad(p)
     * 
     * This enforces incompressibility (∇·V = 0), a key property of fluids.
     */
    void project(std::vector<float>& velocityX,
                 std::vector<float>& velocityY,
                 std::vector<float>& pressureField,
                 std::vector<float>& divergenceField);
    
    /**
     * Advection step - moves quantities along the velocity field.
     * 
     * Uses semi-Lagrangian method:
     * 1. For each cell, trace backward along velocity field
     * 2. Interpolate value at traced position
     * 3. This is unconditionally stable (no CFL constraint)
     * 
     * Why stable? We're interpolating from known values, not extrapolating.
     */
    void advect(int b, std::vector<float>& d, const std::vector<float>& d0,  
                const std::vector<float>& velocX, const std::vector<float>& velocY, 
                float dt);
    
    /**
     * Boundary conditions handler.
     * b=0: continuous (density, pressure)
     * b=1: zero X velocity at left/right boundary samples
     * b=2: zero Y velocity at bottom/top boundary samples
     */
    void set_bnd(int b, std::vector<float>& x);
    
    /**
     * Linear solver using Gauss-Seidel or successive over-relaxation.
     * Solves: x = (x0 + a*(neighbors)) / c
     * 
     * Used for both diffusion (implicit solve) and projection (Poisson solve).
     * Updated values are consumed immediately for cache-friendly convergence.
     */
    bool lin_solve(int b, std::vector<float>& x,
                   const std::vector<float>& x0,
                   float a, float c, int maxIterations, float relaxation,
                   float residualTolerance = 0.0f);
    
    /**
     * Compute divergence of velocity field.
     * div(V) = ∂Vx/∂x + ∂Vy/∂y
     * Uses the backward difference paired with projection's forward gradient.
     * Used for visualization and projection.
     */
    void computeDivergence(const std::vector<float>& velocityX,
                           const std::vector<float>& velocityY,
                           std::vector<float>& output);
    
    /**
     * Compute scalar vorticity (curl magnitude).
     * ω = ∂Vy/∂x - ∂Vx/∂y
     * Positive = counter-clockwise rotation
     * Negative = clockwise rotation
     */
    void computeVorticityField();

public:
    FluidSim(float diffusion, float viscosity);
    ~FluidSim();
    
    /**
     * Main simulation step.
     * Timestep in seconds (computed from frame time)
     */
    void step(float dt);

    /**
     * Convert an external timestep to the timestep used by the simulation.
     * Negative/non-finite values become zero and long frames are capped.
     */
    static float sanitizeTimestep(float dt);
    
    // Interaction methods
    void addDensity(int x, int y, float amount);
    void addDensityRate(int x, int y, float amountPerSecond, float dt);
    void addVelocity(int x, int y, float amountX, float amountY);
    void addVelocityRate(int x, int y, float amountXPerSecond,
                         float amountYPerSecond, float dt);
    void fadeDensity(float dt);
    
    // Getters for visualization
    const std::vector<float>& getDensity() const { return density; }
    const std::vector<float>& getVx() const { return Vx; }
    const std::vector<float>& getVy() const { return Vy; }
    const std::vector<float>& getDivergence() const { return divergence; }
    const std::vector<float>& getVorticity() const { return vorticity; }
    int getSize() const { return size; }
};

#endif // FLUIDSIM_H
