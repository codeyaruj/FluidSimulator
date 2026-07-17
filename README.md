# 2D Fluid Simulation

This is an interactive 2D fluid simulation written in C++ and rendered with
OpenGL. Dragging the mouse adds dye and pushes the fluid. The display can show
the dye itself or two useful debug fields: divergence and vorticity.

I originally made this project during my first year while learning C++, OpenGL, and basic fluid simulation. I revisited it later to fix problems in the pressure projection, mouse input, and debug rendering, and added regression tests for the bugs I found.

## How the simulation works

I used a grid rather than particles because it made the pressure and velocity
calculations easier to understand. Each cell stores density and horizontal and
vertical velocity.

For each update, the program:

1. Diffuses the velocity to model viscosity.
2. Solves for pressure and removes divergence from the velocity.
3. Moves the velocity through the grid using semi-Lagrangian advection.
4. Projects the velocity a second time.
5. Diffuses and advects the density through the resulting flow.

Advection works by tracing each grid cell backward through the velocity field
and interpolating between nearby cells. The pressure solve is iterative. The
OpenGL renderer draws the selected field on a full-screen rectangle; signed
debug values are blue when negative, black near zero, and red when positive.

## Dependencies and building

The project needs a C++14 compiler, CMake 3.20 or newer, OpenGL 3.3, GLFW 3,
and GLEW.

Ubuntu or Debian:

```bash
sudo apt-get install build-essential cmake libglfw3-dev libglew-dev libgl1-mesa-dev
```

macOS:

```bash
brew install cmake glfw glew
```

On Windows, the libraries can be installed with vcpkg:

```powershell
vcpkg install glfw3 glew opengl
```

Build and run from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/FluidSimulation
```

Multi-configuration generators such as Visual Studio need `--config Debug`
when building and testing. The executable is then usually
`build\Debug\FluidSimulation.exe`.

Run the regression tests with:

```bash
ctest --test-dir build --output-on-failure
```

The tests are enabled by default. Pass `-DBUILD_TESTING=OFF` to CMake if only
the application is needed.

## Controls

- Hold the left mouse button and drag to add density and velocity.
- Press `1`, `2`, or `3` for density, divergence, or vorticity.
- Press `Tab` to cycle through those views.
- Press `R` to reset.
- Press `Esc` to quit.

## What I learned and current limitations

The boundary conditions were the hardest part, and the simulation can still
behave strangely near the corners. Fixing the projection also showed me that a
result that looks plausible is not necessarily numerically correct, which is
why the projection and input bugs now have regression tests.

This is a small learning project rather than a complete fluid-physics system.
It is two-dimensional, uses one density field, runs the simulation on the CPU,
and treats the edge of the grid as a closed box. There are no moving obstacles
or free surfaces. Semi-Lagrangian advection is stable and approachable, but it
also smooths out fine detail over time.

## References

- Jos Stam, [Stable Fluids](http://www.dgp.toronto.edu/people/stam/reality/Research/pdf/ns.pdf)
- Jos Stam, [Real-Time Fluid Dynamics for Games](https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/GDC03.pdf)
- NVIDIA, [GPU Gems: Fast Fluid Dynamics Simulation on the GPU](https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu)
- Mike Ash, [Fluid Simulation for Dummies](https://mikeash.com/pyblog/fluid-simulation-for-dummies.html)
- Jamie Wong, [WebGL Fluid Simulation](http://jamie-wong.com/2016/08/05/webgl-fluid-simulation/)
