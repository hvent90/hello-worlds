#include "chunk.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// Helper to generate initial heights and positions
void GenerateInitialHeights(ChunkProps props, float** outPositions, float** outColors,
                           float** outUps, int* outVertexCount) {
    float half = props.width / 2.0f;
    int resolution = props.resolution + 2;
    int effectiveResolution = props.resolution;

    int vertexCount = resolution * resolution;
    *outVertexCount = vertexCount;

    *outPositions = (float*)malloc(vertexCount * 3 * sizeof(float));
    *outColors = (float*)malloc(vertexCount * 4 * sizeof(float));
    *outUps = (float*)malloc(vertexCount * 3 * sizeof(float));

    int idx = 0;
    for (int x = -1; x <= effectiveResolution; x++) {
        float xp = (props.width * x) / effectiveResolution;
        for (int y = -1; y <= effectiveResolution; y++) {
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

            // Store position
            (*outPositions)[idx * 3 + 0] = p.x;
            (*outPositions)[idx * 3 + 1] = p.y;
            (*outPositions)[idx * 3 + 2] = p.z;

            // Store color
            (*outColors)[idx * 4 + 0] = color.r / 255.0f;
            (*outColors)[idx * 4 + 1] = color.g / 255.0f;
            (*outColors)[idx * 4 + 2] = color.b / 255.0f;
            (*outColors)[idx * 4 + 3] = color.a / 255.0f;

            // Store up vector (direction)
            (*outUps)[idx * 3 + 0] = direction.x;
            (*outUps)[idx * 3 + 1] = direction.y;
            (*outUps)[idx * 3 + 2] = direction.z;

            idx++;
        }
    }
}

// Generate triangle indices
void GenerateIndices(int resolution, unsigned int** outIndices, int* outIndexCount) {
    int effectiveResolution = resolution + 2;
    int indexCount = effectiveResolution * effectiveResolution * 6;
    *outIndices = (unsigned int*)malloc(indexCount * sizeof(unsigned int));
    *outIndexCount = indexCount;

    int idx = 0;
    for (int i = 0; i < effectiveResolution; i++) {
        for (int j = 0; j < effectiveResolution; j++) {
            // Triangle 1
            (*outIndices)[idx++] = i * (effectiveResolution + 1) + j;
            (*outIndices)[idx++] = (i + 1) * (effectiveResolution + 1) + j + 1;
            (*outIndices)[idx++] = i * (effectiveResolution + 1) + j + 1;

            // Triangle 2
            (*outIndices)[idx++] = (i + 1) * (effectiveResolution + 1) + j;
            (*outIndices)[idx++] = (i + 1) * (effectiveResolution + 1) + j + 1;
            (*outIndices)[idx++] = i * (effectiveResolution + 1) + j;
        }
    }
}

// Generate normals from positions and indices
void GenerateNormals(float* positions, unsigned int* indices, int vertexCount,
                    int indexCount, float** outNormals) {
    *outNormals = (float*)calloc(vertexCount * 3, sizeof(float));

    // Calculate face normals and accumulate
    for (int i = 0; i < indexCount; i += 3) {
        unsigned int i0 = indices[i + 0];
        unsigned int i1 = indices[i + 1];
        unsigned int i2 = indices[i + 2];

        Vector3 v0 = {positions[i0 * 3 + 0], positions[i0 * 3 + 1], positions[i0 * 3 + 2]};
        Vector3 v1 = {positions[i1 * 3 + 0], positions[i1 * 3 + 1], positions[i1 * 3 + 2]};
        Vector3 v2 = {positions[i2 * 3 + 0], positions[i2 * 3 + 1], positions[i2 * 3 + 2]};

        Vector3 edge1 = Vector3Subtract(v1, v0);
        Vector3 edge2 = Vector3Subtract(v2, v0);
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
    float skirtDepth = width * 0.1f;

    for (int i = 0; i < effectiveResolution + 1; i++) {
        for (int j = 0; j < effectiveResolution + 1; j++) {
            bool isEdge = (i == 0 || i == effectiveResolution || j == 0 || j == effectiveResolution);

            if (isEdge) {
                int idx = i * (effectiveResolution + 1) + j;

                Vector3 up = {ups[idx * 3 + 0], ups[idx * 3 + 1], ups[idx * 3 + 2]};
                Vector3 offset = Vector3Scale(up, -skirtDepth * (inverted ? -1.0f : 1.0f));

                positions[idx * 3 + 0] += offset.x;
                positions[idx * 3 + 1] += offset.y;
                positions[idx * 3 + 2] += offset.z;
            }
        }
    }
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
    chunk->transform = MatrixIdentity();

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

    // Generate indices
    GenerateIndices(props.resolution, &indices, &indexCount);

    // Generate normals
    GenerateNormals(positions, indices, vertexCount, indexCount, &normals);

    // Fix edge skirts
    FixEdgeSkirts(props.resolution, positions, ups, normals, props.width, props.radius, props.inverted);

    // Normalize normals
    NormalizeNormals(normals, vertexCount);

    // Create raylib mesh
    chunk->mesh.vertexCount = vertexCount;
    chunk->mesh.triangleCount = indexCount / 3;

    chunk->mesh.vertices = positions;
    chunk->mesh.normals = normals;
    chunk->mesh.colors = (unsigned char*)malloc(vertexCount * 4 * sizeof(unsigned char));

    // Convert float colors to byte colors
    for (int i = 0; i < vertexCount; i++) {
        chunk->mesh.colors[i * 4 + 0] = (unsigned char)(colors[i * 4 + 0] * 255.0f);
        chunk->mesh.colors[i * 4 + 1] = (unsigned char)(colors[i * 4 + 1] * 255.0f);
        chunk->mesh.colors[i * 4 + 2] = (unsigned char)(colors[i * 4 + 2] * 255.0f);
        chunk->mesh.colors[i * 4 + 3] = (unsigned char)(colors[i * 4 + 3] * 255.0f);
    }

    chunk->mesh.indices = (unsigned short*)malloc(indexCount * sizeof(unsigned short));
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
        DrawModel(chunk->model, chunk->position, 1.0f, WHITE);
    }
}

void Chunk_Destroy(Chunk* chunk) {
    if (!chunk) return;

    if (chunk->meshGenerated) {
        UnloadModel(chunk->model);
    }

    free(chunk);
}
