// These regression cases were added after revisiting the original project
// and finding several bugs in projection and boundary handling.

#include "FluidSim.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr float kProjectionTolerance = 0.020f;
constexpr float kMaximumFrameVelocityAmount = 2.5f;
constexpr double kBoundaryFluxTolerance = 1.0e-5;

int failureCount = 0;

void fail(const std::string& message) {
    ++failureCount;
    std::cerr << "  FAIL: " << message << '\n';
}

void expectTrue(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

std::size_t indexOf(int x, int y, int size) {
    return static_cast<std::size_t>(x + y * size);
}

bool velocitiesAreFinite(const FluidSim& fluid) {
    const std::vector<float>& velocityX = fluid.getVelocityX();
    const std::vector<float>& velocityY = fluid.getVelocityY();

    for (float value : velocityX) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    for (float value : velocityY) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

float maxCellScaledDivergence(const FluidSim& fluid) {
    if (!velocitiesAreFinite(fluid)) {
        return std::numeric_limits<float>::infinity();
    }

    const int size = fluid.getSize();
    const std::vector<float>& velocityX = fluid.getVelocityX();
    const std::vector<float>& velocityY = fluid.getVelocityY();
    float maximum = 0.0f;

    // Projection uses this backward-divergence/forward-gradient pair. The
    // outer ring contains ghost cells, so only solver cells are measured.
    for (int y = 1; y < size - 1; ++y) {
        for (int x = 1; x < size - 1; ++x) {
            const float divergence =
                velocityX[indexOf(x, y, size)] -
                velocityX[indexOf(x - 1, y, size)] +
                velocityY[indexOf(x, y, size)] -
                velocityY[indexOf(x, y - 1, size)];
            if (!std::isfinite(divergence)) {
                return std::numeric_limits<float>::infinity();
            }
            maximum = std::max(maximum, std::abs(divergence));
        }
    }
    return maximum;
}

double totalCellScaledDivergence(const FluidSim& fluid) {
    if (!velocitiesAreFinite(fluid)) {
        return std::numeric_limits<double>::infinity();
    }

    const int size = fluid.getSize();
    const std::vector<float>& velocityX = fluid.getVelocityX();
    const std::vector<float>& velocityY = fluid.getVelocityY();
    double total = 0.0;

    for (int y = 1; y < size - 1; ++y) {
        for (int x = 1; x < size - 1; ++x) {
            total += velocityX[indexOf(x, y, size)] -
                     velocityX[indexOf(x - 1, y, size)] +
                     velocityY[indexOf(x, y, size)] -
                     velocityY[indexOf(x, y - 1, size)];
        }
    }
    return total;
}

void applyVelocityBrush(FluidSim& fluid, int centerX, int centerY,
                        float amountX, float amountY) {
    // amountX and amountY are already multiplied by the simulation timestep.
    for (int offsetY = -1; offsetY <= 1; ++offsetY) {
        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
            fluid.addVelocity(centerX + offsetX, centerY + offsetY,
                              amountX, amountY);
        }
    }
}

void verifyBoundaryFlux(const FluidSim& fluid, const std::string& label) {
    const double total = totalCellScaledDivergence(fluid);
    expectTrue(std::isfinite(total),
               label + " boundary flux must be finite");
    expectTrue(std::abs(total) < kBoundaryFluxTolerance,
               label + " must have zero net boundary flux");
}

void verifyProjectedBrush(int centerX, int centerY,
                          float amountX, float amountY,
                          const std::string& label) {
    FluidSim fluid(0.0f, VISCOSITY);
    applyVelocityBrush(fluid, centerX, centerY, amountX, amountY);

    const float before = maxCellScaledDivergence(fluid);
    fluid.step(MAX_SIMULATION_TIMESTEP);
    const float after = maxCellScaledDivergence(fluid);

    std::cout << "  projection divergence: " << before << " -> " << after
              << " (" << label << ", cell-scaled)\n";
    expectTrue(std::isfinite(before),
               label + " initial field must contain only finite velocities");
    expectTrue(velocitiesAreFinite(fluid),
               label + " projected field must contain only finite velocities");
    expectTrue(after < kProjectionTolerance,
               label + " projected field must stay below 0.020");
    expectTrue(after < before * 0.05f,
               label + " projection must remove at least 95% of divergence");
    verifyBoundaryFlux(fluid, label);
}

void testSmoothProjection() {
    FluidSim fluid(0.0f, 0.0f);
    const int center = fluid.getSize() / 2;
    constexpr float sigma = 2.0f;
    constexpr float amplitude = 0.1f;

    // The smooth radial source remains divergent after advection alone, so
    // this exercises both projection calls rather than relying on dissipation.
    for (int y = 1; y < fluid.getSize() - 1; ++y) {
        for (int x = 1; x < fluid.getSize() - 1; ++x) {
            const float dx = static_cast<float>(x - center);
            const float dy = static_cast<float>(y - center);
            const float exponent =
                -(dx * dx + dy * dy) / (2.0f * sigma * sigma);
            const float falloff = std::exp(exponent);
            fluid.addVelocity(x, y,
                              amplitude * dx / sigma * falloff,
                              amplitude * dy / sigma * falloff);
        }
    }

    const float before = maxCellScaledDivergence(fluid);
    fluid.step(1.0f / 60.0f);
    const float after = maxCellScaledDivergence(fluid);

    std::cout << "  projection divergence: " << before << " -> " << after
              << " (smooth, cell-scaled)\n";
    expectTrue(std::isfinite(before),
               "initial smooth field must contain only finite velocities");
    expectTrue(velocitiesAreFinite(fluid),
               "projected smooth field must contain only finite velocities");
    expectTrue(after < kProjectionTolerance,
               "projected smooth field must stay below 0.020");
    expectTrue(after < before * 0.25f,
               "projection must reduce smooth-field divergence by at least 75%");
    verifyBoundaryFlux(fluid, "projected smooth field");
}

void testBrushProjection() {
    const int center = GRID_SIZE / 2;
    const float diagonalAmount =
        kMaximumFrameVelocityAmount / std::sqrt(2.0f);

    verifyProjectedBrush(center, center,
                         kMaximumFrameVelocityAmount, 0.0f,
                         "center maximum-amount brush");
    verifyProjectedBrush(center, center,
                         diagonalAmount, diagonalAmount,
                         "center diagonal maximum-amount brush");
    verifyProjectedBrush(0, center,
                         kMaximumFrameVelocityAmount, 0.0f,
                         "left-wall brush");
    verifyProjectedBrush(1, center,
                         kMaximumFrameVelocityAmount, 0.0f,
                         "left-adjacent brush");
    verifyProjectedBrush(GRID_SIZE - 2, center,
                         kMaximumFrameVelocityAmount, 0.0f,
                         "right-adjacent brush");
    verifyProjectedBrush(GRID_SIZE - 1, center,
                         kMaximumFrameVelocityAmount, 0.0f,
                         "right-wall brush");
    verifyProjectedBrush(center, 0,
                         0.0f, kMaximumFrameVelocityAmount,
                         "bottom-wall brush");
    verifyProjectedBrush(center, GRID_SIZE - 1,
                         0.0f, kMaximumFrameVelocityAmount,
                         "top-wall brush");
    verifyProjectedBrush(0, 0,
                         diagonalAmount, diagonalAmount,
                         "bottom-left corner brush");
    verifyProjectedBrush(GRID_SIZE - 1, GRID_SIZE - 1,
                         diagonalAmount, diagonalAmount,
                         "top-right corner brush");

    constexpr float nearCornerAngle =
        78.75f * 3.14159265358979323846f / 180.0f;
    verifyProjectedBrush(GRID_SIZE - 3, 2,
                         kMaximumFrameVelocityAmount *
                             std::cos(nearCornerAngle),
                         kMaximumFrameVelocityAmount *
                             std::sin(nearCornerAngle),
                         "near-corner oblique maximum-amount brush");
}

void testDomainWideFlow() {
    FluidSim fluid(0.0f, VISCOSITY);
    for (int y = 1; y < GRID_SIZE - 1; ++y) {
        for (int x = 1; x < GRID_SIZE - 1; ++x) {
            fluid.addVelocity(x, y, 10.0f, 0.0f);
        }
    }

    const float before = maxCellScaledDivergence(fluid);
    fluid.step(1.0e-6f);
    const float after = maxCellScaledDivergence(fluid);

    std::cout << "  projection divergence: " << before << " -> " << after
              << " (domain-wide flow, cell-scaled)\n";
    expectTrue(std::isfinite(before),
               "initial domain-wide flow must be finite");
    expectTrue(velocitiesAreFinite(fluid),
               "projected domain-wide flow must contain finite velocities");
    expectTrue(after < kProjectionTolerance,
               "domain-wide flow must meet the projection tolerance");
    expectTrue(after < before,
               "projection must reduce domain-wide flow divergence");
    verifyBoundaryFlux(fluid, "projected domain-wide flow");
}

}  // namespace

int runProjectionRegressionTests() {
    failureCount = 0;
    std::cout << "RUN: pressure projection regression\n";

    try {
        testSmoothProjection();
        testBrushProjection();
        testDomainWideFlow();
    } catch (const std::exception& error) {
        fail(std::string("pressure projection regression threw: ") +
             error.what());
    } catch (...) {
        fail("pressure projection regression threw an unknown exception");
    }

    if (failureCount == 0) {
        std::cout << "PASS: pressure projection regression\n";
    }
    return failureCount;
}
