#ifndef PLANET_H
#define PLANET_H

#include "cubic_quadtree.h"
#include "chunk.h"
#include "chunk_utils.h"
#include "thread_pool.h"
#include <raylib.h>

typedef struct Planet {
    CubicQuadTree* quadtree;
    ChunkMap* chunkMap;
    ChunkPool* chunkPool;
    ThreadPool* threadPool;
    float radius;
    float minCellSize;
    int minCellResolution;
    Vector3 origin;
    Color surfaceColor;
    Color wireframeColor;
    Shader lightingShader;
    Texture2D shadowMapTexture;
    // Terrain generation parameters
    float terrainFrequency;  // Noise frequency multiplier (affects feature size)
    float terrainAmplitude;  // Height variation multiplier (affects feature height)
} Planet;

Planet* Planet_Create(float radius, float minCellSize, int minCellResolution, Vector3 origin, float terrainFrequency, float terrainAmplitude);
void Planet_Update(Planet* planet, Vector3 cameraPosition);
int Planet_Draw(Planet* planet);
int Planet_DrawWithShader(Planet* planet, Shader shader);
void Planet_Free(Planet* planet);

#endif // PLANET_H
