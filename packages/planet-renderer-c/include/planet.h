#ifndef PLANET_H
#define PLANET_H

#include "cubic_quadtree.h"
#include "chunk.h"
#include "chunk_map.h"
#include <raylib.h>

typedef struct Planet {
    CubicQuadTree* quadtree;      // Now ephemeral, rebuilt each frame
    ChunkMap* chunkMap;            // NEW: Persistent chunk storage
    float radius;
    float minCellSize;
    int minCellResolution;
    Vector3 origin;
    float lodDistanceComparisonValue;  // NEW: Store comparator
} Planet;

Planet* Planet_Create(float radius, float minCellSize, int minCellResolution, Vector3 origin);
void Planet_Update(Planet* planet, Vector3 cameraPosition);
void Planet_Draw(Planet* planet);
void Planet_Free(Planet* planet);

#endif // PLANET_H
