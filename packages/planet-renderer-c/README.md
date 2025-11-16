# Planet Renderer (C/Raylib)

A lightweight C implementation of a procedural planet renderer using raylib, based on the core strategy from the TypeScript `@hello-worlds/planets` package.

## Features

- **Cubic Quadtree LOD System** - Efficient level-of-detail management using a cube-mapped sphere
- **Chunk-based Terrain** - Reusable mesh tiles for memory efficiency
- **Edge Skirts** - Prevents gaps between LOD levels
- **Procedural Generation** - Customizable height and color generators
- **Real-time Updates** - Dynamic LOD based on camera position
- **Floating Origin System** - Maintains precision at Earth-scale by keeping camera near world origin

## Architecture

The renderer uses a cube-to-sphere projection approach:
1. Project 6 quadtrees onto a cube (one per face)
2. Each quadtree subdivides based on distance to camera
3. Leaf nodes become terrain chunks
4. Chunks are generated with edge skirts to prevent gaps

## Building

### Prerequisites

- CMake 3.15 or higher
- C compiler (GCC, Clang, MSVC)
- Raylib (automatically downloaded if not found)

### Linux/macOS

#### Compile

```bash
mkdir build
cd build
cmake ..
make
```

#### Run Demo

```bash
./simple_planet
```

### Windows

#### Installing Prerequisites

1. **Install CMake**
   - Download from [cmake.org/download](https://cmake.org/download/)
   - Choose "Windows x64 Installer"
   - During installation, select "Add CMake to the system PATH for all users"
   - Verify installation: Open PowerShell and run `cmake --version`

2. **Install a C Compiler** (choose one):

   **Option A: Visual Studio (Recommended)**
   - Download [Visual Studio Community](https://visualstudio.microsoft.com/downloads/) (free)
   - During installation, select "Desktop development with C++"
   - This includes MSVC compiler and necessary build tools

   **Option B: MinGW-w64**
   - Download from [winlibs.com](https://winlibs.com/) or [msys2.org](https://www.msys2.org/)
   - Extract to `C:\mingw64`
   - Add `C:\mingw64\bin` to your system PATH
   - Verify: `gcc --version` in PowerShell

#### Compile with Visual Studio

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

#### Compile with MinGW

```powershell
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make
```

#### Run Demo

```powershell
# With Visual Studio build:
.\Release\simple_planet.exe

# With MinGW build:
.\simple_planet.exe
```

**Note**: If you get "DLL not found" errors, make sure raylib DLLs are in the same directory as the executable or in your system PATH.

## Controls

- **WASD + Mouse**: Move camera
- **W**: Toggle wireframe mode
- **I**: Toggle info display
- **ESC**: Exit

## API Usage

### Basic Example

```c
#include "planet.h"

// Define height generator
float MyHeightGenerator(Vector3 worldPosition, float radius, void* userData) {
    // Return height at this world position
    return radius * 0.05f * (sinf(worldPosition.x * 0.01f) + 1.0f);
}

// Define color generator
Color MyColorGenerator(Vector3 worldPosition, float height, void* userData) {
    return height > 5.0f ? WHITE : GREEN;
}

int main() {
    // Create planet
    Planet* planet = Planet_Create(
        100.0f,        // radius
        5.0f,          // minCellSize (controls LOD detail)
        32,            // minCellResolution (chunk resolution)
        MyHeightGenerator,
        MyColorGenerator,
        NULL           // user data
    );

    // In your game loop
    while (!WindowShouldClose()) {
        // Update LOD based on camera position
        Planet_Update(planet, camera.position);

        // Render
        BeginMode3D(camera);
            Planet_Render(planet);
        EndMode3D();
    }

    // Cleanup
    Planet_Destroy(planet);
}
```

## Floating Origin Support

For large-scale planets (Earth-sized and above), the renderer includes a floating origin system to prevent floating-point precision issues at large distances.

### Problem

At large distances from the world origin, single-precision floats lose precision:
- At 100 units from origin: ~0.000012 unit precision ✓
- At 6,357,000 units (Earth radius): ~0.76 unit precision ⚠️

This causes visible jitter during camera rotation at Earth-scale coordinates.

### Solution

The floating origin system keeps the camera near world origin (0,0,0) by translating the entire world when the camera gets too far away. This maintains full floating-point precision regardless of the "true" world position.

### Usage

```c
Planet* planet = Planet_Create(
    6357000.0f,    // Earth-scale radius
    256.0f,        // 256 meters per minimum cell
    32,
    MyHeightGenerator,
    MyColorGenerator,
    &radius
);

// Enable floating origin
planet->floatingOriginEnabled = true;
planet->floatingOriginThreshold = 100000.0f;  // Recenter when camera > 100km from origin

// In your game loop, handle camera recentering BEFORE Planet_Update
while (!WindowShouldClose()) {
    UpdateCamera(&camera, CAMERA_FREE);

    // Check if recenter will happen and update camera accordingly
    if (planet->floatingOriginEnabled) {
        float distFromOrigin = Vector3Length(camera.position);
        if (distFromOrigin > planet->floatingOriginThreshold) {
            // Recenter camera to origin
            Vector3 recenterOffset = camera.position;
            camera.position = Vector3Subtract(camera.position, recenterOffset);
            camera.target = Vector3Subtract(camera.target, recenterOffset);
        }
    }

    // Update planet (will also recenter world if needed)
    Planet_Update(planet, camera.position);

    // Render...
}
```

### Behavior

When enabled:
1. Camera stays near (0, 0, 0) for maximum float precision
2. World automatically recenters when camera exceeds threshold
3. All chunks are regenerated with new world-space positions
4. `planet->worldOffset` tracks the accumulated "true" world position

### Configuration

**floatingOriginThreshold**: Distance from origin before recentering occurs
- Small planets (radius ~100): Use ~1000.0f
- Earth-scale (radius ~6.3M): Use ~100000.0f (100km)
- Larger values = less frequent recentering but lower precision
- Smaller values = more frequent recentering but better precision

**floatingOriginEnabled**: Toggle the feature on/off
- Default: `false` (disabled)
- Enable for planets with radius > 100,000 units

### Performance Impact

- Recentering disposes and regenerates all chunks (one-time cost)
- With proper threshold tuning, recentering is infrequent
- Expected: < 1 frame drop during recenter
- No performance impact when not recentering

### Debugging

The system logs recentering events:
```
FLOATING ORIGIN: Recentering world. Camera distance: 125432.00
  World offset now: (125432.00, 0.00, 0.00)
  Disposed 1247 chunks for regeneration
```

See `examples/simple_planet.c` for a complete working example.

## Project Structure

```
planet-renderer-c/
├── include/
│   ├── math_utils.h       # Vector3, Matrix4, Box3 utilities
│   ├── quadtree.h         # QuadTree LOD structure
│   ├── cubic_quadtree.h   # 6-faced cubic quadtree
│   ├── chunk.h            # Terrain chunk mesh generation
│   └── planet.h           # Main planet renderer API
├── src/
│   ├── math_utils.c
│   ├── quadtree.c
│   ├── cubic_quadtree.c
│   ├── chunk.c
│   └── planet.c
├── examples/
│   └── simple_planet.c    # Basic demo application
├── CMakeLists.txt
└── README.md
```

## Key Parameters

### Planet_Create

- **radius**: Planet radius in world units
- **minCellSize**: Smallest allowed chunk size (lower = more detail, higher LOD cost)
- **minCellResolution**: Vertices per chunk dimension (higher = smoother but more expensive)
- **heightGen**: Function to generate terrain height
- **colorGen**: Function to generate terrain color

### LOD Behavior

The LOD system uses a distance-based comparison value (default: 2.0). Chunks subdivide when:
```
distance_to_camera < chunk_size * comparison_value
```

Lower values = more aggressive LOD (fewer chunks, better performance)
Higher values = less aggressive LOD (more chunks, better quality)

## Performance Tips

1. **Adjust minCellSize**: Larger values = fewer chunks
2. **Adjust minCellResolution**: Lower values = less geometry per chunk
3. **Limit chunk generation**: The system generates chunks synchronously - consider adding a budget
4. **Chunk pooling**: The implementation recycles chunks when they go out of view

## Comparison to TypeScript Version

This C implementation follows the same core strategy as `packages/planets/src/planet/`:
- ✅ Cubic quadtree LOD (CubicQuadTree.ts → cubic_quadtree.c)
- ✅ Chunk-based rendering (Chunk.ts → chunk.c)
- ✅ Height/color generators (Planet.chunk.ts → chunk.c)
- ✅ Edge skirts (fixEdgeSkirts.ts → FixEdgeSkirts)
- ✅ **Floating origin system** (implemented in C, planned for TypeScript)
- ❌ Worker threads (simplified to synchronous generation)
- ❌ BVH collision (can be added later)
- ❌ Three.js materials (uses raylib's simpler material system)

## Future Enhancements

- [ ] Asynchronous chunk generation (threading)
- [ ] Better noise functions (simplex/perlin via stb_perlin.h)
- [ ] Custom shaders for advanced materials
- [ ] Collision detection (BVH or heightmap)
- [ ] Texture splatting for terrain layers
- [ ] Normal map generation
- [ ] Atmosphere rendering

## License

MIT

## Credits

Based on the procedural planet generation strategy from [@hello-worlds/planets](../planets/).
