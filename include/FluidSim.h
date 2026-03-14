#ifndef FLUIDSIM_H
#define FLUIDSIM_H

#include <vector>
#include <cassert>

// Grid configuration constants
#define GRID_SIZE 128
#define ITER 4
#define DIFFUSION 0.0f
#define VISCOSITY 0.0000001f

/**
 * FluidSim implements Jos Stam's Stable Fluids algorithm for 2D incompressible flow.
 * 
 * Key concepts:
 * - Eulerian grid-based simulation
 * - Semi-Lagrangian advection (stable for large timesteps)
 * - Helmholtz-Hodge decomposition for velocity projection
 * - Gauss-Seidel relaxation for implicit diffusion and Poisson solve
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
     * Uses Gauss-Seidel iteration to solve the linear system.
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
    void project(float dt);
    
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
     * b=1: horizontal walls (Vx=0)
     * b=2: vertical walls (Vy=0)
     */
    void set_bnd(int b, std::vector<float>& x);
    
    /**
     * Linear solver using Gauss-Seidel relaxation.
     * Solves: x = (x0 + a*(neighbors)) / c
     * 
     * Used for both diffusion (implicit solve) and projection (Poisson solve).
     * Gauss-Seidel converges slowly but is simple and cache-friendly.
     */
    void lin_solve(int b, std::vector<float>& x, const std::vector<float>& x0, 
                   float a, float c);
    
    /**
     * Compute divergence of velocity field.
     * div(V) = ∂Vx/∂x + ∂Vy/∂y
     * Used for visualization and projection.
     */
    void computeDivergence();
    
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
     * @param dt Timestep in seconds (computed from frame time)
     */
    void step(float dt);
    
    // Interaction methods
    void addDensity(int x, int y, float amount);
    void addVelocity(int x, int y, float amountX, float amountY);
    void fadeDensity();
    
    // Getters for visualization
    const std::vector<float>& getDensity() const { return density; }
    const std::vector<float>& getVx() const { return Vx; }
    const std::vector<float>& getVy() const { return Vy; }
    const std::vector<float>& getDivergence() const { return divergence; }
    const std::vector<float>& getVorticity() const { return vorticity; }
    int getSize() const { return size; }
};

#endif // FLUIDSIM_H
