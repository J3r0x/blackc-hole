# Black Hole Simulation

A real-time black hole visualization featuring gravitational lensing effects, inspired by the depiction of **Gargantua** in Christopher Nolan's *Interstellar*.

Built with **C++** and **Raylib**.

---

## Overview

This project simulates the visual appearance of a black hole as seen by a nearby observer. The simulation renders the warping of spacetime around the event horizon, including the characteristic bright ring caused by light bending around the black hole.

The goal was to create a visually accurate representation of relativistic effects while maintaining real-time performance.

---

## Technical Approach

### Rendering Pipeline

The simulation uses a two-pass rendering approach:

1. **3D Scene Pass** — The accretion disk, Einstein ring geometry, and starfield are rendered to an offscreen texture using standard 3D projection
2. **Post-processing Pass** — A fragment shader applies gravitational lensing distortion, bloom, and color grading to the final image

### Gravitational Lensing

The lensing effect is implemented in screen-space using an approximation of the Schwarzschild metric. For each pixel, the shader calculates how much the light ray would be deflected based on its distance from the black hole center, then samples the scene texture at the deflected position.

The deflection angle follows the weak-field approximation θ = 2Rs/b, with additional corrections applied near the photon sphere to simulate strong-field effects.

### Accretion Disk Physics

The disk simulation incorporates several relativistic effects:

- **Keplerian orbital velocity** decreases with radius as v ∝ 1/√r
- **Relativistic beaming** causes intensity to scale with the Doppler factor cubed (I ∝ D³)
- **Color temperature** varies from white-hot at the inner edge to orange-red at the outer edge

---

## Build

### Requirements

- C++23 compatible compiler
- CMake 4.1+
- Raylib
- OpenGL 3.3+

### Compilation

```bash
git clone https://github.com/yourusername/black-hole-simulation.git
cd black-hole-simulation
mkdir build && cd build
cmake ..
cmake --build .
./black-hole-simulation
```

---

## Project Structure

```
black-hole-simulation/
├── main.cpp        # Core simulation logic, 3D geometry generation, camera system
├── lensing.fs      # GLSL fragment shader for gravitational distortion and post-processing
├── CMakeLists.txt  # Build configuration
└── README.md
```


## License

MIT
