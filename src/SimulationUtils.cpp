#include "SimulationUtils.h"
#include "FluidSim.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

bool mapWindowToGrid(double windowX, double windowY,
                     int windowWidth, int windowHeight, int gridSize,
                     GridCoordinate& result) {
    result = {-1, -1};

    if (windowWidth <= 0 || windowHeight <= 0 || gridSize <= 0 ||
        !std::isfinite(windowX) || !std::isfinite(windowY)) {
        return false;
    }

    if (windowX < 0.0 || windowX > static_cast<double>(windowWidth) ||
        windowY < 0.0 || windowY > static_cast<double>(windowHeight)) {
        return false;
    }

    const double normalizedX = windowX / static_cast<double>(windowWidth);
    const double normalizedY =
        (static_cast<double>(windowHeight) - windowY) /
        static_cast<double>(windowHeight);

    const int mappedX = static_cast<int>(normalizedX * gridSize);
    const int mappedY = static_cast<int>(normalizedY * gridSize);
    result.x = std::max(0, std::min(gridSize - 1, mappedX));
    result.y = std::max(0, std::min(gridSize - 1, mappedY));
    return true;
}

bool calculatePointerVelocityRate(double deltaX, double deltaY,
                                  int windowWidth, int windowHeight,
                                  double elapsedSeconds, float coupling,
                                  float maximumNormalizedSpeed,
                                  float& velocityRateX,
                                  float& velocityRateY) {
    velocityRateX = 0.0f;
    velocityRateY = 0.0f;

    if (windowWidth <= 0 || windowHeight <= 0 || elapsedSeconds <= 0.0 ||
        !std::isfinite(deltaX) || !std::isfinite(deltaY) ||
        !std::isfinite(elapsedSeconds) || !std::isfinite(coupling) ||
        !std::isfinite(maximumNormalizedSpeed) || coupling < 0.0f ||
        maximumNormalizedSpeed <= 0.0f) {
        return false;
    }

    double normalizedVelocityX =
        deltaX / static_cast<double>(windowWidth) / elapsedSeconds;
    double normalizedVelocityY =
        -deltaY / static_cast<double>(windowHeight) / elapsedSeconds;
    const double speed = std::hypot(normalizedVelocityX, normalizedVelocityY);
    if (!std::isfinite(speed)) {
        return false;
    }

    if (speed > maximumNormalizedSpeed) {
        const double scale = maximumNormalizedSpeed / speed;
        normalizedVelocityX *= scale;
        normalizedVelocityY *= scale;
    }

    const double rateX = normalizedVelocityX * coupling;
    const double rateY = normalizedVelocityY * coupling;
    if (!std::isfinite(rateX) || !std::isfinite(rateY) ||
        std::abs(rateX) > std::numeric_limits<float>::max() ||
        std::abs(rateY) > std::numeric_limits<float>::max()) {
        return false;
    }

    velocityRateX = static_cast<float>(rateX);
    velocityRateY = static_cast<float>(rateY);
    return true;
}

void applyDensityBrush(FluidSim& fluid, const GridCoordinate& center, float dt) {
    for (int offsetY = -1; offsetY <= 1; ++offsetY) {
        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
            const float densityRate = DENSITY_INJECTION_RATE *
                ((offsetX == 0 && offsetY == 0)
                    ? CENTER_DENSITY_RATE_SCALE
                    : 1.0f);
            fluid.addDensityRate(center.x + offsetX, center.y + offsetY,
                                 densityRate, dt);
        }
    }
}

void applyVelocityBrush(FluidSim& fluid, const GridCoordinate& center,
                        float velocityRateX, float velocityRateY, float dt) {
    for (int offsetY = -1; offsetY <= 1; ++offsetY) {
        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
            fluid.addVelocityRate(center.x + offsetX, center.y + offsetY,
                                  velocityRateX, velocityRateY, dt);
        }
    }
}

std::size_t checkedGridElementCount(int gridSize) {
    if (gridSize <= 0) {
        throw std::invalid_argument("Texture grid size must be positive");
    }

    const std::size_t size = static_cast<std::size_t>(gridSize);
    if (size > std::numeric_limits<std::size_t>::max() / size) {
        throw std::overflow_error("Texture grid element count overflow");
    }
    return size * size;
}

void validateTextureDataSize(std::size_t actualSize, int gridSize) {
    const std::size_t expectedSize = checkedGridElementCount(gridSize);
    if (actualSize != expectedSize) {
        std::ostringstream message;
        message << "Texture data size mismatch: expected " << expectedSize
                << " values, received " << actualSize;
        throw std::invalid_argument(message.str());
    }
}

std::vector<float> prepareTextureData(const std::vector<float>& data,
                                      int gridSize,
                                      TextureFieldKind kind) {
    validateTextureDataSize(data.size(), gridSize);

    for (std::size_t i = 0; i < data.size(); ++i) {
        if (!std::isfinite(data[i])) {
            std::ostringstream message;
            message << "Texture data contains a non-finite value at index " << i;
            throw std::invalid_argument(message.str());
        }
    }

    if (kind == TextureFieldKind::DENSITY) {
        for (std::size_t i = 0; i < data.size(); ++i) {
            if (data[i] < 0.0f || data[i] > 1.0f) {
                std::ostringstream message;
                message << "Density texture value at index " << i
                        << " is outside the [0, 1] simulation range";
                throw std::invalid_argument(message.str());
            }
        }
        return data;
    }

    const auto minimum = std::min_element(data.begin(), data.end());
    const auto maximum = std::max_element(data.begin(), data.end());
    const float maxAbs = std::max(std::abs(*minimum), std::abs(*maximum));

    std::vector<float> normalized(data.size(), 0.0f);
    if (maxAbs <= std::numeric_limits<float>::epsilon()) {
        return normalized;
    }

    for (std::size_t i = 0; i < data.size(); ++i) {
        normalized[i] = data[i] / maxAbs;
    }
    return normalized;
}
