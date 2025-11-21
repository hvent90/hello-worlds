#include "chunk.h"
#include "noise.h"
#include <stdlib.h>
#include <raymath.h>
#include <stdio.h>

Chunk* Chunk_Create(Vector3 offset, float width, float height, float radius, int resolution, Vector3 origin, Matrix localToWorld, float terrainFrequency, float terrainAmplitude) {
    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    chunk->offset = offset;
    chunk->width = width;
    chunk->height = height;
    chunk->radius = radius;
    chunk->resolution = resolution;
    chunk->origin = origin;
    chunk->localToWorld = localToWorld;
    chunk->terrainFrequency = terrainFrequency;
    chunk->terrainAmplitude = terrainAmplitude;
    chunk->isUploaded = false;
    chunk->id = 0;

    // Initialize mesh to zero
    chunk->mesh = (Mesh){ 0 };
    chunk->model = (Model){ 0 };

    return chunk;
}

// Calculate normals from geometry using face normal averaging
static void CalculateTerrainNormals(Mesh* mesh) {
    // Initialize all normals to zero
    for (int i = 0; i < mesh->vertexCount * 3; i++) {
        mesh->normals[i] = 0.0f;
    }

    // For each triangle, calculate face normal and add to vertex normals
    for (int i = 0; i < mesh->triangleCount; i++) {
        unsigned short i0 = mesh->indices[i * 3];
        unsigned short i1 = mesh->indices[i * 3 + 1];
        unsigned short i2 = mesh->indices[i * 3 + 2];

        // Get triangle vertices
        Vector3 v0 = {
            mesh->vertices[i0 * 3],
            mesh->vertices[i0 * 3 + 1],
            mesh->vertices[i0 * 3 + 2]
        };
        Vector3 v1 = {
            mesh->vertices[i1 * 3],
            mesh->vertices[i1 * 3 + 1],
            mesh->vertices[i1 * 3 + 2]
        };
        Vector3 v2 = {
            mesh->vertices[i2 * 3],
            mesh->vertices[i2 * 3 + 1],
            mesh->vertices[i2 * 3 + 2]
        };

        // Calculate edge vectors
        Vector3 edge1 = Vector3Subtract(v1, v0);
        Vector3 edge2 = Vector3Subtract(v2, v0);

        // Calculate face normal (cross product)
        Vector3 faceNormal = Vector3CrossProduct(edge1, edge2);

        // Add face normal to each vertex of the triangle
        mesh->normals[i0 * 3] += faceNormal.x;
        mesh->normals[i0 * 3 + 1] += faceNormal.y;
        mesh->normals[i0 * 3 + 2] += faceNormal.z;

        mesh->normals[i1 * 3] += faceNormal.x;
        mesh->normals[i1 * 3 + 1] += faceNormal.y;
        mesh->normals[i1 * 3 + 2] += faceNormal.z;

        mesh->normals[i2 * 3] += faceNormal.x;
        mesh->normals[i2 * 3 + 1] += faceNormal.y;
        mesh->normals[i2 * 3 + 2] += faceNormal.z;
    }

    // Normalize all vertex normals
    for (int i = 0; i < mesh->vertexCount; i++) {
        Vector3 normal = {
            mesh->normals[i * 3],
            mesh->normals[i * 3 + 1],
            mesh->normals[i * 3 + 2]
        };
        normal = Vector3Normalize(normal);
        mesh->normals[i * 3] = normal.x;
        mesh->normals[i * 3 + 1] = normal.y;
        mesh->normals[i * 3 + 2] = normal.z;
    }
}

void Chunk_Generate(Chunk* chunk) {
    int res = chunk->resolution;
    int numVertices = (res + 1) * (res + 1);
    int numTriangles = res * res * 2;

    // Check if we need to reallocate
    if (chunk->mesh.vertexCount != numVertices) {
        // Free old if exists (Raylib UnloadMesh frees VRAM, but we also have CPU pointers)
        // If we are reusing a chunk, we might have old data.
        // For simplicity in this phase, let's assume if vertexCount matches, we reuse.

        if (chunk->mesh.vertices) free(chunk->mesh.vertices);
        if (chunk->mesh.normals) free(chunk->mesh.normals);
        if (chunk->mesh.texcoords) free(chunk->mesh.texcoords);
        if (chunk->mesh.indices) free(chunk->mesh.indices);

        chunk->mesh.vertexCount = numVertices;
        chunk->mesh.triangleCount = numTriangles;

        chunk->mesh.vertices = (float*)malloc(chunk->mesh.vertexCount * 3 * sizeof(float));
        chunk->mesh.normals = (float*)malloc(chunk->mesh.vertexCount * 3 * sizeof(float));
        chunk->mesh.texcoords = (float*)malloc(chunk->mesh.vertexCount * 2 * sizeof(float));
        chunk->mesh.indices = (unsigned short*)malloc(chunk->mesh.triangleCount * 3 * sizeof(unsigned short));
    }

    int vIndex = 0;
    int tIndex = 0;

    for (int y = 0; y <= res; y++) {
        for (int x = 0; x <= res; x++) {
            float u = (float)x / res;
            float v = (float)y / res;

            // Calculate position on the cube face
            float px = chunk->offset.x + u * chunk->width;
            float py = chunk->offset.y + v * chunk->height;

            // Local position on the face plane (z=0)
            Vector3 localPos = { px, py, 0 };

            // Transform to world space (cube face in 3D)
            Vector3 worldPos = Vector3Transform(localPos, chunk->localToWorld);

            // Normalize to project onto sphere
            Vector3 normalized = Vector3Normalize(worldPos);

            // Apply noise-based height variation for moon-like terrain
            // Normalize to [0,1] range across entire face (-radius to +radius)
            float faceSizeTotal = 2.0f * chunk->radius;
            float normalizedX = (px + chunk->radius) / faceSizeTotal;
            float normalizedY = (py + chunk->radius) / faceSizeTotal;

            // Scale for appropriate noise frequency
            // Lower scale = larger features visible
            // For moon (1737km radius): scale of 15-20 gives realistic crater sizes
            float noiseX = normalizedX * chunk->terrainFrequency;
            float noiseY = normalizedY * chunk->terrainFrequency;
            float heightNoise = MoonTerrain(noiseX, noiseY);

            // Height scaling for realistic lunar features:
            // - Maria vs Highlands: 1-3 km elevation difference
            // - Large craters: several km deep
            // - Total relief: ~5-8 km range
            //
            // MoonTerrain returns values roughly in range [-1.5, +1.5]
            // terrainAmplitude controls the height variation
            // Default 0.003 (~0.3% of radius) gives realistic scale for moon
            float heightVariation = chunk->radius * chunk->terrainAmplitude * heightNoise;
            float adjustedRadius = chunk->radius + heightVariation;

            // Scale by radius and add origin
            Vector3 finalPos = Vector3Add(Vector3Scale(normalized, adjustedRadius), chunk->origin);

            chunk->mesh.vertices[vIndex * 3] = finalPos.x;
            chunk->mesh.vertices[vIndex * 3 + 1] = finalPos.y;
            chunk->mesh.vertices[vIndex * 3 + 2] = finalPos.z;

            // Normals will be calculated after all vertices are generated
            chunk->mesh.normals[vIndex * 3] = 0.0f;
            chunk->mesh.normals[vIndex * 3 + 1] = 0.0f;
            chunk->mesh.normals[vIndex * 3 + 2] = 0.0f;

            chunk->mesh.texcoords[vIndex * 2] = u;
            chunk->mesh.texcoords[vIndex * 2 + 1] = v;

            // Indices
            if (x < res && y < res) {
                int topLeft = y * (res + 1) + x;
                int topRight = topLeft + 1;
                int bottomLeft = (y + 1) * (res + 1) + x;
                int bottomRight = bottomLeft + 1;

                chunk->mesh.indices[tIndex * 3] = topLeft;
                chunk->mesh.indices[tIndex * 3 + 1] = topRight;
                chunk->mesh.indices[tIndex * 3 + 2] = bottomLeft;

                chunk->mesh.indices[tIndex * 3 + 3] = topRight;
                chunk->mesh.indices[tIndex * 3 + 4] = bottomRight;
                chunk->mesh.indices[tIndex * 3 + 5] = bottomLeft;

                tIndex += 2;
            }

            vIndex++;
        }
    }

    // Calculate terrain-aware normals from the actual geometry
    CalculateTerrainNormals(&chunk->mesh);

    // Upload to GPU
    if (chunk->isUploaded) {
        // Prevent Raylib from freeing our CPU buffers which we want to reuse
        // LoadModelFromMesh creates a copy of the mesh struct in model.meshes[0]
        if (chunk->model.meshes) {
            chunk->model.meshes[0].vertices = NULL;
            chunk->model.meshes[0].normals = NULL;
            chunk->model.meshes[0].texcoords = NULL;
            chunk->model.meshes[0].indices = NULL;
        }

        UnloadModel(chunk->model);

        // Reset IDs so UploadMesh creates new VAO/VBOs
        chunk->mesh.vaoId = 0;
        chunk->mesh.vboId = 0;
    }

    UploadMesh(&chunk->mesh, false);
    chunk->model = LoadModelFromMesh(chunk->mesh);
    chunk->isUploaded = true;
}

void Chunk_Draw(Chunk* chunk, Color surfaceColor, Color wireframeColor, Shader lightingShader) {
    if (chunk->isUploaded) {
        // Apply lighting shader to the model's material
        chunk->model.materials[0].shader = lightingShader;

        // Draw with lighting
        DrawModel(chunk->model, (Vector3){0,0,0}, 1.0f, surfaceColor);

        // Draw wireframe (without shader for better visibility)
        DrawModelWires(chunk->model, (Vector3){0,0,0}, 1.0f, wireframeColor);
    }
}

void Chunk_DrawWithShadow(Chunk* chunk, Color surfaceColor, Color wireframeColor, Shader lightingShader, Texture2D shadowMap) {
    if (chunk->isUploaded) {
        // Apply lighting shader to the model's material
        chunk->model.materials[0].shader = lightingShader;

        // Set shadow map as material map (MATERIAL_MAP_METALNESS = texture unit 1)
        chunk->model.materials[0].maps[MATERIAL_MAP_METALNESS].texture = shadowMap;

        // Draw with lighting and shadows
        DrawModel(chunk->model, (Vector3){0,0,0}, 1.0f, surfaceColor);

        // Draw wireframe (without shader for better visibility)
        DrawModelWires(chunk->model, (Vector3){0,0,0}, 1.0f, wireframeColor);
    }
}

void Chunk_Free(Chunk* chunk) {
    if (chunk->isUploaded) {
        UnloadModel(chunk->model); // Unloads GPU data
    }
    
    // Free CPU data
    if (chunk->mesh.vertices) free(chunk->mesh.vertices);
    if (chunk->mesh.normals) free(chunk->mesh.normals);
    if (chunk->mesh.texcoords) free(chunk->mesh.texcoords);
    if (chunk->mesh.indices) free(chunk->mesh.indices);
    
    free(chunk);
}
