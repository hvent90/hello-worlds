#ifndef MESH_H
#define MESH_H

#include <raylib.h>
#include "cube_face.h"

Mesh create_plane_mesh_with_noise(float scale, int resolution);
Mesh create_plane_mesh_with_noise_ws(Vector3 position, float scale,
                                     int resolution);
Mesh create_plane_mesh(float scale, int resolution);
Mesh create_triangle_mesh(float scale);

// Sphere patch mesh for cubic quadtree planet rendering
// face: which cube face this patch belongs to
// u_min/u_max/v_min/v_max: UV bounds on the face, range [-1, 1]
// radius: base radius of the sphere
// noise_scale: multiplier for terrain height displacement
// resolution: number of subdivisions per edge
// skirt_depth: how far skirt vertices extend toward planet center (0 = no skirt)
Mesh create_sphere_patch_mesh(CubeFace face, 
                               float u_min, float u_max, 
                               float v_min, float v_max,
                               float radius, float noise_scale,
                               int resolution, float skirt_depth);

// CPU-side mesh data (no GPU dependence)
typedef struct {
  float *vertices;
  float *normals;
  unsigned short *indices;
  int vertexCount;
  int triangleCount;
} CpuMesh;

// Generate mesh data on CPU (thread-safe)
CpuMesh generate_sphere_patch_cpu(CubeFace face, 
                                 float u_min, float u_max, 
                                 float v_min, float v_max,
                                 float radius, float noise_scale,
                                 int resolution, float skirt_depth);

// Upload CPU mesh data to GPU (Main thread only)
Mesh upload_mesh_gpu(CpuMesh cpu_mesh);

// Helper to free CPU mesh data
void free_cpu_mesh(CpuMesh *cpu_mesh);

// Sphere patch mesh for cubic quadtree planet rendering
// ... (previous comments)
Mesh create_sphere_patch_mesh(CubeFace face, 
                               float u_min, float u_max, 
                               float v_min, float v_max,
                               float radius, float noise_scale,
                               int resolution, float skirt_depth);

#endif
