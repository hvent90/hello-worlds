#include "mesh.h"
#include "noise.h"
#include "cube_face.h"
#include "raymath.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

Mesh create_plane_mesh_with_noise(float scale, int resolution) {
  const int vertex_count = (resolution + 1) * (resolution + 1);
  const int triangle_count = resolution * resolution * 2;
  const int index_count = triangle_count * 3;

  Vector3 *vertices = malloc(vertex_count * sizeof(Vector3));
  unsigned short *indices = malloc(index_count * sizeof(unsigned short));
  Vector3 *normals = malloc(vertex_count * sizeof(Vector3));

  Mesh plane_mesh = {0}; // Initialize all fields in struct to 0
  if (vertices && indices && normals) {
    // Create vertex positions
    for (int z = 0; z <= resolution; z++) {
      for (int x = 0; x <= resolution; x++) {
        const int idx = z * (resolution + 1) + x;
        const float xPos = (x / (float)resolution) * 2.0f * scale - scale;
        const float zPos = (z / (float)resolution) * 2.0f * scale - scale;
        vertices[idx] = (Vector3){
            xPos,
            evaluate_moon_noise(xPos * 0.00002f, zPos * 0.00002f) * 3000.0f,
            zPos};
      }
    }

    // Initialize normals to zero
    for (unsigned int i = 0; i < vertex_count; i++) {
      normals[i] = (Vector3){0.0f, 0.0f, 0.0f};
    }

    // Create indices and accumulate normals
    int idx = 0;
    for (int z = 0; z < resolution; z++) {
      for (int x = 0; x < resolution; x++) {
        const int v0 = z * (resolution + 1) + x;
        const int v1 = v0 + 1;
        const int v2 = (z + 1) * (resolution + 1) + x;
        const int v3 = v2 + 1;

        // First triangle (counter-clockwise from above)
        indices[idx++] = v0;
        indices[idx++] = v2;
        indices[idx++] = v1;

        Vector3 edge1 = Vector3Subtract(vertices[v2], vertices[v0]);
        Vector3 edge2 = Vector3Subtract(vertices[v1], vertices[v0]);
        Vector3 normal = Vector3CrossProduct(edge1, edge2);
        normals[v0] = Vector3Add(normals[v0], normal);
        normals[v2] = Vector3Add(normals[v2], normal);
        normals[v1] = Vector3Add(normals[v1], normal);

        // Second triangle
        indices[idx++] = v1;
        indices[idx++] = v2;
        indices[idx++] = v3;

        edge1 = Vector3Subtract(vertices[v2], vertices[v1]);
        edge2 = Vector3Subtract(vertices[v3], vertices[v1]);
        normal = Vector3CrossProduct(edge1, edge2);
        normals[v1] = Vector3Add(normals[v1], normal);
        normals[v2] = Vector3Add(normals[v2], normal);
        normals[v3] = Vector3Add(normals[v3], normal);
      }
    }

    // Normalize all vertex normals
    for (unsigned int i = 0; i < vertex_count; i++) {
      normals[i] = Vector3Normalize(normals[i]);
    }

    // Populate the mesh
    plane_mesh.vertexCount = vertex_count;
    plane_mesh.triangleCount = triangle_count;
    plane_mesh.vertices = (float *)vertices;
    plane_mesh.indices = indices;
    plane_mesh.normals = (float *)normals;

    UploadMesh(&plane_mesh, false);
    if (plane_mesh.vaoId == 0) {
      fprintf(stderr, "Mesh upload may have failed\n");
      // Clean up on upload failure
      free(vertices);
      free(indices);
      free(normals);
      plane_mesh = (Mesh){0};
    }
    // NOTE: Don't free vertices/indices/normals here - UnloadMesh() will handle
    // it
  } else {
    fprintf(stderr, "Quad memory allocation failed\n");
  }

  return plane_mesh;
}

Mesh create_plane_mesh_with_noise_ws(Vector3 position, float scale,
                                     int resolution) {
  // Final mesh dimensions
  const int vertex_count = (resolution + 1) * (resolution + 1);
  const int triangle_count = resolution * resolution * 2;
  const int index_count = triangle_count * 3;

  // Extended grid dimensions (1 extra vertex on each side for normal calculation)
  const int ext_size = resolution + 3;  // (resolution+1) + 2 border vertices
  const int ext_vertex_count = ext_size * ext_size;
  const float step = scale / (float)resolution;  // Vertex spacing

  // Allocate extended grid for height sampling and normal calculation
  Vector3 *ext_vertices = malloc(ext_vertex_count * sizeof(Vector3));
  Vector3 *ext_normals = malloc(ext_vertex_count * sizeof(Vector3));

  // Allocate final mesh arrays
  Vector3 *vertices = malloc(vertex_count * sizeof(Vector3));
  unsigned short *indices = malloc(index_count * sizeof(unsigned short));
  Vector3 *normals = malloc(vertex_count * sizeof(Vector3));

  Mesh plane_mesh = {0}; // Initialize all fields in struct to 0
  if (ext_vertices && ext_normals && vertices && indices && normals) {
    // Create extended vertex positions
    // Extended grid starts at (-step, -step) relative to mesh origin
    for (int z = 0; z < ext_size; z++) {
      for (int x = 0; x < ext_size; x++) {
        const int idx = z * ext_size + x;
        // (x-1) and (z-1) so that x=1,z=1 corresponds to the mesh origin
        const float xPos = ((float)(x - 1)) * step;
        const float zPos = ((float)(z - 1)) * step;

        const float noise_x = position.x + xPos;
        const float noise_z = position.z + zPos;
        ext_vertices[idx] = (Vector3){
            xPos,
            evaluate_moon_noise(noise_x * 0.00002f, noise_z * 0.00002f) *
                3000.0f,
            zPos};
      }
    }

    // Initialize extended normals to zero
    for (int i = 0; i < ext_vertex_count; i++) {
      ext_normals[i] = (Vector3){0.0f, 0.0f, 0.0f};
    }

    // Accumulate normals from all triangles in extended grid
    // The extended grid has (ext_size-1) × (ext_size-1) quads = (resolution+2)² quads
    for (int z = 0; z < ext_size - 1; z++) {
      for (int x = 0; x < ext_size - 1; x++) {
        const int v0 = z * ext_size + x;
        const int v1 = v0 + 1;
        const int v2 = (z + 1) * ext_size + x;
        const int v3 = v2 + 1;

        // First triangle (counter-clockwise from above)
        Vector3 edge1 = Vector3Subtract(ext_vertices[v2], ext_vertices[v0]);
        Vector3 edge2 = Vector3Subtract(ext_vertices[v1], ext_vertices[v0]);
        Vector3 normal = Vector3CrossProduct(edge1, edge2);
        ext_normals[v0] = Vector3Add(ext_normals[v0], normal);
        ext_normals[v2] = Vector3Add(ext_normals[v2], normal);
        ext_normals[v1] = Vector3Add(ext_normals[v1], normal);

        // Second triangle
        edge1 = Vector3Subtract(ext_vertices[v2], ext_vertices[v1]);
        edge2 = Vector3Subtract(ext_vertices[v3], ext_vertices[v1]);
        normal = Vector3CrossProduct(edge1, edge2);
        ext_normals[v1] = Vector3Add(ext_normals[v1], normal);
        ext_normals[v2] = Vector3Add(ext_normals[v2], normal);
        ext_normals[v3] = Vector3Add(ext_normals[v3], normal);
      }
    }

    // Normalize extended normals
    for (int i = 0; i < ext_vertex_count; i++) {
      ext_normals[i] = Vector3Normalize(ext_normals[i]);
    }

    // Copy inner vertices and normals to final mesh arrays
    // Inner region: x,z in [1, resolution+1] in extended grid
    for (int z = 0; z <= resolution; z++) {
      for (int x = 0; x <= resolution; x++) {
        const int final_idx = z * (resolution + 1) + x;
        const int ext_idx = (z + 1) * ext_size + (x + 1);  // Offset by 1 to skip border
        vertices[final_idx] = ext_vertices[ext_idx];
        normals[final_idx] = ext_normals[ext_idx];
      }
    }

    // Create indices for final mesh
    int idx = 0;
    for (int z = 0; z < resolution; z++) {
      for (int x = 0; x < resolution; x++) {
        const int v0 = z * (resolution + 1) + x;
        const int v1 = v0 + 1;
        const int v2 = (z + 1) * (resolution + 1) + x;
        const int v3 = v2 + 1;

        // First triangle (counter-clockwise from above)
        indices[idx++] = v0;
        indices[idx++] = v2;
        indices[idx++] = v1;

        // Second triangle
        indices[idx++] = v1;
        indices[idx++] = v2;
        indices[idx++] = v3;
      }
    }

    // Free extended arrays - no longer needed
    free(ext_vertices);
    free(ext_normals);

    // Populate the mesh
    plane_mesh.vertexCount = vertex_count;
    plane_mesh.triangleCount = triangle_count;
    plane_mesh.vertices = (float *)vertices;
    plane_mesh.indices = indices;
    plane_mesh.normals = (float *)normals;

    UploadMesh(&plane_mesh, false);
    if (plane_mesh.vaoId == 0) {
      fprintf(stderr, "Mesh upload may have failed\n");
      // Clean up on upload failure
      free(vertices);
      free(indices);
      free(normals);
      plane_mesh = (Mesh){0};
    }
    // NOTE: Don't free vertices/indices/normals here - UnloadMesh() will handle
    // it
  } else {
    fprintf(stderr, "Quad memory allocation failed\n");
    // Clean up any successful allocations
    free(ext_vertices);
    free(ext_normals);
    free(vertices);
    free(indices);
    free(normals);
  }

  return plane_mesh;
}
Mesh create_plane_mesh(float scale, int resolution) {
  const int vertex_count = (resolution + 1) * (resolution + 1);
  const int triangle_count = resolution * resolution * 2;
  const int index_count = triangle_count * 3;

  Vector3 *vertices = malloc(vertex_count * sizeof(Vector3));
  unsigned short *indices = malloc(index_count * sizeof(unsigned short));
  Vector3 *normals = malloc(vertex_count * sizeof(Vector3));

  Mesh plane_mesh = {0}; // Initialize all fields in struct to 0
  if (vertices && indices && normals) {
    // Create vertex positions
    for (int z = 0; z <= resolution; z++) {
      for (int x = 0; x <= resolution; x++) {
        const int idx = z * (resolution + 1) + x;
        const float xPos = (x / (float)resolution) * 2.0f * scale - scale;
        const float zPos = (z / (float)resolution) * 2.0f * scale - scale;
        vertices[idx] = (Vector3){xPos, 0.0f, zPos};
      }
    }

    // Create indices
    int idx = 0;
    for (int z = 0; z < resolution; z++) {
      for (int x = 0; x < resolution; x++) {
        const int v0 = z * (resolution + 1) + x;
        const int v1 = v0 + 1;
        const int v2 = (z + 1) * (resolution + 1) + x;
        const int v3 = v2 + 1;

        // First triangle (counter-clockwise from above)
        indices[idx++] = v0;
        indices[idx++] = v2;
        indices[idx++] = v1;

        // Second triangle
        indices[idx++] = v1;
        indices[idx++] = v2;
        indices[idx++] = v3;
      }
    }

    // Create normals
    for (unsigned int i = 0; i < vertex_count; i++) {
      normals[i] = (Vector3){0.0f, 1.0f, 0.0f};
    }

    // Populate the mesh
    plane_mesh.vertexCount = vertex_count;
    plane_mesh.triangleCount = triangle_count;
    plane_mesh.vertices = (float *)vertices;
    plane_mesh.indices = indices;
    plane_mesh.normals = (float *)normals;

    UploadMesh(&plane_mesh, false);
    if (plane_mesh.vaoId == 0) {
      fprintf(stderr, "Mesh upload may have failed\n");
      // Clean up on upload failure
      free(vertices);
      free(indices);
      free(normals);
      plane_mesh = (Mesh){0};
    }
    // NOTE: Don't free vertices/indices/normals here - UnloadMesh() will handle
    // it
  } else {
    fprintf(stderr, "Quad memory allocation failed\n");
  }

  return plane_mesh;
}

Mesh create_triangle_mesh(float scale) {
  // Allocate on heap
  Vector3 *vertices = malloc(3 * sizeof(Vector3));
  if (!vertices) {
    fprintf(stderr, "Vertex memory allocation failed\n");
    return (Mesh){0};
  }
  unsigned short *indices = malloc(3 * sizeof(unsigned short));
  if (!indices) {
    fprintf(stderr, "Index memory allocation failed\n");
    return (Mesh){0};
  }
  Vector3 *normals = malloc(3 * sizeof(Vector3));
  if (!normals) {
    fprintf(stderr, "Normals memory allocation failed\n");
    return (Mesh){0};
  }

  // Create vertex positions
  vertices[0] = (Vector3){-scale, 0.0f, -scale}; // Bottom-left
  vertices[1] = (Vector3){scale, 0.0f, -scale};  // Bottom-right
  vertices[2] = (Vector3){0.0f, 0.0f, scale};    // Top

  // Create indices (which vertices form the triangle)
  indices[0] = 0;
  indices[1] = 2;
  indices[2] = 1;

  // Create normals (direction light bounces off the surface
  normals[0] = (Vector3){0.0f, 1.0f, 0.0f};
  normals[1] = (Vector3){0.0f, 1.0f, 0.0f};
  normals[2] = (Vector3){0.0f, 1.0f, 0.0f};

  // Create and populate a Mesh
  Mesh triangle_mesh = {0}; // Initialize all fields in struct to 0
  triangle_mesh.vertexCount = 3;
  triangle_mesh.triangleCount = 1;
  triangle_mesh.vertices = (float *)vertices;
  triangle_mesh.normals = (float *)normals;
  triangle_mesh.indices = indices;

  // Upload to GPU
  UploadMesh(&triangle_mesh, false);

  // NOTE: Don't free vertices/indices/normals here - UnloadMesh() will handle
  // it

  return triangle_mesh;
}

// ===== Sphere Patch Mesh for Cubic Quadtree =====

// ===== Sphere Patch Mesh for Cubic Quadtree =====

// Generate mesh data on CPU (no GPU dependence)
CpuMesh generate_sphere_patch_cpu(CubeFace face, 
                                 float u_min, float u_max, 
                                 float v_min, float v_max,
                                 float radius, float noise_scale,
                                 int resolution, float skirt_depth) {
  CpuMesh cpu_mesh = {0};

  // Main grid vertices: (resolution+1) x (resolution+1)
  const int grid_size = resolution + 1;
  const int main_vertex_count = grid_size * grid_size;
  const int main_triangle_count = resolution * resolution * 2;
  
  // Extended grid for proper edge normal calculation: (resolution+3) x (resolution+3)
  // This extends 1 cell beyond the patch boundaries in all directions
  const int ext_size = resolution + 3;
  const int ext_vertex_count = ext_size * ext_size;
  
  // Skirt vertices: 4 edges x (resolution+1) vertices each
  const int skirt_vertex_count = (skirt_depth > 0.0f) ? 4 * grid_size : 0;
  const int skirt_triangle_count = (skirt_depth > 0.0f) ? 4 * resolution * 2 : 0;
  
  const int total_vertex_count = main_vertex_count + skirt_vertex_count;
  const int total_triangle_count = main_triangle_count + skirt_triangle_count;
  const int total_index_count = total_triangle_count * 3;
  
  // Allocate extended grid for normal calculation
  Vector3 *ext_vertices = malloc(ext_vertex_count * sizeof(Vector3));
  Vector3 *ext_normals = malloc(ext_vertex_count * sizeof(Vector3));
  
  // Allocate final mesh arrays
  Vector3 *vertices = malloc(total_vertex_count * sizeof(Vector3));
  unsigned short *indices = malloc(total_index_count * sizeof(unsigned short));
  Vector3 *normals = malloc(total_vertex_count * sizeof(Vector3));
  
  if (!ext_vertices || !ext_normals || !vertices || !indices || !normals) {
    fprintf(stderr, "Sphere patch mesh allocation failed\n");
    free(ext_vertices);
    free(ext_normals);
    free(vertices);
    free(indices);
    free(normals);
    return cpu_mesh;
  }
  
  // UV step for the main grid
  const float u_step = (u_max - u_min) / (float)resolution;
  const float v_step = (v_max - v_min) / (float)resolution;
  
  // Generate extended grid vertices (extends 1 cell beyond patch in all directions)
  for (int y = 0; y < ext_size; y++) {
    for (int x = 0; x < ext_size; x++) {
      const int idx = y * ext_size + x;
      // (x-1, y-1) maps to the main grid origin
      float u = u_min + (x - 1) * u_step;
      float v = v_min + (y - 1) * v_step;
      
      Vector3 normal;
      Vector3 pos = cube_face_to_sphere_point_with_normal(face, u, v, radius, noise_scale, &normal);
      
      ext_vertices[idx] = pos;
      ext_normals[idx] = (Vector3){0.0f, 0.0f, 0.0f};  // Will accumulate face normals
    }
  }
  
  // Accumulate face normals across the entire extended grid
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
  
  // Check if this face needs flipped winding order
  // If so, we also need to flip normals to match
  bool flip_winding = cube_face_needs_flip(face);
  
  // Normalize accumulated normals (and flip if needed)
  for (int i = 0; i < ext_vertex_count; i++) {
    ext_normals[i] = Vector3Normalize(ext_normals[i]);
    if (flip_winding) {
      ext_normals[i] = Vector3Negate(ext_normals[i]);
    }
  }
  
  // Copy inner vertices and normals to final mesh arrays
  // Inner region: x,y in [1, resolution+1] in extended grid
  for (int y = 0; y < grid_size; y++) {
    for (int x = 0; x < grid_size; x++) {
      const int final_idx = y * grid_size + x;
      const int ext_idx = (y + 1) * ext_size + (x + 1);  // Offset by 1 to skip border
      vertices[final_idx] = ext_vertices[ext_idx];
      normals[final_idx] = ext_normals[ext_idx];
    }
  }
  
  // Free extended arrays - no longer needed
  free(ext_vertices);
  free(ext_normals);
  
  // Generate indices for main grid
  // flip_winding was already computed above for normal flipping
  
  int idx = 0;
  for (int y = 0; y < resolution; y++) {
    for (int x = 0; x < resolution; x++) {
      const int v0 = y * grid_size + x;
      const int v1 = v0 + 1;
      const int v2 = (y + 1) * grid_size + x;
      const int v3 = v2 + 1;
      
      if (flip_winding) {
        // Flipped winding: CCW -> CW
        // First triangle
        indices[idx++] = v0;
        indices[idx++] = v1;
        indices[idx++] = v2;
        
        // Second triangle
        indices[idx++] = v1;
        indices[idx++] = v3;
        indices[idx++] = v2;
      } else {
        // Normal winding
        // First triangle
        indices[idx++] = v0;
        indices[idx++] = v2;
        indices[idx++] = v1;
        
        // Second triangle
        indices[idx++] = v1;
        indices[idx++] = v2;
        indices[idx++] = v3;
      }
    }
  }
  
  // Generate skirt geometry (if enabled)
  if (skirt_depth > 0.0f) {
    int skirt_base = main_vertex_count;
    
    // Helper macro to add a skirt edge
    // edge_start: starting vertex index on main mesh edge
    // edge_step: step between vertices on that edge
    #define ADD_SKIRT_EDGE(edge_idx, get_main_vertex)                          \
      do {                                                                      \
        for (int i = 0; i < grid_size; i++) {                                   \
          int main_idx = get_main_vertex;                                       \
          int skirt_idx = skirt_base + edge_idx * grid_size + i;                \
                                                                                \
          /* Skirt vertex is main vertex pushed toward planet center */         \
          Vector3 main_pos = vertices[main_idx];                                \
          Vector3 dir_to_center = Vector3Normalize(main_pos);                   \
          vertices[skirt_idx] = Vector3Subtract(main_pos,                       \
                                  Vector3Scale(dir_to_center, skirt_depth));    \
          normals[skirt_idx] = normals[main_idx];                               \
                                                                                \
          /* Create triangles connecting main edge to skirt */                  \
          if (i < resolution) {                                                 \
            int next_main_idx = get_main_vertex + 1 - i + (i + 1);              \
            int next_skirt_idx = skirt_idx + 1;                                 \
            /* Recompute next_main_idx properly */                              \
          }                                                                     \
        }                                                                       \
      } while(0)
    
    // Bottom edge (y=0): vertices 0 to resolution
    for (int i = 0; i < grid_size; i++) {
      int main_idx = i;  // y=0
      int skirt_idx = skirt_base + i;
      
      Vector3 main_pos = vertices[main_idx];
      Vector3 dir_to_center = Vector3Normalize(main_pos);
      vertices[skirt_idx] = Vector3Subtract(main_pos, Vector3Scale(dir_to_center, skirt_depth));
      normals[skirt_idx] = normals[main_idx];
    }
    // Bottom edge triangles
    for (int i = 0; i < resolution; i++) {
      int m0 = i;
      int m1 = i + 1;
      int s0 = skirt_base + i;
      int s1 = skirt_base + i + 1;
      
      if (flip_winding) {
        indices[idx++] = m0;
        indices[idx++] = s0;
        indices[idx++] = m1;
        indices[idx++] = m1;
        indices[idx++] = s1;
        indices[idx++] = s0;
      } else {
        indices[idx++] = m0;
        indices[idx++] = m1;
        indices[idx++] = s0;
        indices[idx++] = m1;
        indices[idx++] = s0;
        indices[idx++] = s1;
      }
    }
    
    // Top edge (y=resolution): vertices resolution*grid_size to resolution*grid_size + resolution
    int top_skirt_base = skirt_base + grid_size;
    for (int i = 0; i < grid_size; i++) {
      int main_idx = resolution * grid_size + i;
      int skirt_idx = top_skirt_base + i;
      
      Vector3 main_pos = vertices[main_idx];
      Vector3 dir_to_center = Vector3Normalize(main_pos);
      vertices[skirt_idx] = Vector3Subtract(main_pos, Vector3Scale(dir_to_center, skirt_depth));
      normals[skirt_idx] = normals[main_idx];
    }
    // Top edge triangles
    for (int i = 0; i < resolution; i++) {
      int m0 = resolution * grid_size + i;
      int m1 = resolution * grid_size + i + 1;
      int s0 = top_skirt_base + i;
      int s1 = top_skirt_base + i + 1;
      
      if (flip_winding) {
        indices[idx++] = m0;
        indices[idx++] = m1;
        indices[idx++] = s0;
        indices[idx++] = m1;
        indices[idx++] = s0;
        indices[idx++] = s1;
      } else {
        indices[idx++] = m0;
        indices[idx++] = s0;
        indices[idx++] = m1;
        indices[idx++] = m1;
        indices[idx++] = s1;
        indices[idx++] = s0;
      }
    }
    
    // Left edge (x=0): vertices 0, grid_size, 2*grid_size, ...
    int left_skirt_base = skirt_base + 2 * grid_size;
    for (int i = 0; i < grid_size; i++) {
      int main_idx = i * grid_size;
      int skirt_idx = left_skirt_base + i;
      
      Vector3 main_pos = vertices[main_idx];
      Vector3 dir_to_center = Vector3Normalize(main_pos);
      vertices[skirt_idx] = Vector3Subtract(main_pos, Vector3Scale(dir_to_center, skirt_depth));
      normals[skirt_idx] = normals[main_idx];
    }
    // Left edge triangles
    for (int i = 0; i < resolution; i++) {
      int m0 = i * grid_size;
      int m1 = (i + 1) * grid_size;
      int s0 = left_skirt_base + i;
      int s1 = left_skirt_base + i + 1;
      
      if (flip_winding) {
        indices[idx++] = m0;
        indices[idx++] = s0;
        indices[idx++] = m1;
        indices[idx++] = m1;
        indices[idx++] = s0;
        indices[idx++] = s1;
      } else {
        indices[idx++] = m0;
        indices[idx++] = m1;
        indices[idx++] = s0;
        indices[idx++] = m1;
        indices[idx++] = s1;
        indices[idx++] = s0;
      }
    }
    
    // Right edge (x=resolution): vertices resolution, grid_size+resolution, ...
    int right_skirt_base = skirt_base + 3 * grid_size;
    for (int i = 0; i < grid_size; i++) {
      int main_idx = i * grid_size + resolution;
      int skirt_idx = right_skirt_base + i;
      
      Vector3 main_pos = vertices[main_idx];
      Vector3 dir_to_center = Vector3Normalize(main_pos);
      vertices[skirt_idx] = Vector3Subtract(main_pos, Vector3Scale(dir_to_center, skirt_depth));
      normals[skirt_idx] = normals[main_idx];
    }
    // Right edge triangles
    for (int i = 0; i < resolution; i++) {
      int m0 = i * grid_size + resolution;
      int m1 = (i + 1) * grid_size + resolution;
      int s0 = right_skirt_base + i;
      int s1 = right_skirt_base + i + 1;
      
      if (flip_winding) {
        indices[idx++] = m0;
        indices[idx++] = s0;
        indices[idx++] = m1;
        indices[idx++] = m1;
        indices[idx++] = s1;
        indices[idx++] = s0;
      } else {
        indices[idx++] = m0;
        indices[idx++] = m1;
        indices[idx++] = s0;
        indices[idx++] = m1;
        indices[idx++] = s0;
        indices[idx++] = s1;
      }
    }
  }
  
  cpu_mesh.vertices = (float*)vertices;
  cpu_mesh.normals = (float*)normals;
  cpu_mesh.indices = indices;
  cpu_mesh.vertexCount = total_vertex_count;
  cpu_mesh.triangleCount = total_triangle_count;
  
  return cpu_mesh;
}

Mesh upload_mesh_gpu(CpuMesh cpu_mesh) {
  Mesh mesh = {0};
  
  if (cpu_mesh.vertexCount == 0 || cpu_mesh.triangleCount == 0) return mesh;
  
  mesh.vertexCount = cpu_mesh.vertexCount;
  mesh.triangleCount = cpu_mesh.triangleCount;
  mesh.vertices = cpu_mesh.vertices;
  mesh.normals = cpu_mesh.normals;
  mesh.indices = cpu_mesh.indices;
  
  // NOTE: UploadMesh in Raylib (mostly) copies the data, but for dynamic mesh generation
  // we often want to transfer ownership or copy. 
  // Raylib's UploadMesh does NOT free the pointers.
  // However, it does expect 'vertices', 'normals', etc. to be valid pointers.
  UploadMesh(&mesh, false);
  
  if (mesh.vaoId == 0) {
    fprintf(stderr, "Sphere patch mesh upload failed\n");
  }
  
  return mesh;
}

void free_cpu_mesh(CpuMesh *cpu_mesh) {
  if (cpu_mesh->vertices) free(cpu_mesh->vertices);
  if (cpu_mesh->normals) free(cpu_mesh->normals);
  if (cpu_mesh->indices) free(cpu_mesh->indices);
  cpu_mesh->vertices = NULL;
  cpu_mesh->normals = NULL;
  cpu_mesh->indices = NULL;
  cpu_mesh->vertexCount = 0;
  cpu_mesh->triangleCount = 0;
}

Mesh create_sphere_patch_mesh(CubeFace face,
                               float u_min, float u_max,
                               float v_min, float v_max,
                               float radius, float noise_scale,
                               int resolution, float skirt_depth) {
    // Synchronous generation for compatibility
    CpuMesh cpu_mesh = generate_sphere_patch_cpu(face, u_min, u_max, v_min, v_max, 
                                                radius, noise_scale, resolution, skirt_depth);
    Mesh mesh = upload_mesh_gpu(cpu_mesh);
    // After upload, we don't need CPU data anymore for static meshes
    // Raylib UploadMesh uploads to VRAM. We must not free the pointers IF we want to keep them 
    // in CPU memory for collision etc., but for rendering-only meshes we can free them.
    // However, Raylib's UnloadMesh frees the pointers! 
    // Wait, Check Raylib source/docs:
    // "UnloadMesh: Unload mesh data from CPU and GPU" -> checks if vertices != NULL and frees them.
    // So we MUST leave the pointers in the Mesh struct if we want UnloadMesh to clean them up.
    // If we free them here, UnloadMesh will double-free or crash.
    // 
    // BUT: In our new async architecture, `upload_mesh_gpu` assigns the pointers to `mesh.vertices`.
    // So `mesh` OWNS the pointers now.
    // We should therefore NOT call `free_cpu_mesh` here if we passed the pointers to `mesh`.
    
    // Correct behavior:
    // 1. generate -> allocates Malloc
    // 2. upload -> assigns Malloc ptrs to Mesh struct, calls UploadMesh (which sends to VRAM).
    // 3. Return Mesh.
    // 4. Eventually user calls UnloadMesh -> frees Malloc ptrs + VRAM.
    
    // So we do NOTHING here regarding freeing.
    
    return mesh;                                            
}
