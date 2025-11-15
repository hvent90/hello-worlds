#ifndef CHUNK_H
#define CHUNK_H

#include <raylib.h>
#include <stdbool.h>

// Height generator function pointer
typedef float (*HeightGenerator)(Vector3 worldPosition, float radius, void* userData);

// Color generator function pointer
typedef Color (*ColorGenerator)(Vector3 worldPosition, float height, void* userData);

// Chunk properties
typedef struct ChunkProps {
    Vector3 offset;
    Vector3 origin;
    Matrix worldMatrix;
    float width;
    float height;
    float radius;
    int resolution;
    float minCellSize;
    bool inverted;
    HeightGenerator heightGen;
    ColorGenerator colorGen;
    void* userData;
} ChunkProps;

// Chunk mesh data
typedef struct ChunkMeshData {
    float* positions;
    float* normals;
    float* colors;
    float* uvs;
    unsigned int* indices;
    int vertexCount;
    int indexCount;
} ChunkMeshData;

// Chunk structure
typedef struct Chunk {
    Mesh mesh;
    Model model;
    Vector3 offset;
    Vector3 position;
    float width;
    float height;
    float radius;
    int resolution;
    bool visible;
    bool meshGenerated;
    Matrix transform;
} Chunk;

// Chunk operations
Chunk* Chunk_Create(ChunkProps props);
void Chunk_GenerateMesh(Chunk* chunk, ChunkProps props);
void Chunk_Show(Chunk* chunk);
void Chunk_Hide(Chunk* chunk);
void Chunk_Render(Chunk* chunk);
void Chunk_Destroy(Chunk* chunk);

// Chunk generation helpers
void GenerateInitialHeights(ChunkProps props, float** outPositions, float** outColors, float** outUps, int* outVertexCount);
void GenerateIndices(int resolution, unsigned int** outIndices, int* outIndexCount);
void GenerateNormals(float* positions, unsigned int* indices, int vertexCount, int indexCount, float** outNormals);
void FixEdgeSkirts(int resolution, float* positions, float* ups, float* normals, float width, float radius, bool inverted);
void NormalizeNormals(float* normals, int vertexCount);

#endif // CHUNK_H
