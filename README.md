# 2D Fluid Simulation

A real-time 2D fluid simulation built using C++ and OpenGL. This project implements a grid-based (Eulerian) physics model to simulate the behavior of fluids like smoke or water with interactive user input.

![Fluid Simulation](https://img.shields.io/badge/OpenGL-3.3-blue)
![C++](https://img.shields.io/badge/C++-14-orange)
![License](https://img.shields.io/badge/license-MIT-green)

## Features

- **Real-time Eulerian Fluid Dynamics**: Solves the Navier-Stokes equations on a fixed grid
- **Interactive Controls**: Add fluid density and velocity with mouse input
- **GPU-Accelerated Rendering**: OpenGL-based visualization with custom shaders
- **Debug Visualization Modes**: View density, divergence, and vorticity fields
- **Smooth Color Gradients**: Beautiful density visualization with dynamic color mapping
- **Optimized Performance**: Efficient grid-based simulation running at 60+ FPS

## Physics Implementation

### Eulerian Grid-Based Approach

Unlike Lagrangian simulations that track individual particles, this Eulerian method divides space into a fixed grid (default: 128×128) and calculates fluid properties for each cell over time.

### Navier-Stokes Solver

The simulation implements a stable fluid solver based on Jos Stam's work, with the following steps:

1. **Advection**: Moves density and velocity along the fluid's flow field using semi-Lagrangian advection
2. **Diffusion**: Spreads fluid properties to neighboring cells (simulating viscosity)
3. **Projection**: Ensures incompressibility by making the velocity field divergence-free
4. **Boundary Conditions**: Handles fluid interaction with container edges

### Mathematical Details

**Velocity Update:**
```
∂u/∂t = -(u·∇)u + ν∇²u - ∇p + f
```

**Density Update:**
```
∂ρ/∂t = -(u·∇)ρ + κ∇²ρ + S
```

Where:
- `u` = velocity field
- `ρ` = density field
- `ν` = viscosity coefficient
- `κ` = diffusion coefficient
- `p` = pressure
- `f` = external forces
- `S` = density sources

## Project Structure

```
fluid_simulation/
├── include/
│   ├── FluidSim.h       # Fluid physics engine header
│   ├── Renderer.h       # OpenGL renderer header
│   └── SimulationUtils.h  # Headless coordinate/texture helpers
├── src/
│   ├── main.cpp         # Application entry point
│   ├── FluidSim.cpp     # Physics implementation
│   ├── Renderer.cpp     # Rendering implementation
│   └── SimulationUtils.cpp
├── tests/
│   └── FluidSimulationTests.cpp
├── CMakeLists.txt       # Build configuration
└── README.md           # This file
```

## Dependencies

- **OpenGL 3.3+**: Graphics API
- **GLFW 3**: Window management and input handling
- **GLEW**: OpenGL extension loader
- A **C++14** compiler or later (`std::make_unique` is used)
- **CMake 3.20** or later

### Ubuntu/Debian Installation

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libglfw3-dev libglew-dev libgl1-mesa-dev
```

### macOS Installation

```bash
brew install cmake glfw glew
```

### Windows Installation

Use [vcpkg](https://vcpkg.io/) or download the libraries manually:
```powershell
vcpkg install glfw3 glew opengl
```

When configuring with vcpkg, pass its CMake toolchain file, for example:

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\FluidSimulation.exe
```

## Building the Project

### Using CMake

For single-configuration generators such as Makefiles or Ninja:

```bash
# From the repository root
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

# Run the simulation
./build/FluidSimulation
```

### Build Types

**Release build:**

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
```

### Tests

Tests are enabled by default through CTest and do not open a GLFW window or
require a graphical display:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Set `-DBUILD_TESTING=OFF` when an application-only build is desired.

For a graphics-dependency-free CI build, disable the application. This builds
and runs the solver/helper tests without finding or linking OpenGL, GLEW, or
GLFW:

```bash
cmake -S . -B build-headless \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFLUIDSIM_BUILD_APP=OFF
cmake --build build-headless --parallel
ctest --test-dir build-headless --output-on-failure
```

The projection regression scores the cell-scaled backward-difference
divergence used by the solver over interior cells (the outer ring contains
boundary/ghost values). The pressure solve uses over-relaxation and a direct
Poisson-residual stopping criterion (up to 2048 sweeps) targeting `0.010`.
A smooth radial field and maximum-rate 3×3 mouse impulses at the center,
walls, corners, and an oblique near-corner case must all finish below the test
ceiling of `0.020`; each source-shaped field must also lose at least 95% of its
initial divergence. A domain-wide velocity-10 flow additionally verifies that
the paired high-side boundary faces converge and produce zero net flux after
projection. Failure to reach the target within the safety cap is reported as
a runtime error rather than silently accepting an unconverged field.

### Sanitizers

Clang and GCC builds can enable AddressSanitizer and
UndefinedBehaviorSanitizer for both the application and tests:

```bash
cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFLUIDSIM_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

## Usage

### Controls

- **Left Mouse Button + Drag**: Add fluid density and velocity
- **R Key**: Reset the simulation
- **ESC Key**: Exit the application

### Visualization Modes

Press number keys to switch between different visualization modes:

- **1 - Density**: Standard dye visualization (black → blue → cyan → yellow → white)
- **2 - Divergence**: Shows velocity field divergence (should be near black/zero)
  - Blue = negative divergence
  - Red = positive divergence
  - Black areas indicate divergence-free flow (incompressible)
- **3 - Vorticity**: Shows rotation/swirling patterns in the flow
  - Blue = clockwise rotation
  - Red = counter-clockwise rotation
  - Black = no rotation

- **TAB Key**: Cycle through visualization modes
- **H Key**: Show help text with controls

### Customization

Modify constants in `FluidSim.h`:

```cpp
constexpr int GRID_SIZE = 128;             // Grid resolution (128×128)
constexpr int DIFFUSION_SOLVER_ITERATIONS = 4;
constexpr int PRESSURE_SOLVER_MAX_ITERATIONS = 2048;
constexpr float PROJECTION_DIVERGENCE_TOLERANCE = 0.01f;
constexpr float DIFFUSION = 0.0f;          // Diffusion rate
constexpr float VISCOSITY = 0.0000001f;    // Fluid viscosity
constexpr float DENSITY_FADE_RATE = 0.12f; // Density units per second
```

Density is maintained in the normalized range `[0, 1]`. Mouse sources and
fading are rates per second, and simulation timesteps are capped at 50 ms to
avoid large jumps after a pause. The 3×3 density brush injects `1.0` density
unit per second into neighboring cells and `3.0` units per second at its
center; the center saturates at the normalized upper bound. Pointer motion is
normalized by logical window size, coupled at `5.0`, and limited to 10 window
lengths per second; together with the 50 ms timestep cap this bounds a single
frame's velocity impulse at `2.5`.

### Performance Tuning

- **Higher GRID_SIZE**: More detailed simulation, lower FPS
- **Lower GRID_SIZE**: Faster simulation, less detail
- **PRESSURE_SOLVER_MAX_ITERATIONS**: Upper bound for difficult pressure solves
- **PROJECTION_DIVERGENCE_TOLERANCE**: Target cell-scaled residual divergence
- **DIFFUSION_SOLVER_ITERATIONS**: More iterations improve diffusion accuracy
- **VISCOSITY**: Higher values = "thicker" fluid

## Code Overview

### FluidSim Class (`FluidSim.h/cpp`)

Core fluid simulation engine:

```cpp
class FluidSim {
public:
    void step(float dt);            // Advance simulation by one timestep
    void addDensity(int x, int y, float amount);
    void addDensityRate(int x, int y, float amountPerSecond, float dt);
    void addVelocity(int x, int y, float dx, float dy);
    void fadeDensity(float dt);     // Time-based density reduction
    
private:
    void diffuse(...);              // Diffusion step
    void advect(...);               // Advection step
    void project(...);              // Projection step (incompressibility)
    void set_bnd(...);              // Boundary conditions
    void computeDivergence();       // Compute divergence field for visualization
    void computeVorticityField();   // Compute vorticity field for visualization
};
```

### Renderer Class (`Renderer.h/cpp`)

OpenGL visualization with multiple debug modes:

```cpp
class Renderer {
public:
    void draw(const FluidSim& fluid);
    void setRenderMode(RenderMode mode);
    void nextRenderMode();
    
private:
    void updateTexture(...);        // Upload field data to GPU
    void initShaders();             // Compile GLSL shaders
    void initBuffers();             // Setup vertex data
};
```

### Main Application (`main.cpp`)

- Window creation and OpenGL context setup
- Input-state callbacks and timestep-based source application
- Separate logical-window and framebuffer dimensions for HiDPI displays
- Dynamic timestep calculation based on frame time
- Main render loop
- FPS counter

## Rendering Pipeline

1. **Field to Texture**: Validate exact texture size, keep density in `[0, 1]`, and symmetrically normalize signed fields to `[-1, 1]`
2. **Vertex Shader**: Draw full-screen quad
3. **Fragment Shader**: Map field values to colors using mode-specific gradients

### Color Gradients

**Density Mode:**
- Black → Blue (low density)
- Blue → Cyan
- Cyan → Yellow
- Yellow → White (high density)

**Divergence Mode:**
- Black = divergence-free (incompressible flow)
- Blue = negative divergence
- Red = positive divergence

**Vorticity Mode:**
- Blue = clockwise rotation
- Black = no rotation
- Red = counter-clockwise rotation

## Algorithm Details

### Stable Fluids Method

Based on Jos Stam's "Stable Fluids" paper (SIGGRAPH 1999):

1. **Add Forces**: User input adds velocity to the field
2. **Velocity Step**:
   - Diffuse velocity (viscosity)
   - Project to enforce incompressibility
   - Advect velocity through itself
   - Project again
3. **Density Step**:
   - Diffuse density
   - Advect density through velocity field
4. **Render**: Visualize selected field

### Numerical Methods

- **Semi-Lagrangian Advection**: Backtrace particles and use bilinear interpolation
- **Gauss-Seidel/SOR Relaxation**: Iterative diffusion and pressure solves
- **Hodge Decomposition**: Split velocity into divergence-free and gradient components

## Performance

On a typical modern system:
- **FPS**: 60+ at 128×128 resolution
- **Memory**: ~10MB
- **CPU Usage**: Single-threaded, ~15-30%

## Future Enhancements

Possible improvements:
- [ ] Multiple color dyes
- [ ] Particle visualization overlay
- [ ] Variable viscosity/diffusion
- [ ] Export to video
- [ ] GPU compute shaders
- [ ] 3D fluid simulation
- [ ] Interactive obstacles and boundaries
- [ ] Pressure field visualization
- [ ] Vorticity confinement for enhanced turbulence

## References

### Academic Papers

1. **Jos Stam** - "Stable Fluids" (SIGGRAPH 1999)
   - [Paper PDF](http://www.dgp.toronto.edu/people/stam/reality/Research/pdf/ns.pdf)
   
2. **Jos Stam** - "Real-Time Fluid Dynamics for Games" (GDC 2003)
   - [Paper PDF](https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/GDC03.pdf)

3. **Robert Bridson** - "Fluid Simulation for Computer Graphics"
   - Comprehensive textbook on computational fluid dynamics

### Online Resources

- [GPU Gems Chapter 38: Fast Fluid Dynamics](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu)
- [Coding Challenge: 2D Water Ripple](https://www.youtube.com/watch?v=BjoM9oKOAKY)
- [Eulerian vs Lagrangian Methods](https://www.simscale.com/docs/simwiki/numerics-background/eulerian-vs-lagrangian/)

### Tutorials

- [Mike Ash - Fluid Simulation for Dummies](https://mikeash.com/pyblog/fluid-simulation-for-dummies.html)
- [Jamie Wong - Fluid Simulation](http://jamie-wong.com/2016/08/05/webgl-fluid-simulation/)

## Troubleshooting

### Common Issues

**"Failed to initialize GLFW"**
- Ensure GLFW is properly installed
- Check graphics drivers are up to date

**"Shader compilation failed"**
- Verify OpenGL 3.3+ support
- Update graphics drivers

**Low FPS**
- Reduce GRID_SIZE
- Lower solver iteration counts, while retaining the tested divergence tolerance
- Close other GPU-intensive applications

**Segmentation fault**
- Check array bounds in FluidSim
- Verify proper OpenGL context creation

## License

This project is released under the MIT License. See LICENSE file for details.

## Acknowledgments

- Jos Stam for the stable fluids algorithm
- The OpenGL and GLFW communities
- Academic resources on computational fluid dynamics

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## Author

Created as an educational project demonstrating:
- Computational fluid dynamics
- Real-time physics simulation
- OpenGL graphics programming
- C++ performance optimization

---

**Enjoy simulating fluids!** 🌊
