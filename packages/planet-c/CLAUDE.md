<instructions>
Explain concepts, architecture, and design decisions clearly and directly. Provide recommendations grounded in the long-term vision. Avoid leading pedagogical questions ("What do you think?", "Should we...?") unless there's genuine technical ambiguity that requires clarification. Only ask questions when you need actual user input or clarification—not to guide learning.

Do not write code unless asked.
</instructions>

<project>
This is a C project that uses Raylib. The goal is to render a dynamically created rocky planet that can stream in mesh chunks as the user flies around from surface to orbit.

## Long-term Vision
Build a procedurally-generated planet with:
- Cubic quadtree LOD system for efficient streaming
- Cascaded shadow maps (CSM) for high-quality, view-dependent shadows
- Noise-based surface deviations for rocky/realistic terrain
- Cube-to-sphere projection for seamless tiling across planet surface
- Dynamic mesh generation and unloading as camera moves

## Implementation Roadmap
Build incrementally, validating each system before adding complexity:

1. **Flat Plane Generation** - Create a simple flat grid mesh as foundation
2. **Noise Deformation** - Apply noise functions to displace vertices for terrain variation
3. **Basic Shadowing** - Implement simple shadow mapping
4. **Cascaded Shadow Maps** - Upgrade to CSM for distance-based shadow detail
5. **Quadtree LOD System** - Implement dynamic subdivision/simplification based on camera distance
6. **Cube-to-Sphere Projection** - Generalize flat plane system to cube faces, then to spherical planet

Each step is standalone and testable before moving to the next.

## Status

✅ **Step 1: Flat Plane Generation** - Complete. Grid mesh with configurable resolution.

✅ **Step 2: Noise Deformation** - Complete. Sine-based displacement implemented with composable noise function injection via function pointers.

✅ **Step 3: Basic Shadowing** - Complete. Single shadow map with view-direction dependent positioning.

⬜ **Step 4: Cascaded Shadow Maps** - Not started.

⬜ **Step 5-6** - Not started.

## Cascaded Shadow Maps (CSM) - Planet-Scale Strategy

**Goal:** Provide shadow coverage from surface detail to full planetary scale, maintaining quality across altitude ranges from ground level to orbit.

**Approach:** Hybrid cascades combining camera-relative (local detail) and planet-relative (global coverage) shadow maps.

### Cascade Configuration

**Cascade 0: Surface Detail (Camera-relative)**
- **Coverage:** ~100-500 units
- **Positioning:** View-frustum aligned with look-ahead adjustment (based on view direction vs light direction)
- **Purpose:** High-resolution shadows for nearby terrain features, rocks, and objects
- **Active range:** Surface to low altitude
- **Resolution priority:** Highest (most shadow map pixels allocated here)

**Cascade 1: Regional (Camera-relative)**
- **Coverage:** ~1,000-5,000 units
- **Positioning:** View-frustum aligned, moderate look-ahead
- **Purpose:** Medium-distance terrain shadows
- **Active range:** Surface to medium altitude
- **Resolution priority:** High

**Cascade 2: Horizon (Camera-relative)**
- **Coverage:** ~10,000-50,000 units
- **Positioning:** Loosely camera-following or purely distance-based
- **Purpose:** Distant terrain features, mountain shadows on horizon
- **Active range:** Medium to high altitude
- **Resolution priority:** Medium

**Cascade 3: Planetary (Planet-relative)**
- **Coverage:** Planet diameter (e.g., 200,000+ units for radius=100,000)
- **Positioning:** **Fixed to planet center**, independent of camera movement
  ```c
  cascade3_target = planet_center;
  cascade3_position = planet_center + lightDir * -(planet_radius * 1.5);
  ```
- **Purpose:** Global planetary shadow coverage showing day/night terminator from orbit
- **Active range:** Always active (or high altitude → orbit)
- **Resolution priority:** Lowest (covers huge area but only needs rough shadow patterns)

### Key Design Principles

1. **Frustum-Fitting:** Core CSM technique—each cascade is fitted to a slice of the view frustum for optimal shadow resolution distribution
2. **Altitude-based transitions:** Cascades 0-2 fade out with increasing altitude; Cascade 3 becomes dominant from orbit
3. **Fixed planetary reference:** Cascade 3 doesn't follow camera—shows true planetary-scale shadow patterns
4. **FOV adaptation:** Frustum-fitting automatically provides better detail when zooming (narrower FOV)
5. **Complementary techniques:** View-dependent positioning (current implementation) complements multi-cascade resolution distribution

### Implementation Components

**Render passes:** 4 shadow map render passes (one per cascade) from light's perspective
**Shader modifications:** Fragment shader samples appropriate cascade based on pixel depth, with smooth transitions between cascades
**Cascade selection:** Distance-based cascade selection in fragment shader
**Memory:** 4 shadow map textures (varying resolutions: e.g., 2048², 2048², 1024², 1024²)

## Code Conventions

**Naming:**
- Variables & Functions: `snake_case` (lowercase with underscores)
- Constants: `UPPER_SNAKE_CASE` (all caps with underscores)
- Structs & Types: `PascalCase` (capitalize each word)
- Macros: `UPPER_SNAKE_CASE`
- Files: `snake_case` or `lowercase`

**Style:** Follow raylib conventions for consistency with the library.
</project>