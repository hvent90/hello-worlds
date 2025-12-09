#include "mesh.h"
#include "noise.h"
#include "raymath.h"
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
