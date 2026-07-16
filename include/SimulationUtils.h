#ifndef SIMULATIONUTILS_H
#define SIMULATIONUTILS_H

#include <cstddef>
#include <vector>

class FluidSim;

constexpr float MOUSE_VELOCITY_COUPLING = 5.0f;
constexpr float MAX_NORMALIZED_MOUSE_SPEED = 10.0f; // Window lengths per second.

struct GridCoordinate {
    int x;
    int y;
};

/**
 * Map a logical GLFW window coordinate to the simulation grid.
 * The top-left window corner maps to the top-left grid cell. Coordinates
 * outside the closed window rectangle and invalid dimensions are rejected.
 */
bool mapWindowToGrid(double windowX, double windowY,
                     int windowWidth, int windowHeight, int gridSize,
                     GridCoordinate& result);

/**
 * Convert accumulated logical-window cursor motion into a fluid velocity rate.
 * Screen-space Y is inverted and normalized pointer speed is magnitude-limited.
 */
bool calculatePointerVelocityRate(double deltaX, double deltaY,
                                  int windowWidth, int windowHeight,
                                  double elapsedSeconds, float coupling,
                                  float maximumNormalizedSpeed,
                                  float& velocityRateX,
                                  float& velocityRateY);

/** Apply the production 3x3 mouse brush using timestep-based source rates. */
void applyDensityBrush(FluidSim& fluid, const GridCoordinate& center, float dt);
void applyVelocityBrush(FluidSim& fluid, const GridCoordinate& center,
                        float velocityRateX, float velocityRateY, float dt);

enum class TextureFieldKind {
    DENSITY,
    SIGNED
};

/** Return gridSize squared, rejecting non-positive or overflowing sizes. */
std::size_t checkedGridElementCount(int gridSize);

/** Require an exact grid-sized CPU buffer before an OpenGL texture upload. */
void validateTextureDataSize(std::size_t actualSize, int gridSize);

/**
 * Validate and prepare scalar data for upload. Density remains in [0, 1].
 * Signed data is symmetrically normalized to [-1, 1], with zero kept at zero.
 */
std::vector<float> prepareTextureData(const std::vector<float>& data,
                                      int gridSize,
                                      TextureFieldKind kind);

#endif // SIMULATIONUTILS_H
