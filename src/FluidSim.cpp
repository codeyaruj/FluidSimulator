#include "FluidSim.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

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
    if (x >= 1 && x < size - 1 && y >= 1 && y < size - 1 &&
        std::isfinite(amount)) {
        const int index = IX(x, y);
        density[index] = std::max(MIN_DENSITY,
                                  std::min(MAX_DENSITY, density[index] + amount));
    }
}

void FluidSim::addDensityRate(int x, int y, float amountPerSecond, float dt) {
    const float safeDt = sanitizeTimestep(dt);
    if (safeDt == 0.0f || !std::isfinite(amountPerSecond)) {
        return;
    }
    addDensity(x, y, amountPerSecond * safeDt);
}

/**
 * Add velocity at grid position (x, y).
 * Boundary check prevents out-of-bounds access.
 */
void FluidSim::addVelocity(int x, int y, float amountX, float amountY) {
    if (x >= 1 && x < size - 1 && y >= 1 && y < size - 1 &&
        std::isfinite(amountX) && std::isfinite(amountY)) {
        int index = IX(x, y);
        const float nextX = Vx[index] + amountX;
        const float nextY = Vy[index] + amountY;
        if (std::isfinite(nextX) && std::isfinite(nextY)) {
            Vx[index] = nextX;
            Vy[index] = nextY;
        }
    }
}

void FluidSim::addVelocityRate(int x, int y, float amountXPerSecond,
                               float amountYPerSecond, float dt) {
    const float safeDt = sanitizeTimestep(dt);
    if (safeDt == 0.0f || !std::isfinite(amountXPerSecond) ||
        !std::isfinite(amountYPerSecond)) {
        return;
    }
    addVelocity(x, y, amountXPerSecond * safeDt, amountYPerSecond * safeDt);
}

/**
 * Gradually fade density to create trail effect.
 * Prevents density from accumulating indefinitely.
 */
void FluidSim::fadeDensity(float dt) {
    const float safeDt = sanitizeTimestep(dt);
    if (safeDt == 0.0f) {
        return;
    }

    const float fadeAmount = DENSITY_FADE_RATE * safeDt;
    const int N = size * size;
    for (int i = 0; i < N; i++) {
        if (!std::isfinite(density[i])) {
            density[i] = MIN_DENSITY;
        } else {
            density[i] = std::max(MIN_DENSITY, density[i] - fadeAmount);
        }
    }
}

/**
 * Set boundary conditions for different field types.
 * 
 * b=0: Continuous (density, pressure) - mirror boundary
 * b=1: Horizontal velocity (Vx=0 at left/right boundary samples)
 * b=2: Vertical velocity (Vy=0 at bottom/top boundary samples)
 * 
 * This enforces no penetration in the normal velocity components and a
 * zero-gradient condition for scalar and tangential velocity fields.
 */
void FluidSim::set_bnd(int b, std::vector<float>& x) {
    // Top and bottom boundaries
    for (int i = 1; i < size - 1; i++) {
        if (b == 2) {
            // Backward divergence treats rows 0 and size-2 as normal faces.
            x[IX(i, 0)] = 0.0f;
            x[IX(i, size - 2)] = 0.0f;
            x[IX(i, size - 1)] = 0.0f;
        } else {
            x[IX(i, 0)] = x[IX(i, 1)];
            x[IX(i, size - 1)] = x[IX(i, size - 2)];
        }
    }
    
    // Left and right boundaries
    for (int i = 1; i < size - 1; i++) {
        if (b == 1) {
            // Backward divergence treats columns 0 and size-2 as normal faces.
            x[IX(0, i)] = 0.0f;
            x[IX(size - 2, i)] = 0.0f;
            x[IX(size - 1, i)] = 0.0f;
        } else {
            x[IX(0, i)] = x[IX(1, i)];
            x[IX(size - 1, i)] = x[IX(size - 2, i)];
        }
    }
    
    if (b == 1 || b == 2) {
        x[IX(0, 0)] = 0.0f;
        x[IX(0, size - 1)] = 0.0f;
        x[IX(size - 1, 0)] = 0.0f;
        x[IX(size - 1, size - 1)] = 0.0f;
    } else {
        // Scalar corner cells average their two adjacent boundary samples.
        x[IX(0, 0)] = 0.5f * (x[IX(1, 0)] + x[IX(0, 1)]);
        x[IX(0, size - 1)] = 0.5f *
            (x[IX(1, size - 1)] + x[IX(0, size - 2)]);
        x[IX(size - 1, 0)] = 0.5f *
            (x[IX(size - 2, 0)] + x[IX(size - 1, 1)]);
        x[IX(size - 1, size - 1)] = 0.5f *
            (x[IX(size - 2, size - 1)] + x[IX(size - 1, size - 2)]);
    }
}

/**
 * Linear solver using Gauss-Seidel or successive over-relaxation.
 * 
 * Solves the system: x[i] = (x0[i] + a * sum(neighbors)) / c
 * 
 * This is used for:
 * 1. Implicit diffusion (large timesteps stable)
 * 2. Poisson equation in projection step
 * 
 * Cache-friendly row-major iteration for spatial locality.
 * The caller selects the iteration count for diffusion or pressure solves.
 */
bool FluidSim::lin_solve(int b, std::vector<float>& x,
                         const std::vector<float>& x0,
                         float a, float c, int maxIterations, float relaxation,
                         float residualTolerance) {
    const float cRecip = 1.0f / c;
    
    for (int k = 0; k < maxIterations; k++) {
        // Row-major order for cache efficiency
        for (int j = 1; j < size - 1; j++) {
            for (int i = 1; i < size - 1; i++) {
                // Consume updated neighbors immediately (Gauss-Seidel/SOR).
                const int index = IX(i, j);
                const float solved = (x0[index]
                    + a * (x[IX(i + 1, j)] 
                         + x[IX(i - 1, j)] 
                         + x[IX(i, j + 1)] 
                         + x[IX(i, j - 1)])) * cRecip;
                x[index] += relaxation * (solved - x[index]);
            }
        }
        set_bnd(b, x);

        // Pressure uses a direct Poisson-residual stopping criterion. Checking
        // every fourth sweep limits overhead while keeping the tolerance tight.
        const bool shouldCheckResidual = residualTolerance > 0.0f &&
            (((k + 1) % 4 == 0) || k + 1 == maxIterations);
        if (shouldCheckResidual) {
            float maximumResidual = 0.0f;
            for (int j = 1; j < size - 1; ++j) {
                for (int i = 1; i < size - 1; ++i) {
                    const int index = IX(i, j);
                    const float residual = x0[index] + a * (
                        x[IX(i + 1, j)] + x[IX(i - 1, j)] +
                        x[IX(i, j + 1)] + x[IX(i, j - 1)]) -
                        c * x[index];
                    maximumResidual = std::max(maximumResidual,
                                               std::abs(residual));
                }
            }
            if (maximumResidual <= residualTolerance) {
                return true;
            }
        }
    }

    return residualTolerance <= 0.0f;
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
    if (diff == 0.0f) {
        x = x0;
        set_bnd(b, x);
        return;
    }

    float a = dt * diff * (size - 2) * (size - 2);
    lin_solve(b, x, x0, a, 1.0f + 4.0f * a,
              DIFFUSION_SOLVER_ITERATIONS, 1.0f);
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
void FluidSim::project(std::vector<float>& velocityX,
                       std::vector<float>& velocityY,
                       std::vector<float>& pressureField,
                       std::vector<float>& divergenceField) {
    const float h = 1.0f / (size - 2);
    bool hasDivergence = false;
    
    // Step 1: Backward divergence paired with the forward pressure gradient.
    // This makes the discrete divergence and gradient algebraically compatible.
    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            divergenceField[IX(i, j)] = -h * (
                velocityX[IX(i, j)] - velocityX[IX(i - 1, j)] +
                velocityY[IX(i, j)] - velocityY[IX(i, j - 1)]
            );
            hasDivergence = hasDivergence ||
                divergenceField[IX(i, j)] != 0.0f;
            pressureField[IX(i, j)] = 0.0f;
        }
    }
    
    set_bnd(0, divergenceField);
    set_bnd(0, pressureField);

    if (!hasDivergence) {
        set_bnd(1, velocityX);
        set_bnd(2, velocityY);
        return;
    }
    
    // Step 2: Solve Poisson equation for pressure
    const bool pressureConverged = lin_solve(
        0, pressureField, divergenceField, 1.0f, 4.0f,
        PRESSURE_SOLVER_MAX_ITERATIONS, PRESSURE_RELAXATION,
        h * PROJECTION_DIVERGENCE_TOLERANCE);
    if (!pressureConverged) {
        throw std::runtime_error(
            "Pressure solve did not converge within " +
            std::to_string(PRESSURE_SOLVER_MAX_ITERATIONS) +
            " iterations to the cell-scaled divergence target " +
            std::to_string(PROJECTION_DIVERGENCE_TOLERANCE));
    }
    
    // Step 3: Subtract the matching forward pressure gradient from velocity.
    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            velocityX[IX(i, j)] -=
                (pressureField[IX(i + 1, j)] - pressureField[IX(i, j)]) / h;
            velocityY[IX(i, j)] -=
                (pressureField[IX(i, j + 1)] - pressureField[IX(i, j)]) / h;
        }
    }
    
    set_bnd(1, velocityX);
    set_bnd(2, velocityY);
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
void FluidSim::computeDivergence(const std::vector<float>& velocityX,
                                 const std::vector<float>& velocityY,
                                 std::vector<float>& output) {
    const float h = 1.0f / (size - 2);

    std::fill(output.begin(), output.end(), 0.0f);
    
    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            output[IX(i, j)] = (
                (velocityX[IX(i, j)] - velocityX[IX(i - 1, j)]) +
                (velocityY[IX(i, j)] - velocityY[IX(i, j - 1)])
            ) / h;
        }
    }

    set_bnd(0, output);
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
    const float h = 1.0f / (size - 2);
    std::fill(vorticity.begin(), vorticity.end(), 0.0f);

    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            float dVydx = 0.5f *
                (Vy[IX(i + 1, j)] - Vy[IX(i - 1, j)]) / h;
            float dVxdy = 0.5f *
                (Vx[IX(i, j + 1)] - Vx[IX(i, j - 1)]) / h;
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
float FluidSim::sanitizeTimestep(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return 0.0f;
    }
    return std::min(dt, MAX_SIMULATION_TIMESTEP);
}

void FluidSim::step(float dt) {
    dt = sanitizeTimestep(dt);

    if (dt == 0.0f) {
        return;
    }
    
    // Velocity step
    diffuse(1, Vx0, Vx, visc, dt);
    diffuse(2, Vy0, Vy, visc, dt);
    
    project(Vx0, Vy0, pressure, divergence);
    
    advect(1, Vx, Vx0, Vx0, Vy0, dt);
    advect(2, Vy, Vy0, Vx0, Vy0, dt);
    
    project(Vx, Vy, pressure, divergence);
    
    // Density step
    diffuse(0, s, density, diff, dt);
    advect(0, density, s, Vx, Vy, dt);
    
    // Fade density slightly over time
    fadeDensity(dt);
    
    // Compute auxiliary fields for visualization
    computeDivergence(Vx, Vy, divergence);
    computeVorticityField();
}
