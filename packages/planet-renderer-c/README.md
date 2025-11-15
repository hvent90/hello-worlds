# Planet Renderer (C/Raylib)

A lightweight C implementation of a procedural planet renderer using raylib, based on the core strategy from the TypeScript `@hello-worlds/planets` package.

## Features

- **Cubic Quadtree LOD System** - Efficient level-of-detail management using a cube-mapped sphere
- **Chunk-based Terrain** - Reusable mesh tiles for memory efficiency
- **Edge Skirts** - Prevents gaps between LOD levels
- **Procedural Generation** - Customizable height and color generators
- **Real-time Updates** - Dynamic LOD based on camera position

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
