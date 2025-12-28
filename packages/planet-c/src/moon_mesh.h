#ifndef MOON_MESH_H
#define MOON_MESH_H

#include <raylib.h>
#include "cube_face.h"

// Moon mesh data with texture coordinates for albedo sampling
// Uses equirectangular projection for heightmap/albedo textures

// CPU-side mesh data with texture coordinates
typedef struct {
  float *vertices;
  float *normals;
  float *texcoords;  // Equirectangular UV coordinates for texture sampling
  unsigned short *indices;
  int vertexCount;
  int triangleCount;
} MoonCpuMesh;

// Heightmap sampler interface
typedef struct {
  Image heightmap;          // 16-bit or 8-bit grayscale heightmap
  float min_height;         // Minimum height (e.g., -10000 meters)
  float max_height;         // Maximum height (e.g., +10000 meters)
} HeightmapData;

// Initialize heightmap data from an image file
// Expects equirectangular projection image
bool heightmap_load(HeightmapData *data, const char *path, float min_height, float max_height);

// Free heightmap data
void heightmap_unload(HeightmapData *data);

// Sample height at a given sphere direction (normalized vector)
// Returns height in meters
float heightmap_sample(const HeightmapData *data, Vector3 sphere_normal);

// Convert sphere normal to equirectangular UV coordinates
// Returns UV in range [0, 1] for both u and v
Vector2 sphere_to_equirect_uv(Vector3 sphere_normal);

// Generate moon mesh patch on CPU (thread-safe if heightmap is read-only)
// Uses texture-based heightmap instead of procedural noise
// Includes texture coordinates for albedo sampling
MoonCpuMesh generate_moon_patch_cpu(CubeFace face,
                                    float u_min, float u_max,
                                    float v_min, float v_max,
                                    float radius,
                                    const HeightmapData *heightmap,
                                    int resolution,
                                    float skirt_depth);

// Upload moon mesh data to GPU (main thread only)
Mesh upload_moon_mesh_gpu(MoonCpuMesh cpu_mesh);

// Free moon CPU mesh data
void free_moon_cpu_mesh(MoonCpuMesh *mesh);

// Synchronous version for convenience
Mesh create_moon_patch_mesh(CubeFace face,
                            float u_min, float u_max,
                            float v_min, float v_max,
                            float radius,
                            const HeightmapData *heightmap,
                            int resolution,
                            float skirt_depth);

#endif // MOON_MESH_H
