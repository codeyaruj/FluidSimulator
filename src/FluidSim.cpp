#include "FluidSim.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

FluidSim::FluidSim(float diffusionRate, float viscosityRate)
    : size(GRID_SIZE),
      diffusion(diffusionRate),
      viscosity(viscosityRate) {
    const int cellCount = size * size;

    densityPrevious.resize(cellCount, 0.0f);
    density.resize(cellCount, 0.0f);
    velocityX.resize(cellCount, 0.0f);
    velocityY.resize(cellCount, 0.0f);
    velocityXPrevious.resize(cellCount, 0.0f);
    velocityYPrevious.resize(cellCount, 0.0f);
    pressure.resize(cellCount, 0.0f);
    divergence.resize(cellCount, 0.0f);
    vorticity.resize(cellCount, 0.0f);
}

FluidSim::~FluidSim() = default;

void FluidSim::addDensity(int x, int y, float amount) {
    if (x < 1 || x >= size - 1 || y < 1 || y >= size - 1) {
        return;
    }
    if (!std::isfinite(amount)) {
        return;
    }

    const int cell = index(x, y);
    density[cell] = std::max(
        MIN_DENSITY, std::min(MAX_DENSITY, density[cell] + amount));
}

void FluidSim::addVelocity(int x, int y, float amountX, float amountY) {
    if (x < 1 || x >= size - 1 || y < 1 || y >= size - 1) {
        return;
    }
    if (!std::isfinite(amountX) || !std::isfinite(amountY)) {
        return;
    }

    const int cell = index(x, y);
    const float nextX = velocityX[cell] + amountX;
    const float nextY = velocityY[cell] + amountY;
    if (std::isfinite(nextX) && std::isfinite(nextY)) {
        velocityX[cell] = nextX;
        velocityY[cell] = nextY;
    }
}

void FluidSim::fadeDensity(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return;
    }
    dt = std::min(dt, MAX_SIMULATION_TIMESTEP);

    const float fadeAmount = DENSITY_FADE_RATE * dt;
    const int cellCount = size * size;
    for (int i = 0; i < cellCount; i++) {
        density[i] = std::max(MIN_DENSITY, density[i] - fadeAmount);
    }
}

void FluidSim::setBoundary(int boundaryType, std::vector<float>& field) {
    for (int i = 1; i < size - 1; i++) {
        if (boundaryType == 2) {
            // Backward divergence treats rows 0 and size-2 as normal faces.
            field[index(i, 0)] = 0.0f;
            field[index(i, size - 2)] = 0.0f;
            field[index(i, size - 1)] = 0.0f;
        } else {
            field[index(i, 0)] = field[index(i, 1)];
            field[index(i, size - 1)] = field[index(i, size - 2)];
        }
    }

    for (int i = 1; i < size - 1; i++) {
        if (boundaryType == 1) {
            // Backward divergence treats columns 0 and size-2 as normal faces.
            field[index(0, i)] = 0.0f;
            field[index(size - 2, i)] = 0.0f;
            field[index(size - 1, i)] = 0.0f;
        } else {
            field[index(0, i)] = field[index(1, i)];
            field[index(size - 1, i)] = field[index(size - 2, i)];
        }
    }

    if (boundaryType == 1 || boundaryType == 2) {
        field[index(0, 0)] = 0.0f;
        field[index(0, size - 1)] = 0.0f;
        field[index(size - 1, 0)] = 0.0f;
        field[index(size - 1, size - 1)] = 0.0f;
    } else {
        field[index(0, 0)] =
            0.5f * (field[index(1, 0)] + field[index(0, 1)]);
        field[index(0, size - 1)] = 0.5f *
            (field[index(1, size - 1)] + field[index(0, size - 2)]);
        field[index(size - 1, 0)] = 0.5f *
            (field[index(size - 2, 0)] + field[index(size - 1, 1)]);
        field[index(size - 1, size - 1)] = 0.5f *
            (field[index(size - 2, size - 1)] +
             field[index(size - 1, size - 2)]);
    }
}

bool FluidSim::linearSolve(int boundaryType,
                           std::vector<float>& output,
                           const std::vector<float>& input,
                           float a,
                           float c,
                           int maxIterations,
                           float relaxation,
                           float residualTolerance) {
    const float inverseC = 1.0f / c;

    for (int k = 0; k < maxIterations; k++) {
        for (int j = 1; j < size - 1; j++) {
            for (int i = 1; i < size - 1; i++) {
                const int cell = index(i, j);
                const float solved = (
                    input[cell] +
                    a * (output[index(i + 1, j)] +
                         output[index(i - 1, j)] +
                         output[index(i, j + 1)] +
                         output[index(i, j - 1)])) * inverseC;
                output[cell] += relaxation * (solved - output[cell]);
            }
        }
        setBoundary(boundaryType, output);

        // Pressure checks convergence every four sweeps.
        const bool shouldCheckResidual = residualTolerance > 0.0f &&
            (((k + 1) % 4 == 0) || k + 1 == maxIterations);
        if (shouldCheckResidual) {
            float maximumResidual = 0.0f;
            for (int j = 1; j < size - 1; ++j) {
                for (int i = 1; i < size - 1; ++i) {
                    const int cell = index(i, j);
                    const float residual = input[cell] + a * (
                        output[index(i + 1, j)] +
                        output[index(i - 1, j)] +
                        output[index(i, j + 1)] +
                        output[index(i, j - 1)]) - c * output[cell];
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

void FluidSim::diffuse(int boundaryType,
                       std::vector<float>& output,
                       const std::vector<float>& input,
                       float diffusionRate,
                       float dt) {
    if (diffusionRate == 0.0f) {
        output = input;
        setBoundary(boundaryType, output);
        return;
    }

    const float a = dt * diffusionRate * (size - 2) * (size - 2);
    linearSolve(boundaryType, output, input, a, 1.0f + 4.0f * a,
                DIFFUSION_SOLVER_ITERATIONS, 1.0f);
}

void FluidSim::project(std::vector<float>& velocityX,
                       std::vector<float>& velocityY,
                       std::vector<float>& pressureField,
                       std::vector<float>& divergenceField) {
    const float h = 1.0f / (size - 2);
    bool hasDivergence = false;

    // This backward difference must match the forward gradient below.
    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            const int cell = index(i, j);
            divergenceField[cell] = -h * (
                velocityX[cell] - velocityX[index(i - 1, j)] +
                velocityY[cell] - velocityY[index(i, j - 1)]
            );
            hasDivergence = hasDivergence || divergenceField[cell] != 0.0f;
            pressureField[cell] = 0.0f;
        }
    }

    setBoundary(0, divergenceField);
    setBoundary(0, pressureField);

    if (!hasDivergence) {
        setBoundary(1, velocityX);
        setBoundary(2, velocityY);
        return;
    }

    const bool pressureConverged = linearSolve(
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

    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            const int cell = index(i, j);
            velocityX[cell] -=
                (pressureField[index(i + 1, j)] - pressureField[cell]) / h;
            velocityY[cell] -=
                (pressureField[index(i, j + 1)] - pressureField[cell]) / h;
        }
    }

    setBoundary(1, velocityX);
    setBoundary(2, velocityY);
}

void FluidSim::advect(int boundaryType,
                      std::vector<float>& output,
                      const std::vector<float>& input,
                      const std::vector<float>& sourceVelocityX,
                      const std::vector<float>& sourceVelocityY,
                      float dt) {
    const float gridDt = dt * (size - 2);

    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            const int cell = index(i, j);
            float x = i - gridDt * sourceVelocityX[cell];
            float y = j - gridDt * sourceVelocityY[cell];

            x = std::max(0.5f, std::min(x, size - 1.5f));
            const int left = static_cast<int>(x);
            const int right = left + 1;

            y = std::max(0.5f, std::min(y, size - 1.5f));
            const int bottom = static_cast<int>(y);
            const int top = bottom + 1;

            const float rightWeight = x - left;
            const float leftWeight = 1.0f - rightWeight;
            const float topWeight = y - bottom;
            const float bottomWeight = 1.0f - topWeight;

            output[cell] =
                leftWeight *
                    (bottomWeight * input[index(left, bottom)] +
                     topWeight * input[index(left, top)]) +
                rightWeight *
                    (bottomWeight * input[index(right, bottom)] +
                     topWeight * input[index(right, top)]);
        }
    }

    setBoundary(boundaryType, output);
}

void FluidSim::computeDivergence(const std::vector<float>& velocityX,
                                 const std::vector<float>& velocityY,
                                 std::vector<float>& output) {
    const float h = 1.0f / (size - 2);

    std::fill(output.begin(), output.end(), 0.0f);

    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            const int cell = index(i, j);
            output[cell] = (
                (velocityX[cell] - velocityX[index(i - 1, j)]) +
                (velocityY[cell] - velocityY[index(i, j - 1)])
            ) / h;
        }
    }

    setBoundary(0, output);
}

void FluidSim::computeVorticityField() {
    const float h = 1.0f / (size - 2);
    std::fill(vorticity.begin(), vorticity.end(), 0.0f);

    for (int j = 1; j < size - 1; j++) {
        for (int i = 1; i < size - 1; i++) {
            const float velocityYChange = 0.5f *
                (velocityY[index(i + 1, j)] -
                 velocityY[index(i - 1, j)]) / h;
            const float velocityXChange = 0.5f *
                (velocityX[index(i, j + 1)] -
                 velocityX[index(i, j - 1)]) / h;
            vorticity[index(i, j)] =
                velocityYChange - velocityXChange;
        }
    }
}

void FluidSim::step(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) {
        return;
    }
    dt = std::min(dt, MAX_SIMULATION_TIMESTEP);

    diffuse(1, velocityXPrevious, velocityX, viscosity, dt);
    diffuse(2, velocityYPrevious, velocityY, viscosity, dt);

    project(velocityXPrevious, velocityYPrevious, pressure, divergence);

    advect(1, velocityX, velocityXPrevious,
            velocityXPrevious, velocityYPrevious, dt);
    advect(2, velocityY, velocityYPrevious,
            velocityXPrevious, velocityYPrevious, dt);

    project(velocityX, velocityY, pressure, divergence);

    diffuse(0, densityPrevious, density, diffusion, dt);
    advect(0, density, densityPrevious, velocityX, velocityY, dt);

    fadeDensity(dt);

    computeDivergence(velocityX, velocityY, divergence);
    computeVorticityField();
}
