#ifndef PLANET_H
#define PLANET_H

#include <raylib.h>
#include "chunk.h"
#include "cubic_quadtree.h"

#define MAX_CHUNKS 16384

// Chunk map entry
typedef struct ChunkMapEntry {
    char key[64];
    Chunk* chunk;
    bool active;
} ChunkMapEntry;

// Planet structure
typedef struct Planet {
    float radius;
    float minCellSize;
    int minCellResolution;
    float lodDistanceComparisonValue;
    bool inverted;
    Vector3 position;

    ChunkMapEntry chunkMap[MAX_CHUNKS];
    int chunkCount;

    Matrix cubeFaceTransforms[CUBE_FACES];

    HeightGenerator heightGen;
    ColorGenerator colorGen;
    void* userData;

    Shader shader;

    // Floating origin support
    Vector3 worldOffset;           // Accumulated offset of world from "true" position
    float floatingOriginThreshold; // Distance from origin before recentering (default: 1000.0f)
    bool floatingOriginEnabled;    // Enable/disable feature
} Planet;

// Planet operations
Planet* Planet_Create(float radius, float minCellSize, int minCellResolution,
                      HeightGenerator heightGen, ColorGenerator colorGen, void* userData);
void Planet_Update(Planet* planet, Vector3 lodOrigin);
void Planet_Render(Planet* planet);
void Planet_Destroy(Planet* planet);

// Internal helpers
void Planet_MakeChunkKey(int faceIndex, Vector3 position, float size, char* outKey);
Chunk* Planet_FindChunk(Planet* planet, const char* key);
void Planet_AddChunk(Planet* planet, const char* key, Chunk* chunk);
void Planet_RemoveChunk(Planet* planet, const char* key);

#endif // PLANET_H
