#include "FluidSim.h"
#include "SimulationUtils.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

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

void expectNear(float actual, float expected, float tolerance,
                const std::string& message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        fail(message + " (expected " + std::to_string(expected) +
             ", got " + std::to_string(actual) + ")");
    }
}

void expectThrows(const std::function<void()>& operation,
                  const std::string& message) {
    try {
        operation();
        fail(message + " (no exception was thrown)");
    } catch (const std::exception&) {
        // Expected.
    }
}

void expectThrowsContaining(const std::function<void()>& operation,
                            const std::string& expectedText,
                            const std::string& message) {
    try {
        operation();
        fail(message + " (no exception was thrown)");
    } catch (const std::exception& error) {
        if (std::string(error.what()).find(expectedText) == std::string::npos) {
            fail(message + " (diagnostic was: " + error.what() + ")");
        }
    }
}

void runTest(const std::string& name, const std::function<void()>& test) {
    const int failuresBefore = failureCount;
    std::cout << "RUN: " << name << std::endl;
    try {
        test();
    } catch (const std::exception& error) {
        fail(name + " threw unexpectedly: " + error.what());
    } catch (...) {
        fail(name + " threw an unknown exception");
    }

    if (failureCount == failuresBefore) {
        std::cout << "PASS: " << name << '\n';
    }
}

std::size_t indexOf(int x, int y, int size) {
    return static_cast<std::size_t>(x + y * size);
}

float maxCellScaledDivergence(const FluidSim& fluid) {
    const int size = fluid.getSize();
    const std::vector<float>& velocityX = fluid.getVx();
    const std::vector<float>& velocityY = fluid.getVy();
    float maximum = 0.0f;

    for (float value : velocityX) {
        if (!std::isfinite(value)) {
            return std::numeric_limits<float>::infinity();
        }
    }
    for (float value : velocityY) {
        if (!std::isfinite(value)) {
            return std::numeric_limits<float>::infinity();
        }
    }

    // Use the same backward-divergence/forward-gradient pair as projection.
    // The outer ring contains ghost cells, so score only solver cells.
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
    const int size = fluid.getSize();
    double total = 0.0;
    for (int y = 1; y < size - 1; ++y) {
        for (int x = 1; x < size - 1; ++x) {
            total += fluid.getVx()[indexOf(x, y, size)] -
                     fluid.getVx()[indexOf(x - 1, y, size)] +
                     fluid.getVy()[indexOf(x, y, size)] -
                     fluid.getVy()[indexOf(x, y - 1, size)];
        }
    }
    return total;
}

void verifyProjectedBrush(const GridCoordinate& center,
                          float velocityRateX, float velocityRateY,
                          const std::string& label) {
    FluidSim fluid(0.0f, VISCOSITY);
    applyVelocityBrush(fluid, center, velocityRateX, velocityRateY,
                       MAX_SIMULATION_TIMESTEP);
    const float before = maxCellScaledDivergence(fluid);
    fluid.step(MAX_SIMULATION_TIMESTEP);
    const float after = maxCellScaledDivergence(fluid);

    std::cout << "  projection divergence: " << before << " -> " << after
              << " (" << label << ", cell-scaled)\n";
    expectTrue(std::isfinite(after),
               label + " projected field must contain only finite velocities");
    expectTrue(after < 0.020f,
               label + " projected field must stay below 0.020");
    expectTrue(after < before * 0.05f,
               label + " projection must remove at least 95% of divergence");
}

void testProjectionReducesDivergence() {
    FluidSim fluid(0.0f, 0.0f);
    const int center = fluid.getSize() / 2;
    constexpr float sigma = 2.0f;
    constexpr float amplitude = 0.1f;

    // A smooth radial source remains divergent after advection alone, making
    // this a regression for both projection calls rather than for dissipation.
    for (int y = 1; y < fluid.getSize() - 1; ++y) {
        for (int x = 1; x < fluid.getSize() - 1; ++x) {
            const float dx = static_cast<float>(x - center);
            const float dy = static_cast<float>(y - center);
            const float exponent = -(dx * dx + dy * dy) /
                                   (2.0f * sigma * sigma);
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
    expectTrue(std::isfinite(after),
               "projected smooth field must contain only finite velocities");
    expectTrue(after < 0.020f,
               "projected field must stay below the documented 0.020 tolerance");
    expectTrue(after < before * 0.25f,
               "projection must reduce maximum divergence by at least 75%");

    constexpr float maximumRate =
        MOUSE_VELOCITY_COUPLING * MAX_NORMALIZED_MOUSE_SPEED;
    const float diagonalRate = maximumRate / std::sqrt(2.0f);

    verifyProjectedBrush({center, center}, maximumRate, 0.0f,
                         "center maximum-rate brush");
    verifyProjectedBrush({center, center}, diagonalRate, diagonalRate,
                         "center diagonal maximum-rate brush");
    verifyProjectedBrush({0, center}, maximumRate, 0.0f,
                         "left-wall brush");
    verifyProjectedBrush({1, center}, maximumRate, 0.0f,
                         "left-adjacent brush");
    verifyProjectedBrush({GRID_SIZE - 2, center}, maximumRate, 0.0f,
                         "right-adjacent brush");
    verifyProjectedBrush({GRID_SIZE - 1, center}, maximumRate, 0.0f,
                         "right-wall brush");
    verifyProjectedBrush({center, 0}, 0.0f, maximumRate,
                         "bottom-wall brush");
    verifyProjectedBrush({center, GRID_SIZE - 1}, 0.0f, maximumRate,
                         "top-wall brush");
    verifyProjectedBrush({0, 0}, diagonalRate, diagonalRate,
                         "bottom-left corner brush");
    verifyProjectedBrush({GRID_SIZE - 1, GRID_SIZE - 1},
                         diagonalRate, diagonalRate,
                         "top-right corner brush");
    constexpr float nearCornerAngle =
        78.75f * 3.14159265358979323846f / 180.0f;
    verifyProjectedBrush({GRID_SIZE - 3, 2},
                         maximumRate * std::cos(nearCornerAngle),
                         maximumRate * std::sin(nearCornerAngle),
                         "near-corner oblique maximum-rate brush");

    FluidSim uniformFlow(0.0f, VISCOSITY);
    for (int y = 1; y < GRID_SIZE - 1; ++y) {
        for (int x = 1; x < GRID_SIZE - 1; ++x) {
            uniformFlow.addVelocity(x, y, 10.0f, 0.0f);
        }
    }
    const float uniformBefore = maxCellScaledDivergence(uniformFlow);
    uniformFlow.step(1.0e-6f);
    const float uniformAfter = maxCellScaledDivergence(uniformFlow);
    const double uniformTotal = totalCellScaledDivergence(uniformFlow);
    std::cout << "  projection divergence: " << uniformBefore << " -> "
              << uniformAfter << " (domain-wide flow, cell-scaled)\n";
    expectTrue(uniformAfter < 0.020f,
               "domain-wide flow must meet the projection tolerance");
    expectTrue(std::abs(uniformTotal) < 1.0e-5,
               "projected domain-wide flow must have zero net boundary flux");
}

void expectVectorNear(const std::vector<float>& actual,
                      const std::vector<float>& expected,
                      const std::string& message) {
    expectTrue(actual.size() == expected.size(), message + " size mismatch");
    if (actual.size() != expected.size()) {
        return;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        expectNear(actual[i], expected[i], 1.0e-6f,
                   message + " at index " + std::to_string(i));
        expectTrue(std::isfinite(actual[i]), message + " must remain finite");
        expectTrue(actual[i] >= -1.0f && actual[i] <= 1.0f,
                   message + " must remain in [-1, 1]");
    }
}

void testSignedFieldNormalization() {
    expectVectorNear(
        prepareTextureData({0.0f, 0.0f, 0.0f, 0.0f}, 2,
                           TextureFieldKind::SIGNED),
        {0.0f, 0.0f, 0.0f, 0.0f}, "all-zero normalization");
    expectVectorNear(
        prepareTextureData({1.0f, 2.0f, 4.0f, 0.5f}, 2,
                           TextureFieldKind::SIGNED),
        {0.25f, 0.5f, 1.0f, 0.125f}, "positive-only normalization");
    expectVectorNear(
        prepareTextureData({-1.0f, -2.0f, -4.0f, -0.5f}, 2,
                           TextureFieldKind::SIGNED),
        {-0.25f, -0.5f, -1.0f, -0.125f}, "negative-only normalization");
    expectVectorNear(
        prepareTextureData({-2.0f, 1.0f, 0.0f, 0.5f}, 2,
                           TextureFieldKind::SIGNED),
        {-1.0f, 0.5f, 0.0f, 0.25f}, "mixed-sign normalization");
    expectVectorNear(
        prepareTextureData({-8.0f, 2.0f, 0.0f, -4.0f}, 2,
                           TextureFieldKind::SIGNED),
        {-1.0f, 0.25f, 0.0f, -0.5f},
        "largest-negative-magnitude normalization");

    const float tiny = std::numeric_limits<float>::epsilon() * 0.5f;
    expectVectorNear(
        prepareTextureData({tiny, -tiny, 0.0f, tiny}, 2,
                           TextureFieldKind::SIGNED),
        {0.0f, 0.0f, 0.0f, 0.0f}, "near-zero normalization");

    expectThrows([] {
        prepareTextureData(
            {0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
            2, TextureFieldKind::SIGNED);
    }, "NaN texture data must be rejected");
    expectThrows([] {
        prepareTextureData(
            {0.0f, std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
            2, TextureFieldKind::SIGNED);
    }, "infinite texture data must be rejected");
}

void testTextureSizeValidation() {
    const std::vector<float> exact(4, 0.5f);
    const std::vector<float> prepared =
        prepareTextureData(exact, 2, TextureFieldKind::DENSITY);
    expectTrue(prepared == exact, "exact texture input must be accepted");

    expectThrows([] { validateTextureDataSize(0, 2); },
                 "empty texture input must be rejected");
    expectThrowsContaining([] { validateTextureDataSize(3, 2); },
                           "expected 4 values, received 3",
                           "one-short texture input must report both sizes");
    expectThrows([] { validateTextureDataSize(5, 2); },
                 "oversized texture input must be rejected");
    expectThrows([] { validateTextureDataSize(0, 0); },
                 "non-positive texture dimensions must be rejected");
}

void expectCoordinate(double x, double y, int width, int height,
                      int expectedX, int expectedY,
                      const std::string& message) {
    GridCoordinate coordinate;
    expectTrue(mapWindowToGrid(x, y, width, height, GRID_SIZE, coordinate),
               message + " must map successfully");
    expectTrue(coordinate.x == expectedX && coordinate.y == expectedY,
               message + " mapped to (" + std::to_string(coordinate.x) + ", " +
               std::to_string(coordinate.y) + ")");
}

void testCoordinateMapping() {
    expectCoordinate(400.0, 400.0, 800, 800, 64, 64, "window center");
    expectCoordinate(0.0, 0.0, 800, 800, 0, 127, "top-left corner");
    expectCoordinate(800.0, 0.0, 800, 800, 127, 127,
                     "top-right corner");
    expectCoordinate(0.0, 800.0, 800, 800, 0, 0,
                     "bottom-left corner");
    expectCoordinate(800.0, 800.0, 800, 800, 127, 0,
                     "bottom-right corner");

    // A 1600x1200 framebuffer does not enter this logical 800x600 mapping.
    expectCoordinate(600.0, 150.0, 800, 600, 96, 96,
                     "HiDPI logical-window coordinate");

    GridCoordinate coordinate;
    expectTrue(!mapWindowToGrid(-0.01, 10.0, 800, 800, GRID_SIZE, coordinate),
               "coordinate slightly left of the window must be rejected");
    expectTrue(!mapWindowToGrid(800.01, 10.0, 800, 800, GRID_SIZE, coordinate),
               "coordinate slightly right of the window must be rejected");
    expectTrue(!mapWindowToGrid(10.0, -0.01, 800, 800, GRID_SIZE, coordinate),
               "coordinate slightly above the window must be rejected");
    expectTrue(!mapWindowToGrid(10.0, 800.01, 800, 800, GRID_SIZE, coordinate),
               "coordinate slightly below the window must be rejected");
    expectTrue(!mapWindowToGrid(10.0, 10.0, 0, 800, GRID_SIZE, coordinate),
               "zero logical width must be rejected");
    expectTrue(!mapWindowToGrid(10.0, 10.0, 800, 0, GRID_SIZE, coordinate),
               "zero logical height must be rejected");
    expectTrue(!mapWindowToGrid(10.0, 10.0, 0, 0, GRID_SIZE, coordinate),
               "minimized dimensions must be rejected");
    expectTrue(!mapWindowToGrid(std::numeric_limits<double>::infinity(), 10.0,
                                800, 800, GRID_SIZE, coordinate),
               "non-finite cursor coordinates must be rejected");
    expectTrue(coordinate.x == -1 && coordinate.y == -1,
               "failed mapping must not retain stale coordinates");
}

float accumulatedPointerImpulse(int subdivisions) {
    constexpr double totalMotion = 400.0;
    constexpr int windowWidth = 800;
    constexpr int windowHeight = 600;
    const double rawDt = 1.0 / static_cast<double>(subdivisions);
    const float simulationDt = static_cast<float>(rawDt);
    FluidSim fluid(0.0f, 0.0f);
    const GridCoordinate center{64, 64};

    for (int i = 0; i < subdivisions; ++i) {
        float rateX = 0.0f;
        float rateY = 0.0f;
        const bool valid = calculatePointerVelocityRate(
            totalMotion / subdivisions, 0.0, windowWidth, windowHeight,
            rawDt, MOUSE_VELOCITY_COUPLING, MAX_NORMALIZED_MOUSE_SPEED,
            rateX, rateY);
        expectTrue(valid, "ordinary pointer motion must produce a valid rate");
        expectNear(rateY, 0.0f, 0.0f,
                   "horizontal pointer motion must not create Y velocity");
        applyVelocityBrush(fluid, center, rateX, rateY, simulationDt);
    }
    return fluid.getVx()[indexOf(center.x, center.y, fluid.getSize())];
}

void testPointerVelocityRate() {
    const float impulse30 = accumulatedPointerImpulse(30);
    const float impulse60 = accumulatedPointerImpulse(60);
    const float impulse144 = accumulatedPointerImpulse(144);
    expectNear(impulse30, 2.5f, 1.0e-5f, "30 FPS pointer impulse");
    expectNear(impulse60, 2.5f, 1.0e-5f, "60 FPS pointer impulse");
    expectNear(impulse144, 2.5f, 1.0e-5f, "144 FPS pointer impulse");
    expectNear(impulse30, impulse144, 1.0e-5f,
               "pointer impulse must be subdivision-independent");

    float rateX = 0.0f;
    float rateY = 0.0f;
    expectTrue(calculatePointerVelocityRate(800.0, 0.0, 800, 600, 1.0,
                                            MOUSE_VELOCITY_COUPLING,
                                            MAX_NORMALIZED_MOUSE_SPEED,
                                            rateX, rateY),
               "paused-frame pointer rate must remain valid");
    expectNear(rateX * MAX_SIMULATION_TIMESTEP, 0.25f, 1.0e-6f,
               "sanitized timestep must attenuate a one-second paused drag");

    expectTrue(!calculatePointerVelocityRate(1.0, 1.0, 0, 600, 0.01,
                                             MOUSE_VELOCITY_COUPLING,
                                             MAX_NORMALIZED_MOUSE_SPEED,
                                             rateX, rateY),
               "minimized logical dimensions must reject pointer motion");
    expectTrue(!calculatePointerVelocityRate(1.0, 1.0, 800, 600, 0.0,
                                             MOUSE_VELOCITY_COUPLING,
                                             MAX_NORMALIZED_MOUSE_SPEED,
                                             rateX, rateY),
               "zero elapsed time must reject pointer motion");
}

float densityAfterFade(int subdivisions) {
    FluidSim fluid(0.0f, 0.0f);
    fluid.addDensity(64, 64, 1.0f);
    const float dt = 1.0f / static_cast<float>(subdivisions);
    for (int i = 0; i < subdivisions; ++i) {
        fluid.fadeDensity(dt);
    }
    return fluid.getDensity()[indexOf(64, 64, fluid.getSize())];
}

float densityAfterHeldSource(int subdivisions) {
    FluidSim fluid(0.0f, 0.0f);
    const GridCoordinate center{64, 64};
    const float dt = 1.0f / static_cast<float>(subdivisions);
    for (int i = 0; i < subdivisions; ++i) {
        applyDensityBrush(fluid, center, dt);
        fluid.fadeDensity(dt);
    }
    // A neighboring brush cell receives the unsaturated base rate of 1.0/s.
    return fluid.getDensity()[indexOf(65, 64, fluid.getSize())];
}

void testTimeBasedDensity() {
    FluidSim zeroTime(0.0f, 0.0f);
    zeroTime.addDensity(64, 64, 0.5f);
    zeroTime.step(0.0f);
    expectNear(zeroTime.getDensity()[indexOf(64, 64, zeroTime.getSize())],
               0.5f, 0.0f, "zero timestep must not fade density");

    const float fade30 = densityAfterFade(30);
    const float fade60 = densityAfterFade(60);
    const float fade144 = densityAfterFade(144);
    expectNear(fade30, 0.88f, 1.0e-4f, "30 FPS one-second fade");
    expectNear(fade60, 0.88f, 1.0e-4f, "60 FPS one-second fade");
    expectNear(fade144, 0.88f, 1.0e-4f, "144 FPS one-second fade");
    expectNear(fade30, fade144, 1.0e-4f,
               "fade must be subdivision-independent");

    const float source30 = densityAfterHeldSource(30);
    const float source60 = densityAfterHeldSource(60);
    const float source144 = densityAfterHeldSource(144);
    expectNear(source30, 0.88f, 1.0e-4f, "30 FPS held source");
    expectNear(source60, 0.88f, 1.0e-4f, "60 FPS held source");
    expectNear(source144, 0.88f, 1.0e-4f, "144 FPS held source");
    expectNear(source30, source144, 1.0e-4f,
               "held source must be subdivision-independent");

    FluidSim brushRates(0.0f, 0.0f);
    applyDensityBrush(brushRates, GridCoordinate{64, 64}, 0.01f);
    expectNear(brushRates.getDensity()[indexOf(64, 64, brushRates.getSize())],
               0.03f, 1.0e-6f, "brush center must use the documented 3x rate");
    expectNear(brushRates.getDensity()[indexOf(65, 64, brushRates.getSize())],
               0.01f, 1.0e-6f, "brush neighbor must use the base rate");

    FluidSim bounded(0.0f, 0.0f);
    bounded.addDensity(64, 64, 0.001f);
    bounded.fadeDensity(MAX_SIMULATION_TIMESTEP);
    bounded.fadeDensity(MAX_SIMULATION_TIMESTEP);
    for (float value : bounded.getDensity()) {
        expectTrue(std::isfinite(value), "density must remain finite");
        expectTrue(value >= MIN_DENSITY && value <= MAX_DENSITY,
                   "density must remain within [0, 1]");
    }

    bounded.addDensity(64, 64, 5.0f);
    expectNear(bounded.getDensity()[indexOf(64, 64, bounded.getSize())],
               MAX_DENSITY, 0.0f, "density source must honor the [0, 1] cap");

    const float beforeInvalidTime =
        bounded.getDensity()[indexOf(64, 64, bounded.getSize())];
    bounded.fadeDensity(-1.0f);
    bounded.fadeDensity(std::numeric_limits<float>::quiet_NaN());
    bounded.fadeDensity(std::numeric_limits<float>::infinity());
    expectNear(bounded.getDensity()[indexOf(64, 64, bounded.getSize())],
               beforeInvalidTime, 0.0f,
               "invalid timesteps must not change density");
}

void testTimestepValidation() {
    expectNear(FluidSim::sanitizeTimestep(-1.0f), 0.0f, 0.0f,
               "negative timestep");
    expectNear(FluidSim::sanitizeTimestep(0.0f), 0.0f, 0.0f,
               "zero timestep");
    expectNear(FluidSim::sanitizeTimestep(
                   std::numeric_limits<float>::quiet_NaN()),
               0.0f, 0.0f, "NaN timestep");
    expectNear(FluidSim::sanitizeTimestep(
                   std::numeric_limits<float>::infinity()),
               0.0f, 0.0f, "infinite timestep");
    expectNear(FluidSim::sanitizeTimestep(0.02f), 0.02f, 0.0f,
               "ordinary timestep");
    expectNear(FluidSim::sanitizeTimestep(1.0f),
               MAX_SIMULATION_TIMESTEP, 0.0f, "long timestep clamp");
}

} // namespace

int main() {
    runTest("pressure projection reduces divergence",
            testProjectionReducesDivergence);
    runTest("signed-field normalization is symmetric and finite",
            testSignedFieldNormalization);
    runTest("texture upload size validation", testTextureSizeValidation);
    runTest("logical-window coordinate mapping", testCoordinateMapping);
    runTest("frame-rate-independent pointer velocity", testPointerVelocityRate);
    runTest("time-based density fading and injection", testTimeBasedDensity);
    runTest("timestep validation", testTimestepValidation);

    if (failureCount != 0) {
        std::cerr << failureCount << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All FluidSimulation tests passed\n";
    return 0;
}
