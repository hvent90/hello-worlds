#include "chunk.h"
#include <raymath.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// Helper to generate initial heights and positions
void GenerateInitialHeights(ChunkProps props, float** outPositions, float** outColors,
                           float** outUps, int* outVertexCount) {
    float half = props.width / 2.0f;
    int resolution = props.resolution + 4;
    int effectiveResolution = props.resolution + 1;

    int vertexCount = resolution * resolution;
    *outVertexCount = vertexCount;

    // NOTE: positions will become mesh.vertices, managed by raylib - use MemAlloc
    *outPositions = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    // NOTE: colors and ups are temporary arrays freed in this module - use malloc
    *outColors = (float*)malloc(vertexCount * 4 * sizeof(float));
    *outUps = (float*)malloc(vertexCount * 3 * sizeof(float));

    int idx = 0;
    for (int x = -1; x <= effectiveResolution + 1; x++) {
        float xp = (props.width * x) / effectiveResolution;
        for (int y = -1; y <= effectiveResolution + 1; y++) {
            float yp = (props.width * y) / effectiveResolution;

            // Start with flat grid position
            Vector3 p = {xp - half, yp - half, props.radius};
            p = Vector3Add(p, props.offset);

            // Normalize to get direction vector
            Vector3 direction = Vector3Normalize(p);

            // Scale to sphere surface
            p = Vector3Scale(direction, props.radius);
            p.z -= props.radius; // Adjust Z

            // Transform to world space
            Vector3 worldPos = Vector3Transform(p, props.worldMatrix);

            // Get height from generator
            float height = 0.0f;
            if (props.heightGen) {
                height = props.heightGen(worldPos, props.radius, props.userData);
            }

            // Get color from generator
            Color color = WHITE;
            if (props.colorGen) {
                color = props.colorGen(worldPos, height, props.userData);
            }

            // Apply height along direction
            Vector3 heightVec = Vector3Scale(direction, height * (props.inverted ? -1.0f : 1.0f));
            p = Vector3Add(p, heightVec);

            // Store position in LOCAL space (transformation will be applied during rendering)
            (*outPositions)[idx * 3 + 0] = p.x;
            (*outPositions)[idx * 3 + 1] = p.y;
            (*outPositions)[idx * 3 + 2] = p.z;

            // Store color
            (*outColors)[idx * 4 + 0] = color.r / 255.0f;
            (*outColors)[idx * 4 + 1] = color.g / 255.0f;
            (*outColors)[idx * 4 + 2] = color.b / 255.0f;
            (*outColors)[idx * 4 + 3] = color.a / 255.0f;

            // Store up vector in LOCAL space (transformation will be applied during rendering)
            (*outUps)[idx * 3 + 0] = direction.x;
            (*outUps)[idx * 3 + 1] = direction.y;
            (*outUps)[idx * 3 + 2] = direction.z;

            idx++;
        }
    }
}

// Generate triangle indices
void GenerateIndices(int resolution, unsigned int** outIndices, int* outIndexCount) {
    // Grid dimensions: (resolution + 4) x (resolution + 4) vertices
    // Number of quads: (resolution + 3) x (resolution + 3)
    int gridSize = resolution + 4;  // Number of vertices per side
    int quadCount = resolution + 3; // Number of quads per side
    int indexCount = quadCount * quadCount * 6;
    *outIndices = (unsigned int*)malloc(indexCount * sizeof(unsigned int));
    *outIndexCount = indexCount;

    int idx = 0;
    for (int i = 0; i < quadCount; i++) {
        for (int j = 0; j < quadCount; j++) {
            // Triangle 1
            (*outIndices)[idx++] = i * gridSize + j;
            (*outIndices)[idx++] = (i + 1) * gridSize + j + 1;
            (*outIndices)[idx++] = i * gridSize + j + 1;

            // Triangle 2
            (*outIndices)[idx++] = (i + 1) * gridSize + j;
            (*outIndices)[idx++] = (i + 1) * gridSize + j + 1;
            (*outIndices)[idx++] = i * gridSize + j;
        }
    }
}

// Generate normals from positions and indices
void GenerateNormals(float* positions, unsigned int* indices, int vertexCount,
                    int indexCount, float** outNormals) {
    // NOTE: normals will become mesh.normals, managed by raylib - use MemAlloc
    *outNormals = (float*)MemAlloc(vertexCount * 3 * sizeof(float));
    memset(*outNormals, 0, vertexCount * 3 * sizeof(float));

    // Calculate face normals and accumulate
    for (int i = 0; i < indexCount; i += 3) {
        unsigned int i0 = indices[i + 0];
        unsigned int i1 = indices[i + 1];
        unsigned int i2 = indices[i + 2];

        Vector3 v0 = {positions[i0 * 3 + 0], positions[i0 * 3 + 1], positions[i0 * 3 + 2]};
        Vector3 v1 = {positions[i1 * 3 + 0], positions[i1 * 3 + 1], positions[i1 * 3 + 2]};
        Vector3 v2 = {positions[i2 * 3 + 0], positions[i2 * 3 + 1], positions[i2 * 3 + 2]};

        Vector3 edge1 = Vector3Subtract(v2, v1);
        Vector3 edge2 = Vector3Subtract(v0, v1);
        Vector3 normal = Vector3CrossProduct(edge1, edge2);

        // Accumulate normals for each vertex
        (*outNormals)[i0 * 3 + 0] += normal.x;
        (*outNormals)[i0 * 3 + 1] += normal.y;
        (*outNormals)[i0 * 3 + 2] += normal.z;

        (*outNormals)[i1 * 3 + 0] += normal.x;
        (*outNormals)[i1 * 3 + 1] += normal.y;
        (*outNormals)[i1 * 3 + 2] += normal.z;

        (*outNormals)[i2 * 3 + 0] += normal.x;
        (*outNormals)[i2 * 3 + 1] += normal.y;
        (*outNormals)[i2 * 3 + 2] += normal.z;
    }
}

// Fix edge skirts to prevent gaps between LOD levels
void FixEdgeSkirts(int resolution, float* positions, float* ups, float* normals,
                  float width, float radius, bool inverted) {
    int effectiveResolution = resolution + 2;
    int gridSize = effectiveResolution + 1;  // Stride for row-major indexing

    // Clamp skirt size to prevent extreme spikes
    float skirtSize = fminf(width, radius / 5.0f);
    if (skirtSize < 0) skirtSize = 0;

    // Helper macro to apply proxy-based skirt fix
    #define ApplyFix(xi, yi, proxyXi, proxyYi) do { \
        int skirtIndex = (xi) * gridSize + (yi); \
        int proxyIndex = (proxyXi) * gridSize + (proxyYi); \
        \
        Vector3 P = {positions[proxyIndex * 3 + 0], \
                     positions[proxyIndex * 3 + 1], \
                     positions[proxyIndex * 3 + 2]}; \
        \
        Vector3 D = {ups[proxyIndex * 3 + 0], \
                     ups[proxyIndex * 3 + 1], \
                     ups[proxyIndex * 3 + 2]}; \
        \
        D = Vector3Scale(D, inverted ? skirtSize : -skirtSize); \
        P = Vector3Add(P, D); \
        \
        positions[skirtIndex * 3 + 0] = P.x; \
        positions[skirtIndex * 3 + 1] = P.y; \
        positions[skirtIndex * 3 + 2] = P.z; \
        \
        normals[skirtIndex * 3 + 0] = normals[proxyIndex * 3 + 0]; \
        normals[skirtIndex * 3 + 1] = normals[proxyIndex * 3 + 1]; \
        normals[skirtIndex * 3 + 2] = normals[proxyIndex * 3 + 2]; \
    } while(0)

    // Left edge (x = 0): copy from x = 1
    for (int y = 0; y <= effectiveResolution; y++) {
        ApplyFix(0, y, 1, y);
    }

    // Right edge (x = effectiveResolution): copy from x = effectiveResolution - 1
    for (int y = 0; y <= effectiveResolution; y++) {
        ApplyFix(effectiveResolution, y, effectiveResolution - 1, y);
    }

    // Top edge (y = 0): copy from y = 1
    for (int x = 0; x <= effectiveResolution; x++) {
        ApplyFix(x, 0, x, 1);
    }

    // Bottom edge (y = effectiveResolution): copy from y = effectiveResolution - 1
    for (int x = 0; x <= effectiveResolution; x++) {
        ApplyFix(x, effectiveResolution, x, effectiveResolution - 1);
    }

    #undef ApplyFix
}

// Normalize all normals
void NormalizeNormals(float* normals, int vertexCount) {
    for (int i = 0; i < vertexCount; i++) {
        Vector3 n = {normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]};
        n = Vector3Normalize(n);
        normals[i * 3 + 0] = n.x;
        normals[i * 3 + 1] = n.y;
        normals[i * 3 + 2] = n.z;
    }
}

// Create a chunk
Chunk* Chunk_Create(ChunkProps props) {
    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    chunk->offset = props.offset;
    chunk->position = props.offset;
    chunk->width = props.width;
    chunk->height = props.height;
    chunk->radius = props.radius;
    chunk->resolution = props.resolution;
    chunk->visible = false;
    chunk->meshGenerated = false;
    chunk->transform = props.worldMatrix;  // Store the cube face transformation matrix

    // Initialize empty mesh
    chunk->mesh = (Mesh){0};

    return chunk;
}

// Generate the chunk mesh
void Chunk_GenerateMesh(Chunk* chunk, ChunkProps props) {
    float* positions = NULL;
    float* colors = NULL;
    float* ups = NULL;
    float* normals = NULL;
    unsigned int* indices = NULL;
    int vertexCount = 0;
    int indexCount = 0;

    // Generate initial heights
    GenerateInitialHeights(props, &positions, &colors, &ups, &vertexCount);

    if (!positions || !colors || !ups || vertexCount <= 0) {
        printf("ERROR: GenerateInitialHeights failed\n");
        return;
    }

    // Generate indices
    GenerateIndices(props.resolution, &indices, &indexCount);

    if (!indices || indexCount <= 0) {
        printf("ERROR: GenerateIndices failed\n");
        MemFree(positions);  // Allocated with MemAlloc
        free(colors);        // Allocated with malloc
        free(ups);           // Allocated with malloc
        return;
    }

    // Generate normals
    GenerateNormals(positions, indices, vertexCount, indexCount, &normals);

    if (!normals) {
        printf("ERROR: GenerateNormals failed\n");
        MemFree(positions);  // Allocated with MemAlloc
        free(colors);        // Allocated with malloc
        free(ups);           // Allocated with malloc
        free(indices);       // Allocated with malloc
        return;
    }

    // Fix edge skirts
    FixEdgeSkirts(props.resolution, positions, ups, normals, props.width, props.radius, props.inverted);

    // Normalize normals
    NormalizeNormals(normals, vertexCount);

    printf("DEBUG: About to create mesh with vertexCount=%d, indexCount=%d\n", vertexCount, indexCount);

    // Create raylib mesh
    chunk->mesh.vertexCount = vertexCount;
    chunk->mesh.triangleCount = indexCount / 3;

    chunk->mesh.vertices = positions;
    chunk->mesh.normals = normals;

    printf("DEBUG: Allocating colors array: %d bytes\n", vertexCount * 4);
    // NOTE: mesh.colors is managed by raylib - use MemAlloc
    chunk->mesh.colors = (unsigned char*)MemAlloc(vertexCount * 4 * sizeof(unsigned char));

    if (!chunk->mesh.colors) {
        printf("ERROR: Failed to allocate mesh colors\n");
        MemFree(positions);  // Allocated with MemAlloc
        free(colors);        // Allocated with malloc
        free(ups);           // Allocated with malloc
        MemFree(normals);    // Allocated with MemAlloc
        free(indices);       // Allocated with malloc
        return;
    }

    // Convert float colors to byte colors
    for (int i = 0; i < vertexCount; i++) {
        chunk->mesh.colors[i * 4 + 0] = (unsigned char)(colors[i * 4 + 0] * 255.0f);
        chunk->mesh.colors[i * 4 + 1] = (unsigned char)(colors[i * 4 + 1] * 255.0f);
        chunk->mesh.colors[i * 4 + 2] = (unsigned char)(colors[i * 4 + 2] * 255.0f);
        chunk->mesh.colors[i * 4 + 3] = (unsigned char)(colors[i * 4 + 3] * 255.0f);
    }

    // NOTE: mesh.indices is managed by raylib - use MemAlloc
    chunk->mesh.indices = (unsigned short*)MemAlloc(indexCount * sizeof(unsigned short));
    for (int i = 0; i < indexCount; i++) {
        chunk->mesh.indices[i] = (unsigned short)indices[i];
    }

    // Upload mesh to GPU
    UploadMesh(&chunk->mesh, false);

    // Create model from mesh
    chunk->model = LoadModelFromMesh(chunk->mesh);

    chunk->meshGenerated = true;

    // Clean up temporary data
    free(colors);
    free(ups);
    free(indices);
}

void Chunk_Show(Chunk* chunk) {
    chunk->visible = true;
}

void Chunk_Hide(Chunk* chunk) {
    chunk->visible = false;
}

void Chunk_Render(Chunk* chunk) {
    if (chunk->visible && chunk->meshGenerated) {
        // Apply the cube face transformation to orient the chunk correctly
        chunk->model.transform = chunk->transform;
        DrawModel(chunk->model, (Vector3){0, 0, 0}, 1.0f, WHITE);
    }
}

void Chunk_Destroy(Chunk* chunk) {
    if (!chunk) return;

    if (chunk->meshGenerated) {
        UnloadModel(chunk->model);
    }

    free(chunk);
}
