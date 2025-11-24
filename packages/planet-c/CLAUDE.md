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

🚧 **Step 2: Noise Deformation** - In progress. Sine-based displacement implemented with composable noise function injection via function pointers.

⬜ **Step 3-6** - Not started.

## Code Conventions

**Naming:**
- Variables & Functions: `snake_case` (lowercase with underscores)
- Constants: `UPPER_SNAKE_CASE` (all caps with underscores)
- Structs & Types: `PascalCase` (capitalize each word)
- Macros: `UPPER_SNAKE_CASE`
- Files: `snake_case` or `lowercase`

**Style:** Follow raylib conventions for consistency with the library.
</project>