#include "FluidSim.h"
#include "Renderer.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

bool mapWindowToGrid(double x, double y, int width, int height, int gridSize,
                     int& gridX, int& gridY);
bool calculatePointerVelocityRate(double movementX, double movementY,
                                  int width, int height, double elapsedSeconds,
                                  float& velocityX, float& velocityY);
float clampTimestep(float dt);
void addDensityBrush(FluidSim& simulation, int centerX, int centerY, float dt);
void addVelocityBrush(FluidSim& simulation, int centerX, int centerY,
                      float velocityX, float velocityY, float dt);
int runProjectionRegressionTests();

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void checkNear(float actual, float expected, float tolerance,
               const std::string& message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        ++failures;
        std::cerr << "FAIL: " << message << " (expected " << expected
                  << ", got " << actual << ")\n";
    }
}

std::size_t cellIndex(int x, int y, int size) {
    return static_cast<std::size_t>(x + y * size);
}

void testDensity() {
    FluidSim fluid(0.0f, 0.0f);
    std::size_t center = cellIndex(64, 64, fluid.getSize());

    fluid.addDensity(64, 64, 0.25f);
    checkNear(fluid.getDensity()[center], 0.25f, 0.0f,
              "addDensity adds an amount");

    fluid.addDensity(64, 64, 5.0f);
    checkNear(fluid.getDensity()[center], MAX_DENSITY, 0.0f,
              "density is clamped to one");

    fluid.addDensity(64, 64, -5.0f);
    checkNear(fluid.getDensity()[center], MIN_DENSITY, 0.0f,
              "density is clamped to zero");

    FluidSim brushed(0.0f, 0.0f);
    addDensityBrush(brushed, 64, 64, 0.1f);
    checkNear(brushed.getDensity()[center], 0.3f, 0.000001f,
              "density brush scales its center by the timestep");
    checkNear(brushed.getDensity()[cellIndex(65, 64, brushed.getSize())],
              0.1f, 0.000001f,
              "density brush scales neighboring cells by the timestep");
}

float densityAfterOneSecond(int steps, bool addSource) {
    FluidSim fluid(0.0f, 0.0f);
    std::size_t center = cellIndex(64, 64, fluid.getSize());
    float dt = 1.0f / static_cast<float>(steps);

    if (!addSource) {
        fluid.addDensity(64, 64, 1.0f);
    }

    for (int i = 0; i < steps; ++i) {
        if (addSource) {
            addDensityBrush(fluid, 63, 64, dt);
        }
        fluid.fadeDensity(dt);
    }

    return fluid.getDensity()[center];
}

void testDensityTiming() {
    FluidSim zeroTime(0.0f, 0.0f);
    std::size_t center = cellIndex(64, 64, zeroTime.getSize());
    zeroTime.addDensity(64, 64, 0.5f);
    zeroTime.fadeDensity(0.0f);
    checkNear(zeroTime.getDensity()[center], 0.5f, 0.0f,
              "zero time does not fade density");

    float fade30 = densityAfterOneSecond(30, false);
    float fade60 = densityAfterOneSecond(60, false);
    float fade144 = densityAfterOneSecond(144, false);
    checkNear(fade30, 0.88f, 0.0001f, "30 FPS fade");
    checkNear(fade60, 0.88f, 0.0001f, "60 FPS fade");
    checkNear(fade144, 0.88f, 0.0001f, "144 FPS fade");

    float source30 = densityAfterOneSecond(30, true);
    float source60 = densityAfterOneSecond(60, true);
    float source144 = densityAfterOneSecond(144, true);
    checkNear(source30, 0.88f, 0.0001f, "30 FPS source");
    checkNear(source60, 0.88f, 0.0001f, "60 FPS source");
    checkNear(source144, 0.88f, 0.0001f, "144 FPS source");
}

void testTimestepClamping() {
    checkNear(clampTimestep(-1.0f), 0.0f, 0.0f,
              "negative timestep is rejected");
    checkNear(clampTimestep(0.0f), 0.0f, 0.0f,
              "zero timestep is rejected");
    checkNear(clampTimestep(std::numeric_limits<float>::quiet_NaN()),
              0.0f, 0.0f, "NaN timestep is rejected");
    checkNear(clampTimestep(std::numeric_limits<float>::infinity()),
              0.0f, 0.0f, "infinite timestep is rejected");
    checkNear(clampTimestep(0.02f), 0.02f, 0.0f,
              "ordinary timestep is unchanged");
    checkNear(clampTimestep(1.0f), MAX_SIMULATION_TIMESTEP, 0.0f,
              "long timestep is capped");

    FluidSim longStep(0.0f, 0.0f);
    FluidSim cappedStep(0.0f, 0.0f);
    std::size_t center = cellIndex(64, 64, longStep.getSize());

    longStep.addDensity(64, 64, 0.5f);
    cappedStep.addDensity(64, 64, 0.5f);
    longStep.step(1.0f);
    cappedStep.step(MAX_SIMULATION_TIMESTEP);
    checkNear(longStep.getDensity()[center],
              cappedStep.getDensity()[center], 0.0f,
              "long timestep is clamped");

    float before = longStep.getDensity()[center];
    longStep.step(-1.0f);
    longStep.step(std::numeric_limits<float>::quiet_NaN());
    longStep.step(std::numeric_limits<float>::infinity());
    checkNear(longStep.getDensity()[center], before, 0.0f,
              "invalid timesteps do nothing");
}

void checkCoordinate(double x, double y, int width, int height,
                     int expectedX, int expectedY,
                     const std::string& message) {
    int gridX;
    int gridY;
    bool mapped =
        mapWindowToGrid(x, y, width, height, GRID_SIZE, gridX, gridY);
    check(mapped, message + " maps successfully");
    check(gridX == expectedX && gridY == expectedY,
          message + " maps to the expected cell");
}

void testCoordinateMapping() {
    checkCoordinate(400.0, 400.0, 800, 800, 64, 64, "window center");
    checkCoordinate(0.0, 0.0, 800, 800, 0, 127, "top-left");
    checkCoordinate(800.0, 0.0, 800, 800, 127, 127, "top-right");
    checkCoordinate(0.0, 800.0, 800, 800, 0, 0, "bottom-left");
    checkCoordinate(800.0, 800.0, 800, 800, 127, 0, "bottom-right");
    checkCoordinate(600.0, 150.0, 800, 600, 96, 96, "HiDPI logical size");

    int gridX;
    int gridY;
    check(!mapWindowToGrid(-0.01, 10.0, 800, 800, GRID_SIZE,
                           gridX, gridY),
          "left outside coordinate is rejected");
    check(!mapWindowToGrid(800.01, 10.0, 800, 800, GRID_SIZE,
                           gridX, gridY),
          "right outside coordinate is rejected");
    check(!mapWindowToGrid(10.0, -0.01, 800, 800, GRID_SIZE,
                           gridX, gridY),
          "top outside coordinate is rejected");
    check(!mapWindowToGrid(10.0, 800.01, 800, 800, GRID_SIZE,
                           gridX, gridY),
          "bottom outside coordinate is rejected");
    check(!mapWindowToGrid(10.0, 10.0, 0, 800, GRID_SIZE,
                           gridX, gridY),
          "zero window width is rejected");
    check(!mapWindowToGrid(10.0, 10.0, 800, 0, GRID_SIZE,
                           gridX, gridY),
          "zero window height is rejected");
    check(!mapWindowToGrid(std::numeric_limits<double>::infinity(), 10.0,
                           800, 800, GRID_SIZE, gridX, gridY),
          "non-finite coordinate is rejected");
    check(gridX == -1 && gridY == -1,
          "failed mapping clears its output");
}

float pointerImpulseAfterOneSecond(int steps) {
    FluidSim fluid(0.0f, 0.0f);
    float dt = 1.0f / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        float velocityX;
        float velocityY;
        bool valid = calculatePointerVelocityRate(
            400.0 / steps, 0.0, 800, 600, dt, velocityX, velocityY);
        check(valid, "pointer movement produces a velocity");
        addVelocityBrush(fluid, 64, 64, velocityX, velocityY, dt);
    }

    return fluid.getVelocityX()[cellIndex(64, 64, fluid.getSize())];
}

void testPointerVelocity() {
    float impulse30 = pointerImpulseAfterOneSecond(30);
    float impulse60 = pointerImpulseAfterOneSecond(60);
    float impulse144 = pointerImpulseAfterOneSecond(144);

    checkNear(impulse30, 2.5f, 0.00001f, "30 FPS pointer impulse");
    checkNear(impulse60, 2.5f, 0.00001f, "60 FPS pointer impulse");
    checkNear(impulse144, 2.5f, 0.00001f, "144 FPS pointer impulse");

    float velocityX;
    float velocityY;
    check(calculatePointerVelocityRate(
              800.0, 0.0, 800, 600, 0.01, velocityX, velocityY),
          "fast pointer movement produces a capped velocity");
    checkNear(std::hypot(velocityX, velocityY), 50.0f, 0.00001f,
              "pointer speed is capped before timestep scaling");

    FluidSim cappedBrush(0.0f, 0.0f);
    addVelocityBrush(cappedBrush, 64, 64, velocityX, velocityY,
                     MAX_SIMULATION_TIMESTEP);
    checkNear(
        cappedBrush.getVelocityX()[
            cellIndex(64, 64, cappedBrush.getSize())],
        2.5f, 0.00001f, "one-frame pointer impulse is bounded");

    check(!calculatePointerVelocityRate(
              1.0, 1.0, 0, 600, 0.01, velocityX, velocityY),
          "zero window size rejects pointer velocity");
    check(!calculatePointerVelocityRate(
              1.0, 1.0, 800, 600, 0.0, velocityX, velocityY),
          "zero elapsed time rejects pointer velocity");
}

void checkVector(const std::vector<float>& actual,
                 const std::vector<float>& expected,
                 const std::string& message) {
    check(actual.size() == expected.size(), message + " size");
    if (actual.size() != expected.size()) {
        return;
    }

    for (std::size_t i = 0; i < actual.size(); ++i) {
        checkNear(actual[i], expected[i], 0.000001f, message);
        check(actual[i] >= -1.0f && actual[i] <= 1.0f,
              message + " stays in range");
    }
}

void testTexturePreparation() {
    checkVector(
        prepareTextureData({0.0f, 0.0f, 0.0f, 0.0f}, 2, true),
        {0.0f, 0.0f, 0.0f, 0.0f}, "zero signed field");
    checkVector(
        prepareTextureData({1.0f, 2.0f, 4.0f, 0.5f}, 2, true),
        {0.25f, 0.5f, 1.0f, 0.125f}, "positive signed field");
    checkVector(
        prepareTextureData({-1.0f, -2.0f, -4.0f, -0.5f}, 2, true),
        {-0.25f, -0.5f, -1.0f, -0.125f}, "negative signed field");
    checkVector(
        prepareTextureData({-8.0f, 2.0f, 0.0f, -4.0f}, 2, true),
        {-1.0f, 0.25f, 0.0f, -0.5f}, "mixed signed field");

    const float tiny = std::numeric_limits<float>::epsilon() * 0.5f;
    checkVector(
        prepareTextureData({tiny, -tiny, 0.0f, tiny}, 2, true),
        {0.0f, 0.0f, 0.0f, 0.0f}, "near-zero signed field");

    std::vector<float> density(4, 0.5f);
    check(prepareTextureData(density, 2, false) == density,
          "density is not normalized");

    try {
        prepareTextureData(std::vector<float>(3, 0.0f), 2, false);
        check(false, "short texture data is rejected");
    } catch (const std::invalid_argument& error) {
        std::string message = error.what();
        check(message.find("expected 4 values, received 3") !=
                  std::string::npos,
              "texture size error reports both sizes");
    }

    try {
        prepareTextureData(std::vector<float>(5, 0.0f), 2, false);
        check(false, "oversized texture data is rejected");
    } catch (const std::invalid_argument&) {
    }

    try {
        prepareTextureData({}, 0, false);
        check(false, "non-positive texture size is rejected");
    } catch (const std::invalid_argument&) {
    }

    try {
        prepareTextureData(
            {0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
            2, true);
        check(false, "non-finite signed texture is rejected");
    } catch (const std::invalid_argument&) {
    }

    try {
        prepareTextureData(
            {0.0f, std::numeric_limits<float>::infinity(), 0.0f, 0.0f},
            2, true);
        check(false, "infinite signed texture is rejected");
    } catch (const std::invalid_argument&) {
    }
}

}

int main() {
    std::cout << "Running basic tests\n";
    testDensity();
    testDensityTiming();
    testTimestepClamping();
    testCoordinateMapping();
    testPointerVelocity();
    testTexturePreparation();

    failures += runProjectionRegressionTests();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All FluidSimulation tests passed\n";
    return 0;
}
