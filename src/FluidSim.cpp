#include "FluidSim.h"
#include <algorithm>
#include <cmath>
#include <cstring>

/**
 * Constructor - preallocates all buffers to avoid runtime allocations.
 * All grids are sized N x N where N = GRID_SIZE.
 */
FluidSim::FluidSim(float diffusion, float viscosity) 
    : size(GRID_SIZE), diff(diffusion), visc(viscosity) {
    
    const int N = size * size;
    
    // Density fields (double buffered)
    s.resize(N, 0.0f);
    density.resize(N, 0.0f);
    
    // Velocity fields (double buffered)
    Vx.resize(N, 0.0f);
    Vy.resize(N, 0.0f);
    Vx0.resize(N, 0.0f);
    Vy0.resize(N, 0.0f);
    
    // Auxiliary fields for projection and visualization
    pressure.resize(N, 0.0f);
    divergence.resize(N, 0.0f);
    vorticity.resize(N, 0.0f);
}

FluidSim::~FluidSim() {
    // All vectors automatically clean up
}

/**
 * Add density at grid position (x, y).
 * Boundary check prevents out-of-bounds access.
 */
void FluidSim::addDensity(int x, int y, float amount) {
    if (x >= 1 && x < size - 1 && y >= 1 && y < size - 1) {
        density[IX(x, y)] += amount;
    }
}

/**
 * Add velocity at grid position (x, y).
 * Boundary check prevents out-of-bounds access.
 */
void FluidSim::addVelocity(int x, int y, float amountX, float amountY) {
    if (x >= 1 && x < size - 1 && y >= 1 && y < size - 1) {
        int index = IX(x, y);
        Vx[index] += amountX;
        Vy[index] += amountY;
    }
}

/**
 * Gradually fade density to create trail effect.
 * Prevents density from accumulating indefinitely.
 */
void FluidSim::fadeDensity() {
    const int N = size * size;
    for (int i = 0; i < N; i++) {
        density[i] = std::max(0.0f, density[i] - 0.002f);
    }
}

/**
 * Set boundary conditions for different field types.
 * 
 * b=0: Continuous (density, pressure) - mirror boundary
 * b=1: Horizontal velocity (Vx=0 at vertical walls)
 * b=2: Vertical velocity (Vy=0 at horizontal walls)
 * 
 * This enforces no-slip boundary conditions for velocity
 * and zero-gradient for scalar fields.
 */
void FluidSim::set_bnd(int b, std::vector<float>& x) {
    // Top and bottom boundaries
    for (int i = 1; i < size - 1; i++) {
        // Bottom wall: invert Vy (b==2), mirror others
        x[IX(i, 0)] = (b == 2) ? -x[IX(i, 1)] : x[IX(i, 1)];
        // Top wall
        x[IX(i, size - 1)] = (b == 2) ? -x[IX(i, size - 2)] : x[IX(i, size - 2)];
    }
    
    // Left and right boundaries
    for (int i = 1; i < size - 1; i++) {
        // Left wall: invert Vx (b==1), mirror others
        x[IX(0, i)] = (b == 1) ? -x[IX(1, i)] : x[IX(1, i)];
        // Right wall
        x[IX(size - 1, i)] = (b == 1) ? -x[IX(size - 2, i)] : x[IX(size - 2, i)];
    }
    
    // Corner cells - average of neighbors
    x[IX(0, 0)] = 0.5f * (x[IX(1, 0)] + x[IX(0, 1)]);
    x[IX(0, size - 1)] = 0.5f * (x[IX(1, size - 1)] + x[IX(0, size - 2)]);
    x[IX(size - 1, 0)] = 0.5f * (x[IX(size - 2, 0)] + x[IX(size - 1, 1)]);
    x[IX(size - 1, size - 1)] = 0.5f * (x[IX(size - 2, size - 1)] + x[IX(size - 1, size - 2)]);
}

/**
 * Linear solver using Gauss-Seidel relaxation.
 * 
 * Solves the system: x[i] = (x0[i] + a * sum(neighbors)) / c
 * 
 * This is used for:
 * 1. Implicit diffusion (large timesteps stable)
 * 2. Poisson equation in projection step
 * 
 * Cache-friendly row-major iteration for spatial locality.
 * Iterates ITER times (typically 20-50 for good convergence).
 */
void FluidSim::lin_solve(int b, std::vector<float>& x, const std::vector<float>& x0, 
                         float a, float c) {
    const float cRecip = 1.0f / c;
    
    for (int k = 0; k < ITER; k++) {
        // Row-major order for cache efficiency
        for (int j = 1; j < size - 1; j++) {
            for (int i = 1; i < size - 1; i++) {
                // Gauss-Seidel: use updated values immediately
                x[IX(i, j)] = (x0[IX(i, j)] 
                    + a * (x[IX(i + 1, j)] 
                         + x[IX(i - 1, j)] 
                         + x[IX(i, j + 1)] 
                         + x[IX(i, j - 1)])) * cRecip;
            }
        }
        set_bnd(b, x);
    }
}

/**
 * Diffusion step - spreads density/velocity via implicit method.
 * 
 * Uses backward Euler: (I - dt*diff*Laplacian)x = x0
 * This is unconditionally stable (no CFL constraint on diffusion).
 * 
 * Discretized: x[i] - dt*diff*(x[i+1]+x[i-1]+x[i+N]+x[i-N]-4*x[i])/h^2 = x0[i]
 * Rearranged: x[i]*(1+4*a) - a*sum(neighbors) = x0[i]
 * Where a = dt * diff * N * N
 */
void FluidSim::diffuse(int b, std::vector<float>& x, const std::vector<float>& x0, 
                       float diff, float dt) {
    float a = dt * diff * (size - 2) * (size - 2);
    lin_solve(b, x, x0, a, 1.0f + 4.0f * a);
}

/**
 * Projection step - makes velocity field divergence-free.
 * 
 * Theory (Helmholtz-Hodge decomposition):
 * Any vector field V can be decomposed as:
 *   V = V_divFree + grad(phi)
 * where div(V_divFree) = 0
 * 
 * Taking divergence: div(V) = Laplacian(phi)
 * Solve for phi (pressure), then:
 *   V_divFree = V - grad(phi)
 * 
 * This enforces the incompressibility constraint: ∇·V = 0
 * Physical meaning: fluid volume is conserved (incompressible flow).
 */
void FluidSim::project(float /*dt*/) {
    const float h = 1.0f / size;
    
    // Step 1: Compute divergence of velocity field
    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            divergence[IX(i, j)] = -0.5f * h * (
                Vx0[IX(i + 1, j)] - Vx0[IX(i - 1, j)] +
                Vy0[IX(i, j + 1)] - Vy0[IX(i, j - 1)]
            );
            pressure[IX(i, j)] = 0.0f;
        }
    }
    
    set_bnd(0, divergence);
    set_bnd(0, pressure);
    
    // Step 2: Solve Poisson equation for pressure
    lin_solve(0, pressure, divergence, 1.0f, 4.0f);
    
    // Step 3: Subtract pressure gradient from velocity
    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            Vx[IX(i, j)] -= 0.5f * (pressure[IX(i + 1, j)] - pressure[IX(i - 1, j)]) / h;
            Vy[IX(i, j)] -= 0.5f * (pressure[IX(i, j + 1)] - pressure[IX(i, j - 1)]) / h;
        }
    }
    
    set_bnd(1, Vx);
    set_bnd(2, Vy);
}

/**
 * Advection step - moves quantities along the velocity field.
 * 
 * Semi-Lagrangian method (unconditionally stable):
 * 1. For each cell center, trace backward along velocity field
 * 2. Find where the particle came from (backtrace)
 * 3. Interpolate value at that position
 * 4. Assign interpolated value to current cell
 * 
 * Why stable? We're always interpolating between known values,
 * never extrapolating beyond the data. No CFL constraint!
 * 
 * Bilinear interpolation gives smooth results.
 */
void FluidSim::advect(int b, std::vector<float>& d, const std::vector<float>& d0,  
                      const std::vector<float>& velocX, const std::vector<float>& velocY, 
                      float dt) {
    float dtx = dt * (size - 2);
    float dty = dt * (size - 2);
    
    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            // Backtrace: find where this cell's content came from
            float x = i - dtx * velocX[IX(i, j)];
            float y = j - dty * velocY[IX(i, j)];
            
            // Clamp to valid interpolation range
            if (x < 0.5f) x = 0.5f;
            if (x > size - 1.5f) x = size - 1.5f;
            int i0 = static_cast<int>(x);
            int i1 = i0 + 1;
            
            if (y < 0.5f) y = 0.5f;
            if (y > size - 1.5f) y = size - 1.5f;
            int j0 = static_cast<int>(y);
            int j1 = j0 + 1;
            
            // Bilinear interpolation weights
            float s1 = x - i0;
            float s0 = 1.0f - s1;
            float t1 = y - j0;
            float t0 = 1.0f - t1;
            
            // Interpolate from 4 neighboring cells
            d[IX(i, j)] = s0 * (t0 * d0[IX(i0, j0)] + t1 * d0[IX(i0, j1)]) +
                          s1 * (t0 * d0[IX(i1, j0)] + t1 * d0[IX(i1, j1)]);
        }
    }
    
    set_bnd(b, d);
}

/**
 * Compute divergence field for visualization.
 * div(V) = ∂Vx/∂x + ∂Vy/∂y
 * 
 * Should be near zero everywhere after projection (incompressible flow).
 */
void FluidSim::computeDivergence() {
    const float h = 1.0f / size;
    
    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            divergence[IX(i, j)] = 0.5f * (
                (Vx[IX(i + 1, j)] - Vx[IX(i - 1, j)]) +
                (Vy[IX(i, j + 1)] - Vy[IX(i, j - 1)])
            ) / h;
        }
    }
}

/**
 * Compute scalar vorticity (curl magnitude).
 * ω = ∂Vy/∂x - ∂Vx/∂y
 * 
 * Positive vorticity = counter-clockwise rotation
 * Negative vorticity = clockwise rotation
 * 
 * Reveals turbulent structures and vortices in the flow.
 */
void FluidSim::computeVorticityField() {
    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            float dVydx = 0.5f * (Vy[IX(i + 1, j)] - Vy[IX(i - 1, j)]);
            float dVxdy = 0.5f * (Vx[IX(i, j + 1)] - Vx[IX(i, j - 1)]);
            vorticity[IX(i, j)] = dVydx - dVxdy;
        }
    }
}

/**
 * Main simulation step with dynamic timestep.
 * 
 * Algorithm overview:
 * 1. Diffuse velocity (viscosity)
 * 2. Project to make divergence-free
 * 3. Advect velocity (self-advection)
 * 4. Project again (advection may introduce divergence)
 * 5. Diffuse density
 * 6. Advect density
 * 7. Fade density
 * 8. Compute auxiliary fields for visualization
 * 
 * @param dt Timestep in seconds (computed from frame time)
 */
void FluidSim::step(float dt) {
    // Clamp timestep for stability (prevent huge jumps)
    dt = std::min(dt, 0.05f);  // Max 50ms (20 FPS minimum)
    
    // Velocity step
    diffuse(1, Vx0, Vx, visc, dt);
    diffuse(2, Vy0, Vy, visc, dt);
    
    project(dt);
    
    // Copy to swap buffers
    std::memcpy(Vx.data(), Vx0.data(), Vx.size() * sizeof(float));
    std::memcpy(Vy.data(), Vy0.data(), Vy.size() * sizeof(float));
    
    advect(1, Vx, Vx0, Vx0, Vy0, dt);
    advect(2, Vy, Vy0, Vx0, Vy0, dt);
    
    project(dt);
    
    // Density step
    diffuse(0, s, density, diff, dt);
    advect(0, density, s, Vx, Vy, dt);
    
    // Fade density slightly over time
    fadeDensity();
    
    // Compute auxiliary fields for visualization
    computeDivergence();
    computeVorticityField();
}
