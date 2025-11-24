#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include "noise.h"
#include "raymath.h"

Mesh create_plane_mesh_with_noise(float scale, int resolution) {
    const int vertex_count = (resolution + 1) * (resolution + 1);
    const int triangle_count = resolution * resolution * 2;
    const int index_count = triangle_count * 3;

    Vector3 *vertices = malloc(vertex_count * sizeof(Vector3));
    unsigned short *indices = malloc(index_count * sizeof(unsigned short));
    Vector3 *normals = malloc(vertex_count * sizeof(Vector3));

    Mesh plane_mesh = { 0 }; // Initialize all fields in struct to 0
    if (vertices && indices && normals) {
        // Create vertex positions
        for (int z = 0; z <= resolution; z++) {
            for (int x = 0; x <= resolution; x++) {
                const int idx = z * (resolution + 1) + x;
                const float xPos = (x / (float)resolution) * 2.0f * scale - scale;
                const float zPos = (z / (float)resolution) * 2.0f * scale - scale;
                vertices[idx] = (Vector3){xPos, evaluate_moon_noise(xPos * 0.00002f, zPos * 0.00002f) * 3000.0f, zPos};
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
        plane_mesh.vertices = (float*)vertices;
        plane_mesh.indices = indices;
        plane_mesh.normals = (float*)normals;

        UploadMesh(&plane_mesh, false);
        if (plane_mesh.vaoId == 0) {
            fprintf(stderr, "Mesh upload may have failed\n");
            // Clean up on upload failure
            free(vertices);
            free(indices);
            free(normals);
            plane_mesh = (Mesh){0};
        }
        // NOTE: Don't free vertices/indices/normals here - UnloadMesh() will handle it
    } else {
        fprintf(stderr, "Quad memory allocation failed\n");
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

    Mesh plane_mesh = { 0 }; // Initialize all fields in struct to 0
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
        plane_mesh.vertices = (float*)vertices;
        plane_mesh.indices = indices;
        plane_mesh.normals = (float*)normals;

        UploadMesh(&plane_mesh, false);
        if (plane_mesh.vaoId == 0) {
            fprintf(stderr, "Mesh upload may have failed\n");
            // Clean up on upload failure
            free(vertices);
            free(indices);
            free(normals);
            plane_mesh = (Mesh){0};
        }
        // NOTE: Don't free vertices/indices/normals here - UnloadMesh() will handle it
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
    vertices[1] = (Vector3){scale, 0.0f, -scale}; // Bottom-right
    vertices[2] = (Vector3){0.0f, 0.0f, scale}; // Top

    // Create indices (which vertices form the triangle)
    indices[0] = 0;
    indices[1] = 2;
    indices[2] = 1;

    // Create normals (direction light bounces off the surface
    normals[0] = (Vector3){0.0f, 1.0f, 0.0f};
    normals[1] = (Vector3){0.0f, 1.0f, 0.0f};
    normals[2] = (Vector3){0.0f, 1.0f, 0.0f};

    // Create and populate a Mesh
    Mesh triangle_mesh = { 0 }; // Initialize all fields in struct to 0
    triangle_mesh.vertexCount = 3;
    triangle_mesh.triangleCount = 1;
    triangle_mesh.vertices = (float*)vertices;
    triangle_mesh.normals = (float*)normals;
    triangle_mesh.indices = indices;

    // Upload to GPU
    UploadMesh(&triangle_mesh, false);

    // NOTE: Don't free vertices/indices/normals here - UnloadMesh() will handle it

    return triangle_mesh;
}
