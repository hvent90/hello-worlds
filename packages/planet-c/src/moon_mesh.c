#include "moon_mesh.h"
#include "cube_face.h"
#include <raymath.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// Heightmap Loading and Sampling
// ============================================================================

bool heightmap_load(HeightmapData *data, const char *path, float min_height, float max_height) {
  if (data == NULL || path == NULL) return false;

  data->heightmap = LoadImage(path);
  if (data->heightmap.data == NULL) {
    fprintf(stderr, "Failed to load heightmap: %s\n", path);
    return false;
  }

  data->min_height = min_height;
  data->max_height = max_height;

  printf("Loaded heightmap: %dx%d, format=%d\n",
         data->heightmap.width, data->heightmap.height, data->heightmap.format);

  return true;
}

void heightmap_unload(HeightmapData *data) {
  if (data == NULL) return;
  if (data->heightmap.data != NULL) {
    UnloadImage(data->heightmap);
    data->heightmap.data = NULL;
  }
}

Vector2 sphere_to_equirect_uv(Vector3 n) {
  // Convert sphere normal to equirectangular UV coordinates
  // longitude = atan2(z, x), latitude = asin(y)
  // UV range: [0, 1] for both

  float longitude = atan2f(n.z, n.x);              // Range: [-PI, PI]
  float latitude = asinf(Clamp(n.y, -1.0f, 1.0f)); // Range: [-PI/2, PI/2]

  // Map to [0, 1] range
  float u = (longitude / (2.0f * (float)M_PI)) + 0.5f;
  float v = (latitude / (float)M_PI) + 0.5f;

  return (Vector2){ u, v };
}

// Helper function to get normalized pixel value from heightmap
static float get_heightmap_pixel(const HeightmapData *data, int x, int y) {
  Color pixel = GetImageColor(data->heightmap, x, y);
  if (data->heightmap.format == PIXELFORMAT_UNCOMPRESSED_GRAYSCALE ||
      data->heightmap.format == PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA) {
    return pixel.r / 255.0f;
  }
  return (0.299f * pixel.r + 0.587f * pixel.g + 0.114f * pixel.b) / 255.0f;
}

float heightmap_sample(const HeightmapData *data, Vector3 sphere_normal) {
  if (data == NULL || data->heightmap.data == NULL) return 0.0f;

  Vector2 uv = sphere_to_equirect_uv(sphere_normal);

  // Convert UV to pixel coordinates with wrapping
  int width = data->heightmap.width;
  int height = data->heightmap.height;

  // Bilinear interpolation
  float px = uv.x * (float)width;
  float py = (1.0f - uv.y) * (float)height;  // Flip V for standard image orientation

  int x0 = (int)floorf(px);
  int y0 = (int)floorf(py);
  int x1 = x0 + 1;
  int y1 = y0 + 1;

  float fx = px - (float)x0;
  float fy = py - (float)y0;

  // Wrap x, clamp y
  x0 = ((x0 % width) + width) % width;
  x1 = ((x1 % width) + width) % width;
  y0 = Clamp(y0, 0, height - 1);
  y1 = Clamp(y1, 0, height - 1);

  // Sample 4 corners
  float s00 = get_heightmap_pixel(data, x0, y0);
  float s10 = get_heightmap_pixel(data, x1, y0);
  float s01 = get_heightmap_pixel(data, x0, y1);
  float s11 = get_heightmap_pixel(data, x1, y1);

  // Bilinear interpolation
  float s0 = s00 * (1.0f - fx) + s10 * fx;
  float s1 = s01 * (1.0f - fx) + s11 * fx;
  float normalized = s0 * (1.0f - fy) + s1 * fy;

  return data->min_height + normalized * (data->max_height - data->min_height);
}

// ============================================================================
// Mesh Generation with Texture Coordinates
// ============================================================================

// Get sphere point with height from heightmap
static Vector3 moon_face_to_sphere_point(CubeFace face, float u, float v,
                                         float radius, const HeightmapData *heightmap,
                                         Vector3 *out_normal) {
  Vector3 cube_point = cube_face_to_cube_point(face, u, v);
  Vector3 sphere_normal = Vector3Normalize(cube_point);

  if (out_normal) {
    *out_normal = sphere_normal;
  }

  // Sample height from heightmap
  float height = 0.0f;
  if (heightmap != NULL && heightmap->heightmap.data != NULL) {
    height = heightmap_sample(heightmap, sphere_normal);
  }

  float displaced_radius = radius + height;
  return Vector3Scale(sphere_normal, displaced_radius);
}

MoonCpuMesh generate_moon_patch_cpu(CubeFace face,
                                    float u_min, float u_max,
                                    float v_min, float v_max,
                                    float radius,
                                    const HeightmapData *heightmap,
                                    int resolution,
                                    float skirt_depth) {
  MoonCpuMesh cpu_mesh = {0};

  // Grid dimensions
  const int grid_size = resolution + 1;
  const int main_vertex_count = grid_size * grid_size;
  const int main_triangle_count = resolution * resolution * 2;

  // Extended grid for proper edge normal calculation
  const int ext_size = resolution + 3;
  const int ext_vertex_count = ext_size * ext_size;

  // Skirt geometry
  const int skirt_vertex_count = (skirt_depth > 0.0f) ? 4 * grid_size : 0;
  const int skirt_triangle_count = (skirt_depth > 0.0f) ? 4 * resolution * 2 : 0;

  const int total_vertex_count = main_vertex_count + skirt_vertex_count;
  const int total_triangle_count = main_triangle_count + skirt_triangle_count;
  const int total_index_count = total_triangle_count * 3;

  // Allocate extended grid for normal calculation
  Vector3 *ext_vertices = malloc(ext_vertex_count * sizeof(Vector3));
  Vector3 *ext_normals = malloc(ext_vertex_count * sizeof(Vector3));
  Vector3 *ext_sphere_normals = malloc(ext_vertex_count * sizeof(Vector3));

  // Allocate final mesh arrays
  Vector3 *vertices = malloc(total_vertex_count * sizeof(Vector3));
  unsigned short *indices = malloc(total_index_count * sizeof(unsigned short));
  Vector3 *normals = malloc(total_vertex_count * sizeof(Vector3));
  Vector2 *texcoords = malloc(total_vertex_count * sizeof(Vector2));

  if (!ext_vertices || !ext_normals || !ext_sphere_normals ||
      !vertices || !indices || !normals || !texcoords) {
    fprintf(stderr, "Moon patch mesh allocation failed\n");
    free(ext_vertices);
    free(ext_normals);
    free(ext_sphere_normals);
    free(vertices);
    free(indices);
    free(normals);
    free(texcoords);
    return cpu_mesh;
  }

  // UV step for the main grid
  const float u_step = (u_max - u_min) / (float)resolution;
  const float v_step = (v_max - v_min) / (float)resolution;

  // Generate extended grid vertices
  for (int y = 0; y < ext_size; y++) {
    for (int x = 0; x < ext_size; x++) {
      const int idx = y * ext_size + x;
      float u = u_min + (x - 1) * u_step;
      float v = v_min + (y - 1) * v_step;

      Vector3 sphere_normal;
      Vector3 pos = moon_face_to_sphere_point(face, u, v, radius, heightmap, &sphere_normal);

      ext_vertices[idx] = pos;
      ext_sphere_normals[idx] = sphere_normal;
      ext_normals[idx] = (Vector3){0.0f, 0.0f, 0.0f};
    }
  }

  // Accumulate face normals across extended grid
  for (int y = 0; y < ext_size - 1; y++) {
    for (int x = 0; x < ext_size - 1; x++) {
      const int v0 = y * ext_size + x;
      const int v1 = v0 + 1;
      const int v2 = (y + 1) * ext_size + x;
      const int v3 = v2 + 1;

      // First triangle
      Vector3 edge1 = Vector3Subtract(ext_vertices[v2], ext_vertices[v0]);
      Vector3 edge2 = Vector3Subtract(ext_vertices[v1], ext_vertices[v0]);
      Vector3 face_normal = Vector3CrossProduct(edge1, edge2);
      ext_normals[v0] = Vector3Add(ext_normals[v0], face_normal);
      ext_normals[v1] = Vector3Add(ext_normals[v1], face_normal);
      ext_normals[v2] = Vector3Add(ext_normals[v2], face_normal);

      // Second triangle
      edge1 = Vector3Subtract(ext_vertices[v2], ext_vertices[v1]);
      edge2 = Vector3Subtract(ext_vertices[v3], ext_vertices[v1]);
      face_normal = Vector3CrossProduct(edge1, edge2);
      ext_normals[v1] = Vector3Add(ext_normals[v1], face_normal);
      ext_normals[v2] = Vector3Add(ext_normals[v2], face_normal);
      ext_normals[v3] = Vector3Add(ext_normals[v3], face_normal);
    }
  }

  // Check if face needs flipped winding
  bool flip_winding = cube_face_needs_flip(face);

  // Normalize accumulated normals
  for (int i = 0; i < ext_vertex_count; i++) {
    ext_normals[i] = Vector3Normalize(ext_normals[i]);
    if (flip_winding) {
      ext_normals[i] = Vector3Negate(ext_normals[i]);
    }
  }

  // Copy inner vertices to final mesh arrays with texture coordinates
  for (int y = 0; y < grid_size; y++) {
    for (int x = 0; x < grid_size; x++) {
      const int final_idx = y * grid_size + x;
      const int ext_idx = (y + 1) * ext_size + (x + 1);

      vertices[final_idx] = ext_vertices[ext_idx];
      normals[final_idx] = ext_normals[ext_idx];

      // Calculate equirectangular UV from sphere normal
      texcoords[final_idx] = sphere_to_equirect_uv(ext_sphere_normals[ext_idx]);
    }
  }

  // Free extended arrays
  free(ext_vertices);
  free(ext_normals);
  free(ext_sphere_normals);

  // Generate indices for main grid
  int idx = 0;
  for (int y = 0; y < resolution; y++) {
    for (int x = 0; x < resolution; x++) {
      const int v0 = y * grid_size + x;
      const int v1 = v0 + 1;
      const int v2 = (y + 1) * grid_size + x;
      const int v3 = v2 + 1;

      if (flip_winding) {
        indices[idx++] = v0;
        indices[idx++] = v1;
        indices[idx++] = v2;
        indices[idx++] = v1;
        indices[idx++] = v3;
        indices[idx++] = v2;
      } else {
        indices[idx++] = v0;
        indices[idx++] = v2;
        indices[idx++] = v1;
        indices[idx++] = v1;
        indices[idx++] = v2;
        indices[idx++] = v3;
      }
    }
  }

  // Generate skirt geometry
  if (skirt_depth > 0.0f) {
    int skirt_base = main_vertex_count;

    // Bottom edge (y=0)
    for (int i = 0; i < grid_size; i++) {
      int main_idx = i;
      int skirt_idx = skirt_base + i;

      Vector3 main_pos = vertices[main_idx];
      Vector3 dir_to_center = Vector3Normalize(main_pos);
      vertices[skirt_idx] = Vector3Subtract(main_pos, Vector3Scale(dir_to_center, skirt_depth));
      normals[skirt_idx] = normals[main_idx];
      texcoords[skirt_idx] = texcoords[main_idx];
    }
    for (int i = 0; i < resolution; i++) {
      int m0 = i;
      int m1 = i + 1;
      int s0 = skirt_base + i;
      int s1 = skirt_base + i + 1;

      if (flip_winding) {
        indices[idx++] = m0; indices[idx++] = s0; indices[idx++] = m1;
        indices[idx++] = m1; indices[idx++] = s1; indices[idx++] = s0;
      } else {
        indices[idx++] = m0; indices[idx++] = m1; indices[idx++] = s0;
        indices[idx++] = m1; indices[idx++] = s0; indices[idx++] = s1;
      }
    }

    // Top edge (y=resolution)
    int top_skirt_base = skirt_base + grid_size;
    for (int i = 0; i < grid_size; i++) {
      int main_idx = resolution * grid_size + i;
      int skirt_idx = top_skirt_base + i;

      Vector3 main_pos = vertices[main_idx];
      Vector3 dir_to_center = Vector3Normalize(main_pos);
      vertices[skirt_idx] = Vector3Subtract(main_pos, Vector3Scale(dir_to_center, skirt_depth));
      normals[skirt_idx] = normals[main_idx];
      texcoords[skirt_idx] = texcoords[main_idx];
    }
    for (int i = 0; i < resolution; i++) {
      int m0 = resolution * grid_size + i;
      int m1 = resolution * grid_size + i + 1;
      int s0 = top_skirt_base + i;
      int s1 = top_skirt_base + i + 1;

      if (flip_winding) {
        indices[idx++] = m0; indices[idx++] = m1; indices[idx++] = s0;
        indices[idx++] = m1; indices[idx++] = s0; indices[idx++] = s1;
      } else {
        indices[idx++] = m0; indices[idx++] = s0; indices[idx++] = m1;
        indices[idx++] = m1; indices[idx++] = s1; indices[idx++] = s0;
      }
    }

    // Left edge (x=0)
    int left_skirt_base = skirt_base + 2 * grid_size;
    for (int i = 0; i < grid_size; i++) {
      int main_idx = i * grid_size;
      int skirt_idx = left_skirt_base + i;

      Vector3 main_pos = vertices[main_idx];
      Vector3 dir_to_center = Vector3Normalize(main_pos);
      vertices[skirt_idx] = Vector3Subtract(main_pos, Vector3Scale(dir_to_center, skirt_depth));
      normals[skirt_idx] = normals[main_idx];
      texcoords[skirt_idx] = texcoords[main_idx];
    }
    for (int i = 0; i < resolution; i++) {
      int m0 = i * grid_size;
      int m1 = (i + 1) * grid_size;
      int s0 = left_skirt_base + i;
      int s1 = left_skirt_base + i + 1;

      if (flip_winding) {
        indices[idx++] = m0; indices[idx++] = s0; indices[idx++] = m1;
        indices[idx++] = m1; indices[idx++] = s0; indices[idx++] = s1;
      } else {
        indices[idx++] = m0; indices[idx++] = m1; indices[idx++] = s0;
        indices[idx++] = m1; indices[idx++] = s1; indices[idx++] = s0;
      }
    }

    // Right edge (x=resolution)
    int right_skirt_base = skirt_base + 3 * grid_size;
    for (int i = 0; i < grid_size; i++) {
      int main_idx = i * grid_size + resolution;
      int skirt_idx = right_skirt_base + i;

      Vector3 main_pos = vertices[main_idx];
      Vector3 dir_to_center = Vector3Normalize(main_pos);
      vertices[skirt_idx] = Vector3Subtract(main_pos, Vector3Scale(dir_to_center, skirt_depth));
      normals[skirt_idx] = normals[main_idx];
      texcoords[skirt_idx] = texcoords[main_idx];
    }
    for (int i = 0; i < resolution; i++) {
      int m0 = i * grid_size + resolution;
      int m1 = (i + 1) * grid_size + resolution;
      int s0 = right_skirt_base + i;
      int s1 = right_skirt_base + i + 1;

      if (flip_winding) {
        indices[idx++] = m0; indices[idx++] = s0; indices[idx++] = m1;
        indices[idx++] = m1; indices[idx++] = s1; indices[idx++] = s0;
      } else {
        indices[idx++] = m0; indices[idx++] = m1; indices[idx++] = s0;
        indices[idx++] = m1; indices[idx++] = s0; indices[idx++] = s1;
      }
    }
  }

  cpu_mesh.vertices = (float*)vertices;
  cpu_mesh.normals = (float*)normals;
  cpu_mesh.texcoords = (float*)texcoords;
  cpu_mesh.indices = indices;
  cpu_mesh.vertexCount = total_vertex_count;
  cpu_mesh.triangleCount = total_triangle_count;

  return cpu_mesh;
}

Mesh upload_moon_mesh_gpu(MoonCpuMesh cpu_mesh) {
  Mesh mesh = {0};

  if (cpu_mesh.vertexCount == 0 || cpu_mesh.triangleCount == 0) return mesh;

  mesh.vertexCount = cpu_mesh.vertexCount;
  mesh.triangleCount = cpu_mesh.triangleCount;
  mesh.vertices = cpu_mesh.vertices;
  mesh.normals = cpu_mesh.normals;
  mesh.texcoords = cpu_mesh.texcoords;
  mesh.indices = cpu_mesh.indices;

  UploadMesh(&mesh, false);

  if (mesh.vaoId == 0) {
    fprintf(stderr, "Moon patch mesh upload failed\n");
  }

  return mesh;
}

void free_moon_cpu_mesh(MoonCpuMesh *mesh) {
  if (mesh == NULL) return;
  free(mesh->vertices);
  free(mesh->normals);
  free(mesh->texcoords);
  free(mesh->indices);
  mesh->vertices = NULL;
  mesh->normals = NULL;
  mesh->texcoords = NULL;
  mesh->indices = NULL;
  mesh->vertexCount = 0;
  mesh->triangleCount = 0;
}

Mesh create_moon_patch_mesh(CubeFace face,
                            float u_min, float u_max,
                            float v_min, float v_max,
                            float radius,
                            const HeightmapData *heightmap,
                            int resolution,
                            float skirt_depth) {
  MoonCpuMesh cpu_mesh = generate_moon_patch_cpu(face, u_min, u_max, v_min, v_max,
                                                  radius, heightmap, resolution, skirt_depth);
  return upload_moon_mesh_gpu(cpu_mesh);
}
